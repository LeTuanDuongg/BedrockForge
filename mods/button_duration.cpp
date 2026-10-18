#include "hybrid_mod.h"
#include "engine_bridge/button_schedule_126504.h"
#include <android/log.h>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

namespace {
std::mutex lock;
bool started = false;
bool requested_enabled = false;

int prepare_and_set(bool enabled) {
    if (!enabled) return hybrid_engine_button_schedule_126504_control(2);
    const int prepared = hybrid_engine_button_schedule_126504_control(0);
    if (prepared) return prepared;
    const int result = hybrid_engine_button_schedule_126504_control(1);
    if (!result) return 0;
    const int cleanup = hybrid_engine_button_schedule_126504_control(2);
    return cleanup ? cleanup : result;
}

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost)) return -1;
    std::lock_guard<std::mutex> guard(lock);
    if (started) return -2;
    const int result = prepare_and_set(requested_enabled);
    if (result) return result;
    started = true;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "button_mod=lifecycle_start;enabled=%d;pid=%d", requested_enabled, getpid());
    return 0;
}

void stop() {
    std::lock_guard<std::mutex> guard(lock);
    if (!started) return;
    const int result = hybrid_engine_button_schedule_126504_control(2);
    if (result) {
        __android_log_print(ANDROID_LOG_ERROR, "HybridMCPE",
            "button_mod=cleanup_failed;result=%d;pid=%d;handle_must_be_retained=1", result, getpid());
        throw std::runtime_error("button schedule hook cleanup failed");
    }
    started = false;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "button_mod=lifecycle_stop;enabled_state_preserved=%d;pid=%d", requested_enabled, getpid());
}

const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:button-duration-experiment", 0, start, stop};
}

extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() { return &mod; }

extern "C" __attribute__((visibility("default"))) int hybrid_button_duration_mod_control(unsigned operation) {
    if (operation > 2) return -1;
    std::lock_guard<std::mutex> guard(lock);
    if (!started) return -2;
    if (operation == 0) return hybrid_engine_button_schedule_126504_control(0);
    const bool enabled = operation == 1;
    const int result = enabled ? prepare_and_set(true) : hybrid_engine_button_schedule_126504_control(2);
    if (!result) requested_enabled = enabled;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "button_mod=control;operation=%u;result=%d;enabled=%d;pid=%d;tid=%d",
        operation, result, requested_enabled, getpid(), gettid());
    return result;
}
