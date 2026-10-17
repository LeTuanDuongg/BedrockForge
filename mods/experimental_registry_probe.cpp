#include "hybrid_mod.h"
#include "engine_bridge.h"
#include "experimental_hooks.h"
#include <android/log.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>

namespace {
std::mutex lookup_probe_mutex;
std::thread lookup_probe_thread;
bool lookup_probe_active=false;
unsigned lookup_probe_used_mask=0;
std::atomic<bool> lookup_probe_running{false};
uint64_t monotonic_ns() {
    timespec time{};
    clock_gettime(CLOCK_BOOTTIME, &time);
    return static_cast<uint64_t>(time.tv_sec)*1000000000ULL+time.tv_nsec;
}

const char* phase_name(unsigned phase) {
    switch (phase) {
        case 0: return "native_mod_abi_start";
        case 1: return "main_menu_manual";
        case 2: return "world_loaded_manual";
        default: return "invalid";
    }
}

int schedule_snapshot(unsigned phase) {
    if (phase > 2) return -1;
    std::lock_guard<std::mutex> lock(lookup_probe_mutex);
    if (!lookup_probe_active) return -1;
    const unsigned bit=1u<<phase;
    if (lookup_probe_used_mask&bit) return -2;
    if (lookup_probe_thread.joinable()) {
        if (lookup_probe_running.load(std::memory_order_acquire)) return -3;
        lookup_probe_thread.join();
    }
    lookup_probe_used_mask|=bit;
    lookup_probe_running.store(true,std::memory_order_release);
    const uint64_t requested=monotonic_ns();
    try {
        lookup_probe_thread=std::thread([phase,requested] {
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
                "registry_trigger=execute;phase=%s;requested_ns=%llu;elapsed_ns=%llu;pid=%d;tid=%d;engine_calls=0",
                phase_name(phase), static_cast<unsigned long long>(requested),
                static_cast<unsigned long long>(monotonic_ns()), getpid(), gettid());
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s",
                hybrid_experimental_registry_lifecycle_snapshot(phase));
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
                "registry_trigger=complete;phase=%s;elapsed_ns=%llu;pid=%d;tid=%d",
                phase_name(phase), static_cast<unsigned long long>(monotonic_ns()), getpid(), gettid());
            lookup_probe_running.store(false,std::memory_order_release);
        });
        return 0;
    } catch (...) {
        lookup_probe_used_mask&=~bit;
        lookup_probe_running.store(false,std::memory_order_release);
        return -4;
    }
}

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost)) return -1;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "experimental_mod_abi_start=entered;pid=%d;tid=%d",
        getpid(), gettid());
    {
        std::lock_guard<std::mutex> lock(lookup_probe_mutex);
        if (lookup_probe_active) return -2;
        lookup_probe_active=true;
    }
    const int startup_sample=schedule_snapshot(0);
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "registry_trigger=armed;phase=native_mod_abi_start;result=%d;pid=%d;tid=%d",
        startup_sample,getpid(),gettid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_registry_getter_probe(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_candidate_trace_start(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_hook_self_test(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_engine_candidate_hook_start(), getpid());
    return 0;
}
void stop() {
    {
        std::lock_guard<std::mutex> lock(lookup_probe_mutex);
        lookup_probe_active=false;
        if (lookup_probe_thread.joinable()) lookup_probe_thread.join();
    }
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_engine_candidate_hook_stop(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_candidate_trace_stop(), getpid());
}
const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:experimental-registry-probe", 0, start, stop};
}

extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() { return &mod; }

// Private experimental command; no address, arguments, or engine-thread callback is accepted.
extern "C" __attribute__((visibility("default"))) int hybrid_experimental_trigger_registry_sample(unsigned phase) {
    if (phase != 1 && phase != 2) return -1;
    return schedule_snapshot(phase);
}
