#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"

#include <android/log.h>
#include <atomic>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <mutex>
#include <shadowhook.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr const char* kTag = "HybridMCPE";
constexpr const char* kProfile = "bedrock-1.26.50.4-arm64";
constexpr uintptr_t kCallsiteElfVa = 0xfb3c1ac;
constexpr uintptr_t kTargetElfVa = 0x10487d40;
constexpr uintptr_t kRegistryGlobalElfVa = 0x134d7db8;
constexpr uintptr_t kCobblestoneConstructorElfVa = 0x100fb840;
constexpr uintptr_t kBlockStateSweepElfVa = 0x103e2dd4;
constexpr uint32_t kOriginalCall = 0x94252ee5;
constexpr uint32_t kTargetPrologue[4]{0xfc190fe8, 0xa9017bfd, 0xa9026ffc, 0xa90367fa};
constexpr uint32_t kCobblestoneConstructorPrologue[4]{0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0xaa0003f3};
constexpr uint32_t kBlockStateSweepPrologue[4]{0xd10283ff, 0xa9057bfd, 0xa90667fa, 0xa9075ff8};
uint64_t monotonic_ns();
bool engine_executable(uintptr_t address);

#ifdef HYBRID_EXPERIMENTAL_ENGINE_HOOK
using CandidateFunction = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
std::mutex candidate_hook_lock;
void* candidate_hook_stub = nullptr;
CandidateFunction candidate_original = nullptr;
std::atomic<unsigned> candidate_hits{0};
std::atomic<uintptr_t> candidate_expected_return_pc{0};
std::atomic<uintptr_t> candidate_registry_identity{0};

// shortcut: forward the four GP arguments observed at the verified caller; extend only if another callsite proves more.
using CobblestoneConstructor = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
std::mutex cobblestone_hook_lock;
void* cobblestone_hook_stub = nullptr;
CobblestoneConstructor cobblestone_constructor_original = nullptr;
std::atomic<unsigned> cobblestone_constructor_hits{0};
std::atomic<uintptr_t> cobblestone_constructor_objects[64]{};
using StateAppend = uintptr_t (*)(uintptr_t, uintptr_t);
StateAppend state_append_original = nullptr, palette_append_original = nullptr;
void* state_append_stub = nullptr;
void* palette_append_stub = nullptr;
StateAppend state_factory_original = nullptr;
void* state_factory_stub = nullptr;
std::atomic<uintptr_t> state_factory_context{0};
std::atomic<uintptr_t> state_palette_instances[2]{};
StateAppend type_attachment_original = nullptr;
void* type_attachment_stub = nullptr;
using StateConstructor = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t);
StateConstructor state_constructor_original = nullptr;
void* state_constructor_stub = nullptr;
using RegistryIndex2 = uintptr_t (*)(uintptr_t, uintptr_t);
using RegistryIndex3 = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t);
RegistryIndex3 registry_namespace_original = nullptr;
RegistryIndex2 registry_identity_original = nullptr, registry_identity_copy_original = nullptr;
void* registry_namespace_stub = nullptr, *registry_identity_stub = nullptr, *registry_identity_copy_stub = nullptr;
std::atomic<unsigned> state_factory_hits{0}, state_constructor_hits{0};
std::atomic<unsigned> type_attachment_hits{0};
std::atomic<unsigned> registry_namespace_hits{0}, registry_identity_hits{0}, registry_identity_copy_hits{0};
std::atomic<unsigned> registry_identity_copy_bytes_logged{0};
std::atomic<uintptr_t> numeric_identity_types[3]{};
std::atomic<unsigned> numeric_identity_states{0};
using RegistrationVersionGate = bool (*)(uintptr_t, uintptr_t);
RegistrationVersionGate registration_version_original = nullptr;
void* registration_version_stub = nullptr;
std::atomic<unsigned> registration_version_hits{0};
using BlockStateSweep = void (*)(uintptr_t, uintptr_t);
BlockStateSweep block_state_sweep_original = nullptr;
void* block_state_sweep_stub = nullptr;
std::atomic<bool> custom_registration_attempted{false};
std::atomic<uintptr_t> custom_registered_type{0}, custom_default_state{0};
std::atomic<bool> construction_contracts_verified{false}, construction_requested{false};
std::atomic<uintptr_t> unregistered_type{0};
std::atomic<pid_t> construction_thread{0};
std::atomic<uintptr_t> state_engine_base{0};
std::atomic<unsigned> state_append_hits{0}, palette_append_hits{0};
std::atomic<unsigned> state_append_calls{0}, palette_append_calls{0};

uintptr_t palette_append_proxy(uintptr_t palette, uintptr_t state);

bool observed_cobblestone(uintptr_t object) {
    constexpr uintptr_t mask = 0x00ffffffffffffffULL;
    if (!object) return false;
    for (const auto& entry : cobblestone_constructor_objects) {
        const uintptr_t value = entry.load(std::memory_order_acquire);
        if (value && (value & mask) == (object & mask)) return true;
    }
    return false;
}

bool observed_state(const hybrid::SelfMemory& memory, uintptr_t state, uintptr_t& owner) {
    uintptr_t vptr = 0;
    return state && memory.read(state, &vptr, sizeof vptr) &&
        vptr == state_engine_base.load(std::memory_order_acquire) + 0x130f01c0 &&
        memory.read(state + 0x68, &owner, sizeof owner) &&
        (observed_cobblestone(owner) || owner == custom_registered_type.load(std::memory_order_acquire));
}

bool observed_name(const hybrid::SelfMemory& memory, uintptr_t address, char (&text)[97]);

bool observed_engine_block_state(const hybrid::SelfMemory& memory, uintptr_t state, uintptr_t& owner) {
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    uintptr_t state_vtable = 0, owner_vtable = 0, owning_state = 0;
    char name[97]{};
    return base && state && memory.read(state, &state_vtable, sizeof state_vtable) &&
        state_vtable == base + 0x130f01c0 && memory.read(state + 0x68, &owner, sizeof owner) && owner &&
        memory.read(owner, &owner_vtable, sizeof owner_vtable) && owner_vtable == base + 0x130d6190 &&
        observed_name(memory, owner + 0xd0, name) && std::strchr(name, ':') &&
        memory.read(owner + 0x200, &owning_state, sizeof owning_state) && owning_state;
}

bool observed_name(const hybrid::SelfMemory& memory, uintptr_t address, char (&text)[97]) {
    uintptr_t words[3]{};
    if (!memory.read(address, words, sizeof words)) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(words);
    const size_t size = bytes[0] & 1 ? words[1] : bytes[0] >> 1;
    if (size > 96 || (!(bytes[0] & 1) && size > 22)) return false;
    if (bytes[0] & 1) {
        if (size && !memory.read(words[2], text, size)) return false;
    } else std::memcpy(text, bytes + 1, size);
    for (size_t i = 0; i < size; ++i)
        if (text[i] < 0x20 || text[i] > 0x7e) return false;
    text[size] = 0;
    return true;
}

bool registration_version_proxy(uintptr_t lhs, uintptr_t rhs) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    if (base && (return_pc == base + 0x93c331c || return_pc == base + 0x93c3340) &&
            registration_version_hits.fetch_add(1, std::memory_order_relaxed) < 4) {
        hybrid::SelfMemory memory;
        uint16_t left[4]{}, right[4]{};
        uint8_t left_invalid = 0, right_invalid = 0;
        const bool valid = memory.read(lhs, left, sizeof left) && memory.read(rhs, right, sizeof right) &&
            memory.read(lhs + 0x18, &left_invalid, sizeof left_invalid) &&
            memory.read(rhs + 0x18, &right_invalid, sizeof right_invalid);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "registration_version_gate=entry;pid=%d;tid=%d;mono_ns=%" PRIu64
            ";return_elf_va=0x%" PRIxPTR ";lhs=0x%" PRIxPTR ";rhs_context=0x%" PRIxPTR
            ";read=%d;lhs_version=%u.%u.%u;rhs_version=%u.%u.%u;flags=%u,%u;invalid=%u,%u;behavior=forward_only",
            getpid(), gettid(), monotonic_ns(), return_pc - base, lhs, rhs, valid,
            left[0], left[1], left[2], right[0], right[1], right[2], left[3], right[3],
            left_invalid, right_invalid);
    }
    return registration_version_original(lhs, rhs);
}

void trace_registry_index(const char* stage, uintptr_t expected_return, uintptr_t return_pc,
                         std::atomic<unsigned>& counter, uintptr_t x0, uintptr_t x1,
                         uintptr_t x2, uintptr_t result) {
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    const bool caller_match = base && return_pc == base + expected_return;
    if (!caller_match) return;
    const unsigned hit = counter.fetch_add(1, std::memory_order_relaxed);
    if (hit >= 16) return;
    hybrid::SelfMemory memory;
    char key1[97]{}, key2[97]{};
    const bool key1_valid = caller_match && observed_name(memory, x1, key1);
    const bool key2_valid = caller_match && x2 && observed_name(memory, x2, key2);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_registry_index=%s;hit=%u;pid=%d;tid=%d;mono_ns=%" PRIu64
        ";x0=0x%" PRIxPTR ";x1=0x%" PRIxPTR ";x2=0x%" PRIxPTR
        ";key1_read=%d;key1=%s;key2_read=%d;key2=%s;result=0x%" PRIxPTR
        ";return_pc=0x%" PRIxPTR ";return_pc_elf_va=0x%" PRIxPTR
        ";expected_return_elf_va=0x%" PRIxPTR ";caller_match=1;engine_mutation=original_only",
        stage, hit + 1, getpid(), gettid(), monotonic_ns(), x0, x1, x2,
        key1_valid, key1_valid ? key1 : "unvalidated", key2_valid, key2_valid ? key2 : "unvalidated",
        result, return_pc, return_pc - base, expected_return);
}

uintptr_t registry_namespace_proxy(uintptr_t x0, uintptr_t x1, uintptr_t x2) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const RegistryIndex3 original = registry_namespace_original;
    if (!original) return 0;
    const uintptr_t result = original(x0, x1, x2);
    trace_registry_index("namespace_update", 0x93c3df0, return_pc, registry_namespace_hits, x0, x1, x2, result);
    return result;
}

uintptr_t registry_identity_proxy(uintptr_t x0, uintptr_t x1) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const RegistryIndex2 original = registry_identity_original;
    if (!original) return 0;
    const uintptr_t result = original(x0, x1);
    trace_registry_index("identity_lookup_or_insert", 0x93c3e84, return_pc, registry_identity_hits, x0, x1, 0, result);
    return result;
}

uintptr_t registry_identity_copy_proxy(uintptr_t x0, uintptr_t x1) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const RegistryIndex2 original = registry_identity_copy_original;
    if (!original) return 0;
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    const bool exact_caller = base && (return_pc == base + 0x93c3e94 || return_pc == base + 0x94d7820);
    const unsigned sample = exact_caller ? registry_identity_copy_bytes_logged.fetch_add(1, std::memory_order_relaxed) : 4;
    const bool capture = exact_caller && sample < 4;
    hybrid::SelfMemory memory;
    uint64_t destination_before[5]{}, source_before[5]{}, destination_after[5]{}, source_after[5]{};
    const bool destination_before_valid = capture && memory.read(x0, destination_before, sizeof destination_before);
    const bool source_before_valid = capture && memory.read(x1, source_before, sizeof source_before);
    char source_name_before[97]{};
    const bool source_name_before_valid = capture && observed_name(memory, x1 + 8, source_name_before);
    const uintptr_t result = original(x0, x1);
    const bool destination_after_valid = capture && memory.read(x0, destination_after, sizeof destination_after);
    const bool source_after_valid = capture && memory.read(x1, source_after, sizeof source_after);
    trace_registry_index("identity_value_copy", 0x93c3e94, return_pc, registry_identity_copy_hits, x0, x1, 0, result);
    if (capture) {
        char source_name[97]{}, destination_name[97]{};
        const bool keys_valid = source_name_before_valid && source_before_valid && source_after_valid && destination_after_valid &&
            observed_name(memory, x1 + 8, source_name) && observed_name(memory, x0 + 8, destination_name);
        uint64_t calculated_hash = source_name_before[0] ? UINT64_C(0xcbf29ce484222325) : 0;
        for (size_t i = 0; source_name_before[i]; ++i)
            calculated_hash = calculated_hash * UINT64_C(0x100000001b3) ^ static_cast<unsigned char>(source_name_before[i]);
        uintptr_t queue[32]{}, matched_node = 0, matched_type = 0, root_after = 0;
        uint64_t matched_hash = 0;
        char node_name[97]{};
        size_t head = 0, tail = 0;
        const uintptr_t registry = base + kRegistryGlobalElfVa;
        uintptr_t root = 0;
        if (keys_valid && memory.read(registry + 8, &root, sizeof root) && root) queue[tail++] = root;
        while (head < tail) {
            const uintptr_t node = queue[head++];
            uintptr_t links[2]{}, value = 0;
            uint64_t node_hash = 0;
            if (!memory.read(node, links, sizeof links) || !memory.read(node + 0x20, &node_hash, sizeof node_hash)) break;
            // The verified comparator orders hash first. Equal-hash names must match exactly.
            if (node_hash == calculated_hash && memory.read(node + 0x48, &value, sizeof value) && value &&
                    observed_name(memory, node + 0x28, node_name) &&
                    std::strcmp(node_name, source_name_before) == 0) {
                matched_node = node;
                matched_type = value;
                matched_hash = node_hash;
                break;
            }
            if (node_hash == calculated_hash) break;
            const uintptr_t link = links[node_hash < calculated_hash ? 1 : 0];
            bool seen = false;
            for (size_t i = 0; i < tail; ++i)
                if ((queue[i] & UINT64_C(0x00ffffffffffffff)) == (link & UINT64_C(0x00ffffffffffffff))) seen = true;
            if (link && !seen && tail < 32) queue[tail++] = link;
        }
        const bool root_stable = memory.read(registry + 8, &root_after, sizeof root_after) && root == root_after;
        const unsigned identity_slot = return_pc == base + 0x93c3e94 ? 1 :
            (std::strcmp(source_name_before, "minecraft:coarse_dirt") == 0 ? 2 : 0);
        const char* expected_name = identity_slot == 1 ? "minecraft:cobblestone" :
            (identity_slot == 2 ? "minecraft:coarse_dirt" : "minecraft:dirt");
        const uintptr_t expected_vtable = base + (identity_slot == 1 ? 0x130d6190 : 0x130c5c28);
        uintptr_t vtable = 0, primary = 0;
        uint16_t numeric_id = 0;
        uint64_t auxiliary_hash = 0;
        char type_name[97]{};
        const bool identity_valid = keys_valid && matched_type && root_stable &&
            std::strcmp(source_name_before, expected_name) == 0 &&
            memory.read(matched_type, &vtable, sizeof vtable) && vtable == expected_vtable &&
            observed_name(memory, matched_type + 0xd0, type_name) && std::strcmp(type_name, expected_name) == 0 &&
            memory.read(matched_type + 0x14e, &numeric_id, sizeof numeric_id) &&
            memory.read(matched_type + 0x200, &primary, sizeof primary) &&
            x0 >= 8 && memory.read(x0 - 8, &auxiliary_hash, sizeof auxiliary_hash) &&
            matched_hash == calculated_hash && auxiliary_hash == calculated_hash && destination_after[0] == calculated_hash;
        if (identity_valid) numeric_identity_types[identity_slot].store(matched_type, std::memory_order_release);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "numeric_identity=registration;pid=%d;tid=%d;mono_ns=%" PRIu64
            ";valid=%d;name=%s;type=0x%" PRIxPTR ";numeric14e=%u;identifier_hash=0x%" PRIx64
            ";auxiliary_hash=0x%" PRIx64 ";primary=0x%" PRIxPTR ";caller_elf_va=0x%" PRIxPTR
            ";lookup_depth=%zu;auxiliary_key_kind=identifier_hash_not_numeric;engine_mutation=original_only",
            getpid(), gettid(), monotonic_ns(), identity_valid, expected_name, matched_type, numeric_id,
            calculated_hash, auxiliary_hash, primary, return_pc - base, head);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "registration_key_copy=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
            ";read=%d;source_before=%s;source_after=%s;destination=%s;hash=0x%" PRIx64 ";fnv1_match=%d;hash_copied=%d"
            ";source_unchanged40=%d;destination_cache_reset=%d;node=0x%" PRIxPTR ";type=0x%" PRIxPTR
            ";node_name=%s;node_hash_match=%d;root_stable=%d;sample_bound=32;caller_guard_scope=vanilla;engine_mutation=original_only",
            getpid(), gettid(), monotonic_ns(), keys_valid, keys_valid ? source_name_before : "unvalidated",
            keys_valid ? source_name : "unvalidated",
            keys_valid ? destination_name : "unvalidated", source_after[0],
            keys_valid && calculated_hash == source_after[0],
            keys_valid && source_after[0] == destination_after[0],
            keys_valid && std::memcmp(source_before, source_after, sizeof source_before) == 0,
            keys_valid && destination_after[4] == 0, matched_node, matched_type,
            matched_node ? node_name : "unvalidated",
            matched_node && matched_hash == source_after[0] && std::strcmp(node_name, source_name_before) == 0, root_stable);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_identity_copy_bytes;sample=%u;caller_match=%d;dst_before_read=%d;dst_before=%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
            ";src_before_read=%d;src_before=%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
            ";dst_after_read=%d;dst_after=%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
            ";src_after_read=%d;src_after=%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64 ",%016" PRIx64
            ";dst=0x%" PRIxPTR ";src=0x%" PRIxPTR ";result=0x%" PRIxPTR ";pid=%d;tid=%d",
            sample + 1, exact_caller, destination_before_valid, destination_before[0], destination_before[1], destination_before[2], destination_before[3],
            source_before_valid, source_before[0], source_before[1], source_before[2], source_before[3],
            destination_after_valid, destination_after[0], destination_after[1], destination_after[2], destination_after[3],
            source_after_valid, source_after[0], source_after[1], source_after[2], source_after[3],
            x0, x1, result, getpid(), gettid());
    }
    return result;
}

void type_preparation_snapshot(const char* phase, uintptr_t type) {
    hybrid::SelfMemory memory;
    uintptr_t vptr = 0, material = 0, pairs[4]{}, primary = 0;
    uint32_t property_bits = 0;
    uint16_t numeric_id = 0;
    char name[97]{};
    const bool valid = memory.read(type, &vptr, sizeof vptr) &&
        vptr == state_engine_base.load(std::memory_order_acquire) + 0x130d6190 &&
        observed_name(memory, type + 0xd0, name) &&
        memory.read(type + 0x20, &material, sizeof material) &&
        memory.read(type + 0x14e, &numeric_id, sizeof numeric_id) &&
        memory.read(type + 0x1a4, &property_bits, sizeof property_bits) &&
        memory.read(type + 0x388, pairs, sizeof pairs) &&
        memory.read(type + 0x200, &primary, sizeof primary);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_type_preparation=%s;pid=%d;tid=%d;type=0x%" PRIxPTR
        ";read=%d;name=%s;material=0x%" PRIxPTR ";numeric14e=%u;bits1a4=%u"
        ";component388=0x%" PRIxPTR ";control390=0x%" PRIxPTR
        ";attachment398=0x%" PRIxPTR ";control3a0=0x%" PRIxPTR
        ";primary=0x%" PRIxPTR ";engine_mutation=original_only",
        phase, getpid(), gettid(), type, valid, valid ? name : "unvalidated", material,
        numeric_id, property_bits, pairs[0], pairs[1], pairs[2], pairs[3], primary);
}

void destroy_unregistered_type(uintptr_t type) {
    hybrid::SelfMemory memory;
    uintptr_t vptr = 0, destructor = 0;
    const bool valid = memory.read(type, &vptr, sizeof vptr) &&
        vptr == state_engine_base.load(std::memory_order_acquire) + 0x130d6190 &&
        memory.read(vptr + 8, &destructor, sizeof destructor) && engine_executable(destructor);
    if (valid) reinterpret_cast<void (*)(uintptr_t)>(destructor)(type);
    else unregistered_type.store(type, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "native_type_construction=cleanup;pid=%d;tid=%d;type=0x%" PRIxPTR
        ";engine_destructor_called=%d;retained=%d;registered=0", getpid(), gettid(), type, valid, !valid);
}

void construct_unregistered_type(uintptr_t numeric, uintptr_t material) {
    if (!construction_requested.exchange(false, std::memory_order_acq_rel)) return;
    if (!construction_contracts_verified.load(std::memory_order_acquire) || unregistered_type.load()) return;
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    // Engine string constructor's <=22-byte branch copies size+1 bytes into its 24-byte short representation.
    alignas(8) unsigned char name[24]{};
    __android_log_print(ANDROID_LOG_INFO, kTag, "native_type_construction=name_begin;pid=%d;tid=%d", getpid(), gettid());
    reinterpret_cast<void (*)(void*, const char*, size_t)>(base + 0x85380f0)(name, "hybridmcpe", 10);
    uintptr_t object = 0;
    try {
        object = reinterpret_cast<uintptr_t (*)(size_t)>(base + 0xe49677c)(0x3a8);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_type_construction=constructor_begin;pid=%d;tid=%d;type=0x%" PRIxPTR, getpid(), gettid(), object);
        cobblestone_constructor_original(object, reinterpret_cast<uintptr_t>(name), numeric, material);
        hybrid::SelfMemory memory;
        uintptr_t vptr = 0, actual_material = 0;
        char actual_name[97]{};
        const bool valid = memory.read(object, &vptr, sizeof vptr) && vptr == base + 0x130d6190 &&
            observed_name(memory, object + 0xd0, actual_name) &&
            memory.read(object + 0x20, &actual_material, sizeof actual_material) && actual_material == material;
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_type_construction=return;pid=%d;tid=%d;type=0x%" PRIxPTR
            ";read=%d;name=%s;numeric_reused_for_unregistered_test=0x%" PRIxPTR
            ";registered=0;vtable_created_by=engine_constructor", getpid(), gettid(), object,
            valid, valid ? actual_name : "unvalidated", numeric);
        // The engine's no-colon branch at 0x10107d68 prefixes the full identifier with "minecraft:".
        if (valid && std::strcmp(actual_name, "minecraft:hybridmcpe") == 0) {
            construction_thread.store(gettid(), std::memory_order_relaxed);
            unregistered_type.store(object, std::memory_order_release);
        } else destroy_unregistered_type(object);
    } catch (...) {
        // Retain any partially constructed allocation; its destructor contract is not valid after a throw.
        if (object) unregistered_type.store(object, std::memory_order_release);
        construction_contracts_verified.store(false, std::memory_order_release);
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "native_type_construction=exception;pid=%d;retained=0x%" PRIxPTR ";registration=disabled", getpid(), object);
    }
}

struct alignas(8) EngineNameKey {
    uint64_t hash;
    unsigned char string[24];
};
struct TreeInsertResult { uintptr_t node; uintptr_t inserted; };
struct EngineRegistryGuard {
    uintptr_t mutex;
    uintptr_t shared_state;
    EngineRegistryGuard() = default;
    EngineRegistryGuard(const EngineRegistryGuard&) = delete;
    ~EngineRegistryGuard() noexcept {}
};
using EngineStringConstructor = void (*)(void*, const char*, size_t);
using EngineTypeConstructor = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using EngineAllocator = uintptr_t (*)(size_t);
using EngineMaterialGetter = uintptr_t (*)(uint32_t);
using EngineWeakLock = uintptr_t (*)(uintptr_t);
using EngineTypeAttachment = uintptr_t (*)(uintptr_t, void*);
using EngineNamespaceUpdate = uintptr_t (*)(uintptr_t, void*, void*);
using EngineNameKeyConstructor = void (*)(void*, void*);
using EngineNameHash = uintptr_t (*)(void*);
using EngineTreeInsert = TreeInsertResult (*)(uintptr_t, void*, const char*, uintptr_t*, uint8_t*);
using EngineIdentityInsert = uintptr_t (*)(uintptr_t, uintptr_t*);
using EngineStringMoveAssign = uintptr_t (*)(void*, void*);
using EngineTypeFlag = void (*)(uintptr_t, uint32_t);
using EngineGuardRelease = void (*)(EngineRegistryGuard*);

struct RegistryScan {
    size_t count = 0;
    uint16_t max_numeric = 0;
    bool duplicate_name = false;
    bool duplicate_hash = false;
};

bool scan_block_registry(uintptr_t registry, uintptr_t base, uint64_t wanted_hash, RegistryScan& out) {
    hybrid::SelfMemory memory;
    uintptr_t root = 0;
    if (!memory.read(registry + 8, &root, sizeof root) || !root) return false;
    std::array<uintptr_t, 16384> pending{};
    size_t depth = 1;
    pending[0] = root;
    const uintptr_t last_engine_data = base + kRegistryGlobalElfVa;
    while (depth) {
        const uintptr_t node = pending[--depth];
        if (!node || ++out.count > 8192) return false;
        uintptr_t children[2]{}, type = 0, vtable = 0;
        uint64_t hash = 0;
        char name[97]{};
        if (!memory.read(node, children, sizeof children) ||
                !memory.read(node + 0x20, &hash, sizeof hash) ||
                !observed_name(memory, node + 0x28, name) ||
                !memory.read(node + 0x48, &type, sizeof type) || !type ||
                !memory.read(type, &vtable, sizeof vtable) ||
                vtable < base + 0x10000000 || vtable >= last_engine_data) return false;
        uint16_t numeric = 0;
        if (!memory.read(type + 0x14e, &numeric, sizeof numeric)) return false;
        if (numeric > out.max_numeric) out.max_numeric = numeric;
        out.duplicate_name |= std::strcmp(name, "hybridmcpe:test_block") == 0;
        out.duplicate_hash |= hash == wanted_hash;
        for (uintptr_t child : children) {
            if (!child) continue;
            if (depth == pending.size()) return false;
            pending[depth++] = child;
        }
    }
    return out.count >= 100;
}

void native_block_registration_at_state_sweep(uintptr_t registry, bool after_sweep) {
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    const uintptr_t required[]{0xe49677c, 0x85380f0, 0x100fb840, 0x1093fdd8, 0xe4a215c,
        0x1010bb6c, 0x94f66e0, 0xe4c31a0, 0xe4c2a84, 0x94f6850,
        0x94f6a0c, 0xe4c2b18, 0x103e2784, 0x103e839c, 0x1010c784};
    for (uintptr_t offset : required) {
        if (!engine_executable(base + offset)) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=profile_gate;bad_elf_va=0x%" PRIxPTR ";pid=%d",
                offset, getpid());
            return;
        }
    }
    const uintptr_t expected_registry = base + kRegistryGlobalElfVa;
    hybrid::SelfMemory memory;
    const uintptr_t context = state_factory_context.load(std::memory_order_acquire);
    uintptr_t palettes[2]{};
    bool palettes_valid = true;
    if (after_sweep) {
        uintptr_t vtables[2]{};
        for (size_t i = 0; i < 2; ++i) {
            palettes[i] = state_palette_instances[i].load(std::memory_order_acquire);
            palettes_valid &= palettes[i] && memory.read(palettes[i], &vtables[i], sizeof vtables[i]) &&
                vtables[i] == base + 0x13060690;
        }
        palettes_valid &= palettes[0] != palettes[1];
    }
    const bool contracts = construction_contracts_verified.load(std::memory_order_acquire);
    const unsigned missing = (!base ? 1u : 0u) | (!context ? 2u : 0u) |
        (!contracts ? 32u : 0u) |
        (!cobblestone_constructor_original ? 64u : 0u) | (!type_attachment_original ? 128u : 0u) |
        (!state_factory_original ? 256u : 0u) | (!palette_append_original ? 512u : 0u) |
        (registry != expected_registry || !registry ? 1024u : 0u) |
        (after_sweep && !palettes_valid ? 2048u : 0u);
    if (missing) {
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "native_block_registration=blocked;stage=sweep_prerequisites;phase=%s;registry=0x%" PRIxPTR
            ";expected=0x%" PRIxPTR ";missing=0x%x;base=0x%" PRIxPTR ";context=0x%" PRIxPTR
            ";palettes=0x%" PRIxPTR ",0x%" PRIxPTR
            ";contracts=%d;hooks=%d,%d,%d,%d;profile=%s;pid=%d;tid=%d",
            after_sweep ? "after" : "before", registry, expected_registry, missing, base, context,
            palettes[0], palettes[1], contracts,
            cobblestone_constructor_original != nullptr, type_attachment_original != nullptr,
            state_factory_original != nullptr, palette_append_original != nullptr,
            kProfile, getpid(), gettid());
        return;
    }
    if (custom_registration_attempted.exchange(true, std::memory_order_acq_rel)) return;

    alignas(8) unsigned char id_string[24]{};
    alignas(8) EngineNameKey lookup_key{}, owned_key{}, identity_key{};
    constexpr char kIdentifier[] = "hybridmcpe:test_block";
    auto string_ctor = reinterpret_cast<EngineStringConstructor>(base + 0x85380f0);
    auto key_ctor = reinterpret_cast<EngineNameKeyConstructor>(base + 0xe4c31a0);
    auto key_hash = reinterpret_cast<EngineNameHash>(base + 0xe4c2a84);
    string_ctor(id_string, kIdentifier, sizeof(kIdentifier) - 1);
    key_ctor(&lookup_key, id_string);
    key_ctor(&owned_key, id_string);
    key_ctor(&identity_key, id_string);
    const uintptr_t identity_hash = key_hash(&identity_key);
    RegistryScan scan{};

    const auto acquire = reinterpret_cast<EngineRegistryGuard (*)(uintptr_t)>(base + 0x103e2784);
    EngineRegistryGuard guard = acquire(registry);
    bool guard_live = true;
    uintptr_t type = 0;
    bool type_owned_by_registry = false;
    auto release_guard = [&]() {
        if (!guard_live) return;
        reinterpret_cast<EngineGuardRelease>(base + 0x103e839c)(&guard);
        guard_live = false;
    };

    try {
        if (!scan_block_registry(registry, base, identity_hash, scan) ||
                scan.duplicate_name || scan.duplicate_hash || scan.max_numeric >= 65534) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=uniqueness_scan;nodes=%zu;duplicate_name=%d"
                ";duplicate_hash=%d;max_numeric=%u;pid=%d;tid=%d",
                scan.count, scan.duplicate_name, scan.duplicate_hash, scan.max_numeric, getpid(), gettid());
            release_guard();
            return;
        }
        const uint16_t numeric = static_cast<uint16_t>(scan.max_numeric + 1);
        const uintptr_t material = reinterpret_cast<EngineMaterialGetter>(base + 0x1093fdd8)(23);
        if (!material) {
            release_guard();
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=material_lookup;numeric=%u;pid=%d", numeric, getpid());
            return;
        }
        type = reinterpret_cast<EngineAllocator>(base + 0xe49677c)(0x3a8);
        if (!type) {
            release_guard();
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=allocation;pid=%d", getpid());
            return;
        }
        reinterpret_cast<EngineTypeConstructor>(base + kCobblestoneConstructorElfVa)(
            type, reinterpret_cast<uintptr_t>(id_string), numeric, material);
        uintptr_t vtable = 0, actual_material = 0;
        uint16_t actual_numeric = 0;
        char actual_name[97]{};
        const bool constructed = memory.read(type, &vtable, sizeof vtable) &&
            vtable == base + 0x130d6190 && observed_name(memory, type + 0xd0, actual_name) &&
            std::strcmp(actual_name, kIdentifier) == 0 &&
            memory.read(type + 0x20, &actual_material, sizeof actual_material) && actual_material == material &&
            memory.read(type + 0x14e, &actual_numeric, sizeof actual_numeric) && actual_numeric == numeric;
        if (!constructed) {
            release_guard();
            destroy_unregistered_type(type);
            type = 0;
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=type_validation;requested=%s;numeric=%u;pid=%d;tid=%d",
                kIdentifier, numeric, getpid(), gettid());
            return;
        }

        uintptr_t attachment[2]{};
        uintptr_t weak_state = 0, strong_object = 0;
        if (memory.read(registry + 0x138, &strong_object, sizeof strong_object) &&
                memory.read(registry + 0x140, &weak_state, sizeof weak_state) && weak_state) {
            attachment[1] = reinterpret_cast<EngineWeakLock>(base + 0xe4a215c)(weak_state);
            if (attachment[1]) attachment[0] = strong_object;
        }
        const uintptr_t expected_attachment[2]{attachment[0], attachment[1]};
        type_attachment_original(type, reinterpret_cast<uintptr_t>(attachment));
        uintptr_t attached[2]{};
        if (!memory.read(type + 0x398, attached, sizeof attached) ||
                attached[0] != expected_attachment[0] || attached[1] != expected_attachment[1] ||
                attachment[0] || attachment[1]) {
            release_guard();
            destroy_unregistered_type(type);
            type = 0;
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=attachment_validation;attached0=0x%" PRIxPTR
                ";attached1=0x%" PRIxPTR ";expected0=0x%" PRIxPTR ";expected1=0x%" PRIxPTR
                ";pid=%d;tid=%d", attached[0], attached[1], expected_attachment[0], expected_attachment[1],
                getpid(), gettid());
            return;
        }
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_block_registration=constructed;identifier=%s;type=0x%" PRIxPTR
            ";numeric=%u;registry=0x%" PRIxPTR ";nodes=%zu;previous_max=%u;hash=0x%" PRIxPTR
            ";attachment=validated;phase=%s_vanilla_block_state_sweep;pid=%d;tid=%d",
            kIdentifier, type, numeric, registry, scan.count, scan.max_numeric, identity_hash,
            after_sweep ? "after" : "before", getpid(), gettid());

        reinterpret_cast<EngineNamespaceUpdate>(base + 0x94f66e0)(registry + 0xe0, id_string, id_string);
        uintptr_t owned_key_pointer = reinterpret_cast<uintptr_t>(&owned_key);
        uint8_t insert_option = 0;
        const auto insertion = reinterpret_cast<EngineTreeInsert>(base + 0x94f6850)(
            registry, &lookup_key, "HybridMCPE experimental block registration", &owned_key_pointer, &insert_option);
        uintptr_t existing_value = 1;
        const bool fresh_node = insertion.inserted == 1 && insertion.node &&
            memory.read(insertion.node + 0x48, &existing_value, sizeof existing_value) && !existing_value;
        if (!fresh_node) {
            release_guard();
            destroy_unregistered_type(type);
            type = 0;
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=blocked;stage=primary_registry_insert;node=0x%" PRIxPTR
                ";inserted=%" PRIuPTR ";node_value=0x%" PRIxPTR ";pid=%d;tid=%d",
                insertion.node, insertion.inserted, existing_value, getpid(), gettid());
            return;
        }
        // This is the same value-ownership handoff observed in the verified vanilla insertion path.
        *reinterpret_cast<uintptr_t*>(insertion.node + 0x48) = type;
        type_owned_by_registry = true;
        custom_registered_type.store(type, std::memory_order_release);
        uintptr_t registered_value = 0;
        char registered_name[97]{};
        const bool primary_valid = memory.read(insertion.node + 0x48, &registered_value, sizeof registered_value) &&
            registered_value == type && observed_name(memory, insertion.node + 0x28, registered_name) &&
            std::strcmp(registered_name, kIdentifier) == 0;

        uintptr_t mutable_identity_hash = identity_hash;
        using IdentityInsert = uintptr_t* (*)(uintptr_t, uintptr_t*);
        uintptr_t* identity_entry = reinterpret_cast<IdentityInsert>(base + 0x94f6a0c)(registry + 0xf8,
            &mutable_identity_hash);
        bool identity_valid = false;
        if (identity_entry) {
            uintptr_t destination[3]{};
            identity_valid = memory.read(reinterpret_cast<uintptr_t>(identity_entry) + 0x10,
                destination, sizeof destination);
            if (identity_valid) {
                const auto move_assign = registry_identity_copy_original ?
                    reinterpret_cast<EngineStringMoveAssign>(registry_identity_copy_original) :
                    reinterpret_cast<EngineStringMoveAssign>(base + 0xe4c2b18);
                // The vanilla helper consumes the full hash-plus-string NameKey.
                move_assign(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(identity_entry) + 0x10),
                    &identity_key);
                char indexed_name[97]{};
                identity_valid = observed_name(memory, reinterpret_cast<uintptr_t>(identity_entry) + 0x18, indexed_name) &&
                    std::strcmp(indexed_name, kIdentifier) == 0;
            }
        }
        release_guard();
        reinterpret_cast<EngineTypeFlag>(base + 0x1010c784)(type, 1);
        __android_log_print(primary_valid && identity_valid ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
            "native_block_registration=primary_committed;identifier=%s;type=0x%" PRIxPTR
            ";node=0x%" PRIxPTR ";numeric=%u;primary_valid=%d;identity_index_valid=%d"
            ";owner=engine_registry;state_sweep=%s;palette=%s;pid=%d;tid=%d",
            kIdentifier, type, insertion.node, numeric, primary_valid, identity_valid,
            after_sweep ? "already_passed" : "next", after_sweep ? "explicit_engine_append_pending" : "engine_owned",
            getpid(), gettid());
        if (!primary_valid) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=partial;stage=primary_readback;type_retained=1;pid=%d", getpid());
        }
        const uintptr_t state = state_factory_original(type, context);
        uintptr_t state_vptr = 0, owner = 0, primary = 0;
        const bool state_valid = state && memory.read(state, &state_vptr, sizeof state_vptr) &&
            state_vptr == base + 0x130f01c0 && memory.read(state + 0x68, &owner, sizeof owner) &&
            owner == type && memory.read(type + 0x200, &primary, sizeof primary) && primary == state;
        if (!state_valid) {
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "native_block_registration=partial;stage=post_sweep_state_factory;type=0x%" PRIxPTR
                ";state=0x%" PRIxPTR ";owner=0x%" PRIxPTR ";primary=0x%" PRIxPTR ";pid=%d;tid=%d",
                type, state, owner, primary, getpid(), gettid());
            return;
        }
        custom_default_state.store(state, std::memory_order_release);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_block_registration=state_ready;identifier=%s;type=0x%" PRIxPTR
            ";state=0x%" PRIxPTR ";owner=validated;palette=%s;runtime_id=unverified;pid=%d;tid=%d",
            kIdentifier, type, state, after_sweep ? "explicit_append_pending" : "engine_sweep_pending",
            getpid(), gettid());
    } catch (...) {
        release_guard();
        if (type && !type_owned_by_registry) destroy_unregistered_type(type);
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "native_block_registration=exception;type=0x%" PRIxPTR ";owned=%d;pid=%d;tid=%d",
            type, type_owned_by_registry, getpid(), gettid());
    }
}

void block_state_sweep_proxy(uintptr_t registry, uintptr_t flags) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    const BlockStateSweep original = block_state_sweep_original;
    if (original) {
        if (base && registry && !custom_registration_attempted.load(std::memory_order_acquire))
            native_block_registration_at_state_sweep(registry, false);
        original(registry, flags);
        if (base && registry && !custom_registration_attempted.load(std::memory_order_acquire) &&
                state_factory_context.load(std::memory_order_acquire)) {
            native_block_registration_at_state_sweep(registry, true);
            const uintptr_t state = custom_default_state.load(std::memory_order_acquire);
            if (state) {
                hybrid::SelfMemory memory;
                bool complete = true;
                for (size_t i = 0; i < 2; ++i) {
                    const uintptr_t palette = state_palette_instances[i].load(std::memory_order_acquire);
                    uintptr_t before[2]{}, after[2]{}, last = 0;
                    const bool before_valid = palette && memory.read(palette + 0x48, before, sizeof before) &&
                        before[1] >= before[0] && (before[1] - before[0]) % 8 == 0;
                    if (!before_valid) {
                        complete = false;
                        continue;
                    }
                    palette_append_proxy(palette, state);
                    const bool after_valid = memory.read(palette + 0x48, after, sizeof after) &&
                        after[1] >= after[0] + 8 && (after[1] - after[0]) % 8 == 0 &&
                        memory.read(after[1] - 8, &last, sizeof last);
                    const bool appended = after_valid && last == state &&
                        (after[1] - after[0]) == (before[1] - before[0]) + 8;
                    complete &= appended;
                    __android_log_print(appended ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
                        "native_block_registration=palette_append_after_sweep;palette=%zu;state=0x%" PRIxPTR
                        ";appended=%d;count_before=%zu;count_after=%zu;runtime_id=unverified;pid=%d;tid=%d",
                        i, state, appended, static_cast<size_t>((before[1] - before[0]) / 8),
                        after_valid ? static_cast<size_t>((after[1] - after[0]) / 8) : 0, getpid(), gettid());
                }
                __android_log_print(complete ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
                    "native_block_registration=palette_integration;state=0x%" PRIxPTR
                    ";complete=%d;runtime_id=unverified;pid=%d;tid=%d",
                    state, complete, getpid(), gettid());
            }
        }
    }
    else __android_log_print(ANDROID_LOG_ERROR, kTag,
        "block_state_sweep=blocked;reason=original_missing;registry=0x%" PRIxPTR ";pid=%d", registry, getpid());
}

uintptr_t type_attachment_proxy(uintptr_t type, uintptr_t source) {
    SHADOWHOOK_STACK_SCOPE();
    const bool selected = observed_cobblestone(type);
    hybrid::SelfMemory memory;
    uintptr_t before[2]{}, after[2]{}, destination[2]{};
    const bool before_read = selected && memory.read(source, before, sizeof before);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t result = type_attachment_original(type, source);
    if (!selected || type_attachment_hits.fetch_add(1, std::memory_order_relaxed) >= 8) return result;
    const bool valid = before_read && memory.read(source, after, sizeof after) &&
        memory.read(type + 0x398, destination, sizeof destination);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_type_attachment=observed;pid=%d;tid=%d;type=0x%" PRIxPTR
        ";caller=0x%" PRIxPTR ";read=%d;source0=0x%" PRIxPTR ";source1=0x%" PRIxPTR
        ";source_cleared=%d;destination_matches=%d;engine_mutation=original_only",
        getpid(), gettid(), type, caller, valid, before[0], before[1],
        valid && !after[0] && !after[1], valid && before[0] == destination[0] && before[1] == destination[1]);
    type_preparation_snapshot("after_attachment", type);
    return result;
}

// shortcut: GP arguments verified at the vanilla callsites only; no mod-facing callable factory ABI.
uintptr_t state_constructor_proxy(uintptr_t state, uintptr_t ordinal, uintptr_t type) {
    SHADOWHOOK_STACK_SCOPE();
    const bool selected = observed_cobblestone(type);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t result = state_constructor_original(state, ordinal, type);
    if (!selected || state_constructor_hits.fetch_add(1, std::memory_order_relaxed) >= 8) return result;
    hybrid::SelfMemory memory;
    uintptr_t owner = 0;
    const bool valid = observed_state(memory, state, owner) && owner == type;
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_state_constructor=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
        ";state=0x%" PRIxPTR ";type=0x%" PRIxPTR ";ordinal=%" PRIuPTR
        ";caller=0x%" PRIxPTR ";owner_matches=%d;engine_mutation=original_only",
        getpid(), gettid(), monotonic_ns(), state, type, ordinal, caller, valid);
    return result;
}

uintptr_t state_factory_proxy(uintptr_t type, uintptr_t context) {
    SHADOWHOOK_STACK_SCOPE();
    hybrid::SelfMemory memory;
    const uintptr_t custom_type = custom_registered_type.load(std::memory_order_acquire);
    const bool custom_selected = custom_type && type == custom_type;
    const bool selected = observed_cobblestone(type) || custom_selected;
    if (selected && state_factory_hits.load(std::memory_order_relaxed) < 8)
        type_preparation_snapshot("before_state_factory", type);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t result = state_factory_original(type, context);
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    if (base && caller == base + 0x103e3028 && observed_cobblestone(type))
        state_factory_context.store(context, std::memory_order_release);
    if (custom_selected && base && caller == base + 0x103e3028) {
        uintptr_t state = 0, vtable = 0, owner = 0;
        uint16_t numeric = 0;
        char name[97]{};
        const bool valid = memory.read(type + 0x200, &state, sizeof state) && state &&
            memory.read(state, &vtable, sizeof vtable) && vtable == base + 0x130f01c0 &&
            memory.read(state + 0x68, &owner, sizeof owner) && owner == type &&
            memory.read(type + 0x14e, &numeric, sizeof numeric) &&
            observed_name(memory, type + 0xd0, name) && std::strcmp(name, "hybridmcpe:test_block") == 0;
        if (valid) custom_default_state.store(state, std::memory_order_release);
        __android_log_print(valid ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
            "native_block_registration=default_state;identifier=%s;type=0x%" PRIxPTR
            ";state=0x%" PRIxPTR ";owner=0x%" PRIxPTR ";numeric=%u;valid=%d"
            ";caller_elf_va=0x103e3028;pid=%d;tid=%d",
            valid ? name : "unvalidated", type, state, owner, numeric, valid, getpid(), gettid());
    }
    // Only sample the verified guarded registry sweep, never an unrelated factory caller.
    if (base && caller == base + 0x103e3028) {
        for (unsigned slot = 0; slot < 3; ++slot) {
            if (type != numeric_identity_types[slot].load(std::memory_order_acquire) || !type ||
                    (numeric_identity_states.fetch_or(1U << slot, std::memory_order_relaxed) & (1U << slot))) continue;
            uint16_t numeric_id = 0;
            uintptr_t state = 0, owner = 0, vtable = 0;
            const bool valid = memory.read(type + 0x14e, &numeric_id, sizeof numeric_id) &&
                memory.read(type + 0x200, &state, sizeof state) && state &&
                memory.read(state, &vtable, sizeof vtable) && vtable == base + 0x130f01c0 &&
                memory.read(state + 0x68, &owner, sizeof owner) && owner == type;
            __android_log_print(ANDROID_LOG_INFO, kTag,
                "numeric_identity=state;pid=%d;tid=%d;mono_ns=%" PRIu64
                ";valid=%d;name=%s;type=0x%" PRIxPTR ";numeric14e=%u;state=0x%" PRIxPTR
                ";owner=0x%" PRIxPTR ";caller_elf_va=0x103e3028;runtime_id=not_inferred;engine_mutation=original_only",
                getpid(), gettid(), monotonic_ns(), valid, slot == 1 ? "minecraft:cobblestone" :
                    (slot == 2 ? "minecraft:coarse_dirt" : "minecraft:dirt"),
                type, numeric_id, state, owner);
        }
    }
    if (!selected || state_factory_hits.fetch_add(1, std::memory_order_relaxed) >= 8) return result;
    uintptr_t primary = 0, owner = 0, bounds[2]{}, first = 0;
    const bool valid = memory.read(type + 0x200, &primary, sizeof primary) &&
        observed_state(memory, primary, owner) && owner == type &&
        memory.read(type + 0x1e8, bounds, sizeof bounds) && bounds[0] &&
        bounds[1] >= bounds[0] + 8 && (bounds[1] - bounds[0]) % 8 == 0 &&
        (bounds[1] - bounds[0]) / 8 <= 4096 && memory.read(bounds[0], &first, sizeof first);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_state_factory=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
        ";type=0x%" PRIxPTR ";context_raw=0x%" PRIxPTR ";caller=0x%" PRIxPTR
        ";state=0x%" PRIxPTR ";owner_matches=%d;primary_is_first=%d;engine_mutation=original_only",
        getpid(), gettid(), monotonic_ns(), type, context, caller, primary, valid, valid && first == primary);
    const uintptr_t candidate = construction_contracts_verified.load(std::memory_order_acquire) ?
        unregistered_type.exchange(0, std::memory_order_acq_rel) : 0;
    if (candidate && valid && construction_thread.load(std::memory_order_relaxed) == gettid()) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_type_state=population_begin;pid=%d;tid=%d;type=0x%" PRIxPTR, getpid(), gettid(), candidate);
        state_factory_original(candidate, context);
        uintptr_t state = 0, state_vptr = 0, candidate_owner = 0;
        const bool state_valid = memory.read(candidate + 0x200, &state, sizeof state) && state &&
            memory.read(state, &state_vptr, sizeof state_vptr) &&
            state_vptr == state_engine_base.load(std::memory_order_acquire) + 0x130f01c0 &&
            memory.read(state + 0x68, &candidate_owner, sizeof candidate_owner) && candidate_owner == candidate;
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "native_type_state=observed;pid=%d;tid=%d;type=0x%" PRIxPTR ";state=0x%" PRIxPTR
            ";valid=%d;context_raw=0x%" PRIxPTR ";registered=0;palette_inserted=0",
            getpid(), gettid(), candidate, state, state_valid, context);
        destroy_unregistered_type(candidate);
    } else if (candidate) {
        unregistered_type.store(candidate, std::memory_order_release);
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "native_type_state=blocked;reason=vanilla_state_or_thread_mismatch;pid=%d;tid=%d;retained=1", getpid(), gettid());
    }
    return result;
}

uintptr_t state_append_proxy(uintptr_t type, uintptr_t source) {
    SHADOWHOOK_STACK_SCOPE();
    hybrid::SelfMemory memory;
    uintptr_t state = 0, owner = 0;
    const bool selected = observed_cobblestone(type) && memory.read(source, &state, sizeof state) &&
        observed_state(memory, state, owner) && owner == type;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    if (state_append_calls.fetch_add(1, std::memory_order_relaxed) < 2)
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_state_append=entry;pid=%d;tid=%d;selected=%d;caller=0x%" PRIxPTR,
            getpid(), gettid(), selected, caller);
    const uintptr_t result = state_append_original(type, source);
    if (!selected || state_append_hits.fetch_add(1, std::memory_order_relaxed) >= 8) return result;
    uintptr_t moved = 0, bounds[2]{}, first = 0, primary = 0;
    const bool read = memory.read(source, &moved, sizeof moved) &&
        memory.read(type + 0x1e8, bounds, sizeof bounds) && bounds[0] &&
        bounds[1] >= bounds[0] && (bounds[1] - bounds[0]) % 8 == 0 &&
        (bounds[1] - bounds[0]) / 8 <= 4096 &&
        memory.read(bounds[0], &first, sizeof first) &&
        memory.read(type + 0x200, &primary, sizeof primary);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_state_append=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
        ";type=0x%" PRIxPTR ";state=0x%" PRIxPTR ";owner=0x%" PRIxPTR
        ";caller=0x%" PRIxPTR ";read=%d;source_cleared=%d;state_count=%zu"
        ";first=0x%" PRIxPTR ";primary=0x%" PRIxPTR ";primary_is_first=%d;engine_mutation=original_only",
        getpid(), gettid(), monotonic_ns(), type, state, owner, caller, read, read && !moved,
        read ? static_cast<size_t>((bounds[1] - bounds[0]) / 8) : 0,
        first, primary, read && primary == first);
    return result;
}

uintptr_t palette_append_proxy(uintptr_t palette, uintptr_t state) {
    SHADOWHOOK_STACK_SCOPE();
    hybrid::SelfMemory memory;
    uintptr_t owner = 0, vptr = 0, dispatch = 0, before[2]{};
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    // The live state uses BlockHashPalette; the base palette's sequential-ID path is different.
    const bool selected = observed_engine_block_state(memory, state, owner) &&
        memory.read(palette, &vptr, sizeof vptr) && vptr == base + 0x13060690 &&
        memory.read(vptr + 24, &dispatch, sizeof dispatch) &&
        dispatch == base + 0xfb8ba5c && memory.read(palette + 0x48, before, sizeof before) &&
        before[1] >= before[0] && (before[1] - before[0]) % 8 == 0 &&
        (before[1] - before[0]) / 8 < 1048576;
    if (selected) {
        for (auto& slot : state_palette_instances) {
            uintptr_t known = slot.load(std::memory_order_acquire);
            if (known == palette) break;
            if (!known && slot.compare_exchange_strong(known, palette, std::memory_order_acq_rel)) break;
        }
    }
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const bool custom_state = state == custom_default_state.load(std::memory_order_acquire);
    if (palette_append_calls.fetch_add(1, std::memory_order_relaxed) < 2)
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_palette_append=entry;pid=%d;tid=%d;selected=%d;vtable=0x%" PRIxPTR
            ";owner=0x%" PRIxPTR ";caller=0x%" PRIxPTR,
            getpid(), gettid(), selected, vptr, owner, caller);
    const uintptr_t result = palette_append_original(palette, state);
    if (!selected || palette_append_hits.fetch_add(1, std::memory_order_relaxed) >= 8) return result;
    uintptr_t after[2]{}, last = 0, primary = 0;
    uint32_t ordinal = 0;
    unsigned char assigned = 0;
    const bool read = memory.read(palette + 0x48, after, sizeof after) &&
        after[1] >= after[0] + 8 && (after[1] - after[0]) % 8 == 0 &&
        (after[1] - after[0]) / 8 <= 1048576 &&
        memory.read(after[1] - 8, &last, sizeof last) &&
        memory.read(owner + 0x200, &primary, sizeof primary) &&
        memory.read(state + 0x11c, &ordinal, sizeof ordinal) &&
        memory.read(state + 0x12a, &assigned, sizeof assigned);
    const size_t index = static_cast<size_t>((before[1] - before[0]) / 8);
    uintptr_t node = 0, mapped_index = UINTPTR_MAX;
    bool hash_found = false;
    if (read && memory.read(palette + 0x70, &node, sizeof node)) {
        for (unsigned step = 0; node && step < 64; ++step) {
            uint32_t key = 0;
            if (!memory.read(node + 0x20, &key, sizeof key)) break;
            if (key == ordinal) {
                hash_found = memory.read(node + 0x28, &mapped_index, sizeof mapped_index);
                break;
            }
            if (!memory.read(node + (ordinal < key ? 0 : 8), &node, sizeof node)) break;
        }
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "block_palette_append=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
        ";palette=0x%" PRIxPTR ";vtable=0x%" PRIxPTR ";state=0x%" PRIxPTR
        ";type=0x%" PRIxPTR ";custom_state=%d;caller=0x%" PRIxPTR ";read=%d;index_before=%zu;palette_key11c=%u"
        ";assigned12a=%u;last_matches=%d;primary_matches=%d;count_increased=%d"
        ";hash_found=%d;hash_index=%zu;hash_index_matches=%d;engine_mutation=original_only",
        getpid(), gettid(), monotonic_ns(), palette, vptr, state, owner, custom_state, caller, read, index,
        ordinal, assigned, read && last == state, read && primary == state,
        read && (after[1] - after[0]) / 8 == index + 1, hash_found,
        static_cast<size_t>(mapped_index), hash_found && mapped_index == index);
    return result;
}

uintptr_t candidate_proxy(uintptr_t x0, uintptr_t x1, uintptr_t x2, uintptr_t x3) {
    SHADOWHOOK_STACK_SCOPE();
    const unsigned hit = candidate_hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t expected_return_pc = candidate_expected_return_pc.load(std::memory_order_relaxed);
    const uintptr_t registry = candidate_registry_identity.load(std::memory_order_relaxed);
    const unsigned registry_match_mask =
        (x0 == registry ? 1U : 0U) | (x1 == registry ? 2U : 0U) |
        (x2 == registry ? 4U : 0U) | (x3 == registry ? 8U : 0U);
    if (hit < 32) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_candidate=entry;hit=%u;mono_ns=%" PRIu64 ";x0=0x%" PRIxPTR
            ";x1=0x%" PRIxPTR ";x2=0x%" PRIxPTR ";x3=0x%" PRIxPTR
            ";return_pc=0x%" PRIxPTR ";expected_return_pc=0x%" PRIxPTR
            ";known_callsite_match=%d;registry_global=0x%" PRIxPTR
            ";registry_arg_match_mask=0x%x;args_dereferenced=0;engine_phase=unknown;pid=%d;tid=%d;abi=provisional_gp4",
            hit + 1, monotonic_ns(), x0, x1, x2, x3, return_pc, expected_return_pc,
            return_pc == expected_return_pc, registry, registry_match_mask, getpid(), gettid());
    }
    const CandidateFunction original = candidate_original;
    if (!original) return 0;
    const uintptr_t result = original(x0, x1, x2, x3);
    if (hit < 32) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_candidate=return;hit=%u;mono_ns=%" PRIu64 ";x0_raw=0x%" PRIxPTR
            ";pid=%d;tid=%d;result_semantics=unknown",
            hit + 1, monotonic_ns(), result, getpid(), gettid());
    }
    return result;
}

void cobblestone_constructor_proxy(uintptr_t self, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3) {
    SHADOWHOOK_STACK_SCOPE();
    const unsigned hit = cobblestone_constructor_hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t return_pc = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    const uintptr_t base = state_engine_base.load(std::memory_order_acquire);
    if (hit < 8 && base && return_pc == base + 0x93c3cc0) {
        hybrid::SelfMemory memory;
        char input[97]{}, fixed_source[97]{};
        const bool valid = observed_name(memory, arg1, input) &&
            observed_name(memory, base + 0x134f1c38, fixed_source);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "vanilla_construction_site=observed;pid=%d;tid=%d;mono_ns=%" PRIu64
            ";caller_elf_va=0x93c3cbc;type=0x%" PRIxPTR ";identifier_read=%d;input=%s;fixed_source=%s"
            ";source_match=%d;numeric=%" PRIuPTR ";numeric_matches_literal4=%d;selection=direct_constructor_call;behavior=forward_only",
            getpid(), gettid(), monotonic_ns(), self, valid, valid ? input : "unvalidated",
            valid ? fixed_source : "unvalidated", valid && std::strcmp(input, fixed_source) == 0,
            arg2, arg2 == 4);
    }
    if (hit < 32) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "cobblestone_ctor=entry;hit=%u;mono_ns=%" PRIu64 ";x0_this=0x%" PRIxPTR
            ";x1=0x%" PRIxPTR ";x2=0x%" PRIxPTR ";x3=0x%" PRIxPTR
            ";return_pc=0x%" PRIxPTR ";pid=%d;tid=%d;object_read=0",
            hit + 1, monotonic_ns(), self, arg1, arg2, arg3, return_pc, getpid(), gettid());
    }
    const CobblestoneConstructor original = cobblestone_constructor_original;
    if (!original) return;
    original(self, arg1, arg2, arg3);
    if (hit < 64) cobblestone_constructor_objects[hit].store(self, std::memory_order_release);
    if (hit < 8) {
        hybrid::SelfMemory memory;
        char name[97]{};
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_type_constructor_input=observed;pid=%d;tid=%d;type=0x%" PRIxPTR
            ";name=%s;numeric_raw=0x%" PRIxPTR ";material=0x%" PRIxPTR,
            getpid(), gettid(), self, observed_name(memory, arg1, name) ? name : "unvalidated", arg2, arg3);
        type_preparation_snapshot("constructor_return", self);
    }
    construct_unregistered_type(arg2, arg3);
    if (hit < 32) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "cobblestone_ctor=return;hit=%u;mono_ns=%" PRIu64 ";this=0x%" PRIxPTR
            ";pid=%d;tid=%d;object_read=0",
            hit + 1, monotonic_ns(), self, getpid(), gettid());
    }
}
#endif

struct EngineMapping {
    uintptr_t base = 0;
    bool found = false;
    hybrid::EngineImage image;
};

uint64_t monotonic_ns() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<uint64_t>(now.tv_nsec);
}

void checkpoint(const char* stage, const char* detail = "") {
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "candidate_trace_stage=%s;elapsed_ns=%" PRIu64 ";pid=%d;tid=%d;%s",
        stage, monotonic_ns(), getpid(), gettid(), detail);
}

int find_engine(dl_phdr_info* info, size_t, void* out) {
    if (!info->dlpi_name) return 0;
    const char* base = std::strrchr(info->dlpi_name, '/');
    if (std::strcmp(base ? base + 1 : info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& mapping = *static_cast<EngineMapping*>(out);
    mapping.base = info->dlpi_addr;
    mapping.image.base = info->dlpi_addr;
    mapping.found = true;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        uintptr_t address = 0;
        if (ph.p_type == PT_LOAD && mapping.image.address(ph.p_vaddr, address))
            mapping.image.segments.push_back({address, static_cast<size_t>(ph.p_memsz), ph.p_flags});
    }
    return 1;
}

bool engine_executable(uintptr_t address) {
#ifdef HYBRID_EXPERIMENTAL_ENGINE_HOOK
    EngineMapping mapping{};
    dl_iterate_phdr(find_engine, &mapping);
    return mapping.found && mapping.base == state_engine_base.load(std::memory_order_acquire) &&
        mapping.image.contains(address, 4, PF_R | PF_X);
#else
    (void)address;
    return false;
#endif
}

const char* start_trace() {
#ifndef HYBRID_EXPERIMENTAL_NATIVE
    return "candidate_trace=disabled";
#else
    checkpoint("mod_start_entered", "interception=disabled");
    checkpoint("engine_verification_begin");
    const auto verified = hybrid::verify_loaded_engine();
    if (!verified.verified || !verified.profile) {
        checkpoint("engine_verification_failed", verified.status.c_str());
        return "candidate_trace=blocked;reason=engine_not_verified;interception=disabled";
    }
    checkpoint("engine_verification_complete", verified.profile->id);
    if (std::strcmp(verified.profile->id, kProfile))
        return "candidate_trace=blocked;reason=unsupported_profile;interception=disabled";

    checkpoint("target_resolution_begin");
    EngineMapping mapping{};
    dl_iterate_phdr(find_engine, &mapping);
    if (!mapping.found || kCallsiteElfVa > UINTPTR_MAX - mapping.base || kTargetElfVa > UINTPTR_MAX - mapping.base) {
        checkpoint("target_resolution_failed", "reason=engine_mapping_absent");
        return "candidate_trace=blocked;reason=engine_mapping_absent;interception=disabled";
    }
    const uintptr_t callsite = mapping.base + kCallsiteElfVa;
    const uintptr_t target = mapping.base + kTargetElfVa;
    char detail[256];
    std::snprintf(detail, sizeof detail,
        "load_bias=0x%" PRIxPTR ";callsite_elf_va=0x%" PRIxPTR ";callsite_runtime=0x%" PRIxPTR
        ";target_elf_va=0x%" PRIxPTR ";target_runtime=0x%" PRIxPTR,
        mapping.base, kCallsiteElfVa, callsite, kTargetElfVa, target);
    checkpoint("target_resolution_complete", detail);

    checkpoint("candidate_validation_begin");
    if (!mapping.image.contains(callsite, 4, PF_R | PF_X) || !mapping.image.contains(target, 4, PF_R | PF_X)) {
        checkpoint("candidate_validation_failed", "reason=address_outside_executable_segment");
        return "candidate_trace=blocked;reason=address_outside_executable_segment;interception=disabled";
    }
    hybrid::SelfMemory memory;
    uint32_t instruction = 0;
    if (!memory.available() || !memory.read(callsite, &instruction, sizeof instruction)) {
        checkpoint("candidate_validation_failed", "reason=callsite_unreadable");
        return "candidate_trace=blocked;reason=callsite_unreadable;interception=disabled";
    }
    if ((instruction & 0xfc000000U) != 0x94000000U || instruction != kOriginalCall) {
        checkpoint("candidate_validation_failed", "reason=call_instruction_mismatch");
        return "candidate_trace=blocked;reason=call_instruction_mismatch;interception=disabled";
    }
    const int64_t immediate = static_cast<int64_t>(static_cast<int32_t>(instruction << 6)) >> 4;
    if (static_cast<uintptr_t>(static_cast<int64_t>(callsite) + immediate) != target) {
        checkpoint("candidate_validation_failed", "reason=call_target_mismatch");
        return "candidate_trace=blocked;reason=call_target_mismatch;interception=disabled";
    }
    std::snprintf(detail, sizeof detail,
        "callsite_elf_va=0x%" PRIxPTR ";target_elf_va=0x%" PRIxPTR ";instruction=0x%08x",
        kCallsiteElfVa, kTargetElfVa, instruction);
    checkpoint("candidate_validation_complete", detail);
    checkpoint("hook_install_skipped", "reason=interception_disabled;code_mutation=none");
    return "candidate_trace=validated;interception=disabled;code_mutation=none";
#endif
}

const char* stop_trace() {
#ifndef HYBRID_EXPERIMENTAL_NATIVE
    return "candidate_trace=disabled";
#else
    checkpoint("cleanup_complete", "interception=disabled;hook_installed=0");
    return "candidate_trace=stopped;interception=disabled;cleanup=not_needed";
#endif
}

const char* start_candidate_hook() {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    return "engine_candidate_hook=disabled;reason=build_gate";
#else
    std::lock_guard<std::mutex> guard(candidate_hook_lock);
    if (candidate_hook_stub) return "engine_candidate_hook=already_installed";
    const auto verified = hybrid::verify_loaded_engine();
    if (!verified.verified || !verified.profile || std::strcmp(verified.profile->id, kProfile))
        return "engine_candidate_hook=blocked;reason=engine_profile_mismatch";
    EngineMapping mapping{};
    dl_iterate_phdr(find_engine, &mapping);
    if (!mapping.found || mapping.base > UINTPTR_MAX - kCallsiteElfVa ||
            mapping.base > UINTPTR_MAX - kTargetElfVa ||
            mapping.base > UINTPTR_MAX - kRegistryGlobalElfVa ||
            !mapping.image.contains(mapping.base + kCallsiteElfVa, 4, PF_R | PF_X) ||
            !mapping.image.contains(mapping.base + kTargetElfVa, sizeof kTargetPrologue, PF_R | PF_X) ||
            !mapping.image.contains(mapping.base + kRegistryGlobalElfVa, 0x158, PF_R | PF_W))
        return "engine_candidate_hook=blocked;reason=executable_mapping_mismatch";
    hybrid::SelfMemory memory;
    uint32_t call = 0;
    uint32_t prologue[4]{};
    const uintptr_t callsite = mapping.base + kCallsiteElfVa;
    const uintptr_t target = mapping.base + kTargetElfVa;
    const uintptr_t registry = mapping.base + kRegistryGlobalElfVa;
    if (!memory.available() || !memory.read(callsite, &call, sizeof call) ||
            !memory.read(target, prologue, sizeof prologue) || call != kOriginalCall ||
            std::memcmp(prologue, kTargetPrologue, sizeof prologue))
        return "engine_candidate_hook=blocked;reason=instruction_mismatch";
    const int64_t immediate = static_cast<int64_t>(static_cast<int32_t>(call << 6)) >> 4;
    if (static_cast<uintptr_t>(static_cast<int64_t>(callsite) + immediate) != target)
        return "engine_candidate_hook=blocked;reason=call_target_mismatch";
    const int init = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init) return "engine_candidate_hook=blocked;reason=shadowhook_init";
    candidate_hits.store(0, std::memory_order_relaxed);
    candidate_expected_return_pc.store(callsite + 4, std::memory_order_relaxed);
    candidate_registry_identity.store(registry, std::memory_order_relaxed);
    candidate_original = nullptr;
    candidate_hook_stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&candidate_proxy), reinterpret_cast<void**>(&candidate_original));
    if (!candidate_hook_stub || !candidate_original) {
        const int error = shadowhook_get_errno();
        if (candidate_hook_stub && shadowhook_unhook(candidate_hook_stub) == 0) candidate_hook_stub = nullptr;
        candidate_original = nullptr;
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "engine_candidate_hook=failed;errno=%d;target=0x%" PRIxPTR ";pid=%d", error, target, getpid());
        return "engine_candidate_hook=failed;stage=install";
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "engine_candidate_hook=installed;target_elf_va=0x%" PRIxPTR ";target_runtime=%p"
        ";callsite_elf_va=0x%" PRIxPTR ";callsite_runtime=%p;load_bias=0x%" PRIxPTR
        ";expected_return_runtime=%p;registry_global_runtime=%p"
        ";pid=%d;tid=%d;sha256=fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1;args=raw_only",
        kTargetElfVa, reinterpret_cast<void*>(target), kCallsiteElfVa, reinterpret_cast<void*>(callsite),
        mapping.base, reinterpret_cast<void*>(callsite + 4), reinterpret_cast<void*>(registry), getpid(), gettid());
    return "engine_candidate_hook=installed;registration=unverified;args=raw_only;registry_relation=identity_comparison_only";
#endif
}

const char* stop_candidate_hook() {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    return "engine_candidate_hook=not_installed;reason=build_gate";
#else
    std::lock_guard<std::mutex> guard(candidate_hook_lock);
    if (!candidate_hook_stub) return "engine_candidate_hook=not_installed";
    const int result = shadowhook_unhook(candidate_hook_stub);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "engine_candidate_hook=removed;result=%d;retained=%d;hits=%u;pid=%d;tid=%d",
        result, result != 0, candidate_hits.load(std::memory_order_relaxed), getpid(), gettid());
    if (result) return "engine_candidate_hook=cleanup_failed;handle_retained=1";
    candidate_hook_stub = nullptr;
    candidate_original = nullptr;
    candidate_expected_return_pc.store(0, std::memory_order_relaxed);
    candidate_registry_identity.store(0, std::memory_order_relaxed);
    return "engine_candidate_hook=removed;cleanup=verified";
#endif
}

const char* start_cobblestone_constructor_hook() {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    return "cobblestone_constructor_hook=disabled;reason=build_gate";
#else
    std::lock_guard<std::mutex> guard(cobblestone_hook_lock);
    if (cobblestone_hook_stub) return "cobblestone_constructor_hook=already_installed";
    const auto verified = hybrid::verify_loaded_engine();
    if (!verified.verified || !verified.profile || std::strcmp(verified.profile->id, kProfile))
        return "cobblestone_constructor_hook=blocked;reason=engine_profile_mismatch";
    EngineMapping mapping{};
    dl_iterate_phdr(find_engine, &mapping);
    if (!mapping.found || mapping.base > UINTPTR_MAX - kCobblestoneConstructorElfVa ||
            !mapping.image.contains(mapping.base + kCobblestoneConstructorElfVa,
                sizeof kCobblestoneConstructorPrologue, PF_R | PF_X))
        return "cobblestone_constructor_hook=blocked;reason=executable_mapping_mismatch";
    hybrid::SelfMemory memory;
    uint32_t prologue[4]{};
    const uintptr_t target = mapping.base + kCobblestoneConstructorElfVa;
    if (!memory.available() || !memory.read(target, prologue, sizeof prologue) ||
            std::memcmp(prologue, kCobblestoneConstructorPrologue, sizeof prologue))
        return "cobblestone_constructor_hook=blocked;reason=instruction_mismatch";
    const int init = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init) return "cobblestone_constructor_hook=blocked;reason=shadowhook_init";
    cobblestone_constructor_hits.store(0, std::memory_order_relaxed);
    for (auto& object : cobblestone_constructor_objects) object.store(0, std::memory_order_relaxed);
    cobblestone_constructor_original = nullptr;
    cobblestone_hook_stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&cobblestone_constructor_proxy),
        reinterpret_cast<void**>(&cobblestone_constructor_original));
    if (!cobblestone_hook_stub || !cobblestone_constructor_original) {
        const int error = shadowhook_get_errno();
        if (cobblestone_hook_stub && shadowhook_unhook(cobblestone_hook_stub) == 0)
            cobblestone_hook_stub = nullptr;
        cobblestone_constructor_original = nullptr;
        __android_log_print(ANDROID_LOG_ERROR, kTag,
            "cobblestone_constructor_hook=failed;stage=install;errno=%d;target=%p;pid=%d",
            error, reinterpret_cast<void*>(target), getpid());
        return "cobblestone_constructor_hook=failed;stage=install";
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "cobblestone_constructor_hook=installed;elf_va=0x%" PRIxPTR
        ";runtime=%p;load_bias=0x%" PRIxPTR
        ";sha256=fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1;abi=gp4_forwarded;callsite_observed=4_gp_args;full_contract=provisional;reads=0;pid=%d;tid=%d",
        kCobblestoneConstructorElfVa, reinterpret_cast<void*>(target), mapping.base, getpid(), gettid());
    struct ObservationHook {
        const char* name;
        uintptr_t va;
        void* proxy;
        void** original;
        void** stub;
        uint32_t code[4];
        unsigned gp_args;
    };
    ObservationHook hooks[]{
        {"block_registration_version_gate", 0x115476e4, reinterpret_cast<void*>(&registration_version_proxy),
            reinterpret_cast<void**>(&registration_version_original), &registration_version_stub,
            {0x39406008, 0x39406029, 0x2a090108, 0x35000208}, 2},
        {"block_state_sweep", kBlockStateSweepElfVa, reinterpret_cast<void*>(&block_state_sweep_proxy),
            reinterpret_cast<void**>(&block_state_sweep_original), &block_state_sweep_stub,
            {kBlockStateSweepPrologue[0], kBlockStateSweepPrologue[1],
             kBlockStateSweepPrologue[2], kBlockStateSweepPrologue[3]}, 2},
        {"block_type_attachment", 0x1010bb6c, reinterpret_cast<void*>(&type_attachment_proxy),
            reinterpret_cast<void**>(&type_attachment_original), &type_attachment_stub,
            {0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0x3dc00020}, 2},
        {"block_state_factory", 0x101071e0, reinterpret_cast<void*>(&state_factory_proxy),
            reinterpret_cast<void**>(&state_factory_original), &state_factory_stub,
            {0xd10203ff, 0xa9027bfd, 0xa9036ffc, 0xa90467fa}, 2},
        {"block_state_constructor", 0x101f95a0, reinterpret_cast<void*>(&state_constructor_proxy),
            reinterpret_cast<void**>(&state_constructor_original), &state_constructor_stub,
            {0xd10143ff, 0xa9027bfd, 0xa90357f6, 0xa9044ff4}, 3},
        {"block_state_append", 0x1010d108, reinterpret_cast<void*>(&state_append_proxy),
            reinterpret_cast<void**>(&state_append_original), &state_append_stub,
            {0xa9bc7bfd, 0xf9000bf7, 0xa90257f6, 0xa9034ff4}, 2},
        {"block_palette_append", 0xfb8ba5c, reinterpret_cast<void*>(&palette_append_proxy),
            reinterpret_cast<void**>(&palette_append_original), &palette_append_stub,
            {0xa9bb7bfd, 0xf9000bf9, 0xa9025ff8, 0xa90357f6}, 2},
        {"block_namespace_index", 0x94f66e0, reinterpret_cast<void*>(&registry_namespace_proxy),
            reinterpret_cast<void**>(&registry_namespace_original), &registry_namespace_stub,
            {0xa9bb7bfd, 0xa90167fa, 0xa9025ff8, 0xa90357f6}, 3},
        {"block_identity_index", 0x94f6a0c, reinterpret_cast<void*>(&registry_identity_proxy),
            reinterpret_cast<void**>(&registry_identity_original), &registry_identity_stub,
            {0xd10143ff, 0xa9027bfd, 0xf9001bf5, 0xa9044ff4}, 2},
        {"block_identity_value_copy", 0x1282d394, reinterpret_cast<void*>(&registry_identity_copy_proxy),
            reinterpret_cast<void**>(&registry_identity_copy_original), &registry_identity_copy_stub,
            {0xa9be7bfd, 0xa9014ff4, 0x910003fd, 0xaa0103f3}, 2}
    };
    state_engine_base.store(mapping.base, std::memory_order_release);
    const uint32_t allocator_branch = 0x14000000U | ((0x12911390U - 0xe49677cU) / 4);
    const uint32_t name_constructor_code[4]{0xa9bd7bfd, 0xa90157f6, 0xa9024ff4, 0x910003fd};
    uint32_t allocator_runtime = 0, name_runtime[4]{};
    construction_contracts_verified.store(
        mapping.image.contains(mapping.base + 0xe49677c, 4, PF_R | PF_X) &&
        memory.read(mapping.base + 0xe49677c, &allocator_runtime, 4) && allocator_runtime == allocator_branch &&
        mapping.image.contains(mapping.base + 0x85380f0, sizeof name_runtime, PF_R | PF_X) &&
        memory.read(mapping.base + 0x85380f0, name_runtime, sizeof name_runtime) &&
        std::memcmp(name_runtime, name_constructor_code, sizeof name_runtime) == 0,
        std::memory_order_release);
    construction_requested.store(false, std::memory_order_release);
    state_append_hits.store(0, std::memory_order_relaxed);
    state_factory_hits.store(0, std::memory_order_relaxed);
    state_constructor_hits.store(0, std::memory_order_relaxed);
    type_attachment_hits.store(0, std::memory_order_relaxed);
    registry_namespace_hits.store(0, std::memory_order_relaxed);
    registry_identity_hits.store(0, std::memory_order_relaxed);
    registry_identity_copy_hits.store(0, std::memory_order_relaxed);
    registry_identity_copy_bytes_logged.store(0, std::memory_order_relaxed);
    for (auto& type : numeric_identity_types) type.store(0, std::memory_order_relaxed);
    numeric_identity_states.store(0, std::memory_order_relaxed);
    registration_version_hits.store(0, std::memory_order_relaxed);
    custom_registration_attempted.store(false, std::memory_order_relaxed);
    custom_registered_type.store(0, std::memory_order_relaxed);
    custom_default_state.store(0, std::memory_order_relaxed);
    palette_append_hits.store(0, std::memory_order_relaxed);
    state_append_calls.store(0, std::memory_order_relaxed);
    palette_append_calls.store(0, std::memory_order_relaxed);
    unsigned installed_count = 0, failed_count = 0;
    for (auto& hook : hooks) {
        const uintptr_t address = mapping.base + hook.va;
        uint32_t code[4]{};
        if (!mapping.image.contains(address, sizeof code, PF_R | PF_X) ||
                !memory.read(address, code, sizeof code) || std::memcmp(code, hook.code, sizeof code)) {
            ++failed_count;
            __android_log_print(ANDROID_LOG_ERROR, kTag,
                "%s_hook=blocked;reason=instruction_mismatch;elf_va=0x%" PRIxPTR ";pid=%d;tid=%d",
                hook.name, hook.va, getpid(), gettid());
            continue;
        }
        *hook.stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(address), hook.proxy, hook.original);
        const bool installed = *hook.stub && *hook.original;
        const int error = installed ? 0 : shadowhook_get_errno();
        if (installed) {
            ++installed_count;
        } else {
            ++failed_count;
            if (*hook.stub && shadowhook_unhook(*hook.stub) == 0) {
                *hook.stub = nullptr;
                *hook.original = nullptr;
            }
        }
        __android_log_print(installed ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag,
            "%s_hook=%s;elf_va=0x%" PRIxPTR ";pid=%d;tid=%d;abi=observed_gp%d;behavior=forward_only;"
            "shadowhook_error=%d;shadowhook_message=%s",
            hook.name, installed ? "installed" : "failed", hook.va,
            getpid(), gettid(), hook.gp_args, error, installed ? "none" : shadowhook_to_errmsg(error));
    }
    if (failed_count) {
        static char partial_status[112];
        std::snprintf(partial_status, sizeof partial_status,
            "cobblestone_constructor_hook=partial;installed=%u;failed=%u;object_dereferences=0",
            installed_count, failed_count);
        return partial_status;
    }
    return "cobblestone_constructor_hook=installed;behavior=forward_only;object_dereferences=0";
#endif
}

const char* stop_cobblestone_constructor_hook() {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    return "cobblestone_constructor_hook=not_installed;reason=build_gate";
#else
    std::lock_guard<std::mutex> guard(cobblestone_hook_lock);
    construction_requested.store(false, std::memory_order_release);
    if (unregistered_type.load(std::memory_order_acquire))
        return "cobblestone_constructor_hook=cleanup_failed;pending_native_object_retained=1";
    for (void** stub : {&registration_version_stub, &block_state_sweep_stub, &palette_append_stub, &state_append_stub, &state_constructor_stub,
                       &state_factory_stub, &type_attachment_stub, &registry_identity_copy_stub,
                       &registry_identity_stub, &registry_namespace_stub}) {
        if (!*stub) continue;
        const int status = shadowhook_unhook(*stub);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_state_hooks=cleanup;result=%d;handle_retained=%d;factory=%d;constructor=%d;attachment=%d;pid=%d",
            status, status != 0, stub == &state_factory_stub, stub == &state_constructor_stub,
            stub == &type_attachment_stub, getpid());
        if (status) return "cobblestone_constructor_hook=cleanup_failed;state_hook_retained=1";
        *stub = nullptr;
    }
    if (!cobblestone_hook_stub) return "cobblestone_constructor_hook=not_installed";
    const int result = shadowhook_unhook(cobblestone_hook_stub);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "cobblestone_constructor_hook=removed;result=%d;retained=%d;hits=%u;pid=%d;tid=%d",
        result, result != 0, cobblestone_constructor_hits.load(std::memory_order_relaxed), getpid(), gettid());
    if (result) return "cobblestone_constructor_hook=cleanup_failed;handle_retained=1";
    cobblestone_hook_stub = nullptr;
    cobblestone_constructor_original = nullptr;
    return "cobblestone_constructor_hook=removed;cleanup=verified";
#endif
}
}

extern "C" const char* hybrid_experimental_candidate_trace_start() { return start_trace(); }
extern "C" const char* hybrid_experimental_candidate_trace_stop() { return stop_trace(); }
extern "C" const char* hybrid_experimental_engine_candidate_hook_start() {
    return start_candidate_hook();
}
extern "C" const char* hybrid_experimental_engine_candidate_hook_stop() {
    return stop_candidate_hook();
}
extern "C" const char* hybrid_experimental_cobblestone_constructor_hook_start() {
    return start_cobblestone_constructor_hook();
}
extern "C" const char* hybrid_experimental_cobblestone_constructor_hook_stop() {
    return stop_cobblestone_constructor_hook();
}
extern "C" int hybrid_experimental_block_construction_arm() {
#if defined(HYBRID_EXPERIMENTAL_NATIVE) && defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    std::lock_guard<std::mutex> guard(cobblestone_hook_lock);
    if (!cobblestone_hook_stub || !state_factory_stub || !construction_contracts_verified.load() ||
        unregistered_type.load()) return -4;
    const bool already = construction_requested.exchange(true, std::memory_order_acq_rel);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "native_type_construction=armed;pid=%d;duplicate=%d;execution=next_vanilla_constructor_thread;registration=disabled",
        getpid(), already);
    return already ? -2 : 0;
#else
    return -4;
#endif
}
extern "C" int hybrid_experimental_cobblestone_constructor_match(std::uintptr_t object) {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    (void)object;
    return 0;
#else
    const unsigned count = cobblestone_constructor_hits.load(std::memory_order_acquire);
    const unsigned bounded = count < 64 ? count : 64;
    for (unsigned i = 0; i < bounded; ++i)
        if (object && cobblestone_constructor_objects[i].load(std::memory_order_acquire) == object)
            return static_cast<int>(i + 1);
    return 0;
#endif
}
