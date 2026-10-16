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
std::thread button_thread;
std::atomic<bool> button_running{false};
bool lookup_probe_active=false;
bool lookup_probe_used=false;
uint64_t monotonic_ns() {
    timespec time{};
    clock_gettime(CLOCK_BOOTTIME, &time);
    return static_cast<uint64_t>(time.tv_sec)*1000000000ULL+time.tv_nsec;
}

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost)) return -1;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "experimental_mod_abi_start=entered;pid=%d;tid=%d",
        getpid(), gettid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_registry_getter_probe(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_candidate_trace_start(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_hook_self_test(), getpid());
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s pid=%d",
        hybrid_experimental_engine_candidate_hook_start(), getpid());
    {
        std::lock_guard<std::mutex> lock(lookup_probe_mutex);
        if (lookup_probe_active) return -2;
        lookup_probe_active=true;
    }
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "registry_trigger=armed;automatic=0;used=%d;pid=%d;tid=%d", lookup_probe_used, getpid(), gettid());
    return 0;
}
void stop() {
    {
        std::lock_guard<std::mutex> lock(lookup_probe_mutex);
        lookup_probe_active=false;
        if (lookup_probe_thread.joinable()) lookup_probe_thread.join();
        if (button_thread.joinable()) button_thread.join();
        hybrid_experimental_button_control(2);
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
extern "C" __attribute__((visibility("default"))) int hybrid_experimental_trigger_registry_sample() {
    std::lock_guard<std::mutex> lock(lookup_probe_mutex);
    if (!lookup_probe_active) return -1;
    if (lookup_probe_used) return -2;
    try {
        lookup_probe_thread=std::thread([] {
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
                "registry_trigger=execute;phase=explicit_manual_probe;elapsed_ns=%llu;pid=%d;tid=%d;engine_thread=0",
                static_cast<unsigned long long>(monotonic_ns()), getpid(), gettid());
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s",
                hybrid_experimental_vanilla_block_lookup_probe());
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
                "registry_trigger=complete;elapsed_ns=%llu;pid=%d;tid=%d",
                static_cast<unsigned long long>(monotonic_ns()), getpid(), gettid());
        });
        lookup_probe_used=true;
        return 0;
    } catch (...) { return -3; }
}

extern "C" __attribute__((visibility("default"))) int hybrid_experimental_trigger_button(unsigned operation) {
    std::lock_guard<std::mutex> lock(lookup_probe_mutex);
    if (!lookup_probe_active || operation > 2) return -1;
    if (button_running.load()) return -2;
    if (button_thread.joinable()) button_thread.join();
    button_running.store(true);
    try {
        button_thread=std::thread([operation] {
            const int result=hybrid_experimental_button_control(operation);
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_command=complete;operation=%u;result=%d;pid=%d;tid=%d",
                operation, result, getpid(), gettid());
            button_running.store(false);
        });
        return 0;
    } catch (...) { button_running.store(false); return -3; }
}
