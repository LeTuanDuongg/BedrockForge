#include "adapter.h"
#include "hybrid_mod.h"

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {
struct RuntimeBridge {
    std::mutex lock;
    bool loaded = false;
    bool enabled = false;
};

void record(const char* phase, const char* status) noexcept {
    char process[128]{};
    if (FILE* file = std::fopen("/proc/self/cmdline", "rb")) {
        std::fread(process, 1, sizeof(process) - 1, file);
        std::fclose(file);
    }
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPEBootstrap",
        "backend=preloader-1.5.24 phase=%s pid=%d process=%s status=%s",
        phase, getpid(), process, status);
}

bool engine_ready() {
    const char* status = hybrid_probe();
    if (!status) {
        record("engine_check", "probe_returned_null");
        return false;
    }
    record("engine_check", status);
    constexpr char verified_52[] = "engine_verified:1.26.52.3;layout_validated=5;";
    constexpr char verified_50[] = "engine_verified:1.26.50.4;layout_validated=0;";
    constexpr char diagnostic_only[] = ";capabilities=0;gameplay_binding=unavailable";
    const bool known_profile =
        std::strncmp(status, verified_52, sizeof(verified_52) - 1) == 0 ||
        std::strncmp(status, verified_50, sizeof(verified_50) - 1) == 0;
    return known_profile && std::strstr(status, diagnostic_only) != nullptr;
}

bool on_load(void* instance, pl::mod::ModContext&) noexcept {
    if (!instance) return false;
    auto& bridge = *static_cast<RuntimeBridge*>(instance);
    try {
        std::lock_guard<std::mutex> guard(bridge.lock);
        if (bridge.loaded || !engine_ready()) {
            record("load_rejected", bridge.loaded ? "duplicate_load" : "engine_not_ready");
            return false;
        }
        bridge.loaded = true;
        record("load", "runtime_verified_no_gameplay_capabilities");
        return true;
    } catch (...) {
        record("load_failed", "exception_caught");
        return false;
    }
}

bool on_enable(void* instance, pl::mod::ModContext&) noexcept {
    if (!instance) return false;
    auto& bridge = *static_cast<RuntimeBridge*>(instance);
    try {
        std::lock_guard<std::mutex> guard(bridge.lock);
        if (!bridge.loaded || bridge.enabled) {
            record("enable_rejected", "invalid_state");
            return false;
        }
        if (hybrid_enable_mods() != 0) {
            record("enable_failed", "hybrid_mod_resume_failed");
            return false;
        }
        bridge.enabled = true;
        record("enable", "runtime_active_no_gameplay_capabilities");
        return true;
    } catch (...) {
        record("enable_failed", "exception_caught");
        return false;
    }
}

bool on_disable(void* instance, pl::mod::ModContext&) noexcept {
    if (!instance) return false;
    auto& bridge = *static_cast<RuntimeBridge*>(instance);
    try {
        std::lock_guard<std::mutex> guard(bridge.lock);
        if (!bridge.loaded || !bridge.enabled) {
            record("disable_rejected", "invalid_state");
            return false;
        }
        if (hybrid_disable_mods() != 0) {
            record("disable_failed", "hybrid_mod_suspend_failed");
            return false;
        }
        bridge.enabled = false;
        record("disable", "runtime_stopped");
        return true;
    } catch (...) {
        record("disable_failed", "exception_caught");
        return false;
    }
}

bool on_unload(void* instance, pl::mod::ModContext&) noexcept {
    if (!instance) return false;
    auto& bridge = *static_cast<RuntimeBridge*>(instance);
    try {
        std::lock_guard<std::mutex> guard(bridge.lock);
        if (!bridge.loaded) {
            record("unload_rejected", "not_loaded");
            return false;
        }
        hybrid_shutdown();
        const char* status = hybrid_probe();
        constexpr char incomplete[] = "runtime_shutdown_incomplete:";
        if (!status || std::strncmp(status, incomplete, sizeof(incomplete) - 1) == 0) {
            record("unload_failed", status ? status : "probe_returned_null");
            return false;
        }
        bridge.enabled = false;
        bridge.loaded = false;
        record("unload", "runtime_stopped");
        return true;
    } catch (...) {
        record("unload_failed", "exception_caught_state_retained");
        return false;
    }
}

RuntimeBridge& bridge() {
    static RuntimeBridge state;
    return state;
}

pl::mod::ModRegistration& registration() {
    static pl::mod::ModRegistration value{
        &bridge(), on_load, on_enable, on_disable, on_unload};
    return value;
}
}

extern "C" HYBRID_PRELOADER_EXPORT pl::mod::ModRegistration*
PLGetModRegistration() {
    try {
        return &registration();
    } catch (...) {
        record("registration_failed", "exception_caught");
        return nullptr;
    }
}
