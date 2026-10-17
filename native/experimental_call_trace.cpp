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
constexpr uint32_t kOriginalCall = 0x94252ee5;
constexpr uint32_t kTargetPrologue[4]{0xfc190fe8, 0xa9017bfd, 0xa9026ffc, 0xa90367fa};
uint64_t monotonic_ns();

#ifdef HYBRID_EXPERIMENTAL_ENGINE_HOOK
using CandidateFunction = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
std::mutex candidate_hook_lock;
void* candidate_hook_stub = nullptr;
CandidateFunction candidate_original = nullptr;
std::atomic<unsigned> candidate_hits{0};
std::atomic<uintptr_t> candidate_expected_return_pc{0};
std::atomic<uintptr_t> candidate_registry_identity{0};

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
}

extern "C" const char* hybrid_experimental_candidate_trace_start() { return start_trace(); }
extern "C" const char* hybrid_experimental_candidate_trace_stop() { return stop_trace(); }
extern "C" const char* hybrid_experimental_engine_candidate_hook_start() {
    return start_candidate_hook();
}
extern "C" const char* hybrid_experimental_engine_candidate_hook_stop() {
    return stop_candidate_hook();
}
