#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"

#include <android/log.h>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr const char* kTag = "HybridMCPE";
constexpr const char* kProfile = "bedrock-1.26.50.4-arm64";
constexpr uintptr_t kCallsiteVa = 0xfb3c1ac;
constexpr uintptr_t kTargetVa = 0x10487d40;
constexpr uint32_t kOriginalCall = 0x94252ee5;

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
    if (!mapping.found || kCallsiteVa > UINTPTR_MAX - mapping.base || kTargetVa > UINTPTR_MAX - mapping.base) {
        checkpoint("target_resolution_failed", "reason=engine_mapping_absent");
        return "candidate_trace=blocked;reason=engine_mapping_absent;interception=disabled";
    }
    const uintptr_t callsite = mapping.base + kCallsiteVa;
    const uintptr_t target = mapping.base + kTargetVa;
    char detail[192];
    std::snprintf(detail, sizeof detail, "base=0x%" PRIxPTR ";callsite=0x%" PRIxPTR ";target=0x%" PRIxPTR,
        mapping.base, callsite, target);
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
    std::snprintf(detail, sizeof detail, "callsite_rva=0x%" PRIxPTR ";target_rva=0x%" PRIxPTR ";instruction=0x%08x",
        kCallsiteVa, kTargetVa, instruction);
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
}

extern "C" const char* hybrid_experimental_candidate_trace_start() { return start_trace(); }
extern "C" const char* hybrid_experimental_candidate_trace_stop() { return stop_trace(); }
extern "C" const char* hybrid_experimental_engine_candidate_hook_start() {
    return "engine_candidate_hook=disabled;reason=waiting_for_self_hook_test";
}
extern "C" const char* hybrid_experimental_engine_candidate_hook_stop() {
    return "engine_candidate_hook=not_installed";
}
