#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"

#include <android/log.h>
#include <atomic>
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
constexpr uint32_t kOriginalCall = 0x94252ee5;
constexpr uint32_t kTargetPrologue[4]{0xfc190fe8, 0xa9017bfd, 0xa9026ffc, 0xa90367fa};
constexpr uint32_t kCobblestoneConstructorPrologue[4]{0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0xaa0003f3};
uint64_t monotonic_ns();

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
std::atomic<uintptr_t> state_engine_base{0};
std::atomic<unsigned> state_append_hits{0}, palette_append_hits{0};
std::atomic<unsigned> state_append_calls{0}, palette_append_calls{0};

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
        memory.read(state + 0x68, &owner, sizeof owner) && observed_cobblestone(owner);
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
    const bool selected = observed_state(memory, state, owner) &&
        memory.read(palette, &vptr, sizeof vptr) && vptr == base + 0x13060690 &&
        memory.read(vptr + 24, &dispatch, sizeof dispatch) &&
        dispatch == base + 0xfb8ba5c && memory.read(palette + 0x48, before, sizeof before) &&
        before[1] >= before[0] && (before[1] - before[0]) % 8 == 0 &&
        (before[1] - before[0]) / 8 < 1048576;
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
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
        ";type=0x%" PRIxPTR ";caller=0x%" PRIxPTR ";read=%d;index_before=%zu;palette_key11c=%u"
        ";assigned12a=%u;last_matches=%d;primary_matches=%d;count_increased=%d"
        ";hash_found=%d;hash_index=%zu;hash_index_matches=%d;engine_mutation=original_only",
        getpid(), gettid(), monotonic_ns(), palette, vptr, state, owner, caller, read, index,
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
    };
    ObservationHook hooks[]{
        {"block_state_append", 0x1010d108, reinterpret_cast<void*>(&state_append_proxy),
            reinterpret_cast<void**>(&state_append_original), &state_append_stub,
            {0xa9bc7bfd, 0xf9000bf7, 0xa90257f6, 0xa9034ff4}},
        {"block_palette_append", 0xfb8ba5c, reinterpret_cast<void*>(&palette_append_proxy),
            reinterpret_cast<void**>(&palette_append_original), &palette_append_stub,
            {0xa9bb7bfd, 0xf9000bf9, 0xa9025ff8, 0xa90357f6}}
    };
    state_engine_base.store(mapping.base, std::memory_order_release);
    state_append_hits.store(0, std::memory_order_relaxed);
    palette_append_hits.store(0, std::memory_order_relaxed);
    state_append_calls.store(0, std::memory_order_relaxed);
    palette_append_calls.store(0, std::memory_order_relaxed);
    for (auto& hook : hooks) {
        const uintptr_t address = mapping.base + hook.va;
        uint32_t code[4]{};
        if (!mapping.image.contains(address, sizeof code, PF_R | PF_X) ||
                !memory.read(address, code, sizeof code) || std::memcmp(code, hook.code, sizeof code)) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "%s_hook=blocked;reason=instruction_mismatch", hook.name);
            continue;
        }
        *hook.stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(address), hook.proxy, hook.original);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "%s_hook=%s;elf_va=0x%" PRIxPTR ";pid=%d;tid=%d;abi=observed_gp2;behavior=forward_only;errno=%d",
            hook.name, *hook.stub && *hook.original ? "installed" : "failed", hook.va,
            getpid(), gettid(), shadowhook_get_errno());
    }
    return "cobblestone_constructor_hook=installed;behavior=forward_only;object_dereferences=0";
#endif
}

const char* stop_cobblestone_constructor_hook() {
#if !defined(HYBRID_EXPERIMENTAL_NATIVE) || !defined(HYBRID_EXPERIMENTAL_ENGINE_HOOK)
    return "cobblestone_constructor_hook=not_installed;reason=build_gate";
#else
    std::lock_guard<std::mutex> guard(cobblestone_hook_lock);
    for (void** stub : {&palette_append_stub, &state_append_stub}) {
        if (!*stub) continue;
        const int status = shadowhook_unhook(*stub);
        __android_log_print(ANDROID_LOG_INFO, kTag,
            "block_state_hooks=cleanup;result=%d;handle_retained=%d;pid=%d", status, status != 0, getpid());
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
