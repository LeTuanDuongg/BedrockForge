#include "adapter.h"
#include "hybrid_mod.h"
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unistd.h>

namespace {
std::mutex adapter_lock;
bool loaded = false;
bool enabled = false;

void record(const char* phase, const char* status) {
    char process[256]{};
    if (FILE* file = std::fopen("/proc/self/cmdline", "rb")) {
        std::fread(process, 1, sizeof(process) - 1, file);
        std::fclose(file);
    }
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPEBootstrap",
        "backend=preloader-c phase=%s pid=%d process=%s status=%s",
        phase, getpid(), process, status);
}

bool engine_ready() {
    const char* status = hybrid_probe();
    record("engine_check", status);
    // A hash match alone does not establish a matching mapped engine layout.
    return std::strstr(status, "engine_verified:1.26.52.3;layout_validated=5;") == status;
}
}

extern "C" bool PLMod_Load(JavaVM* vm, const PreloaderMetadata* info) {
    std::lock_guard<std::mutex> guard(adapter_lock);
    if (loaded || !vm || !info || info->size < sizeof(PreloaderMetadata) ||
        !info->mod_id || !info->library_path || info->library_path[0] != '/') {
        record("load_rejected", "invalid_metadata_or_duplicate_load");
        return false;
    }
    loaded = engine_ready();
    record("load", loaded ? "runtime_ready_no_gameplay_capabilities" : "engine_not_ready");
    return loaded;
}

extern "C" bool PLMod_Enable() {
    std::lock_guard<std::mutex> guard(adapter_lock);
    if (!loaded || enabled || !engine_ready()) {
        record("enable_rejected", "runtime_not_ready_or_already_enabled");
        return false;
    }
    enabled = true;
    record("enable", "runtime_ready_no_gameplay_capabilities");
    return true;
}

extern "C" bool PLMod_Disable() {
    std::lock_guard<std::mutex> guard(adapter_lock);
    hybrid_shutdown();
    enabled = false;
    record("disable", "runtime_stopped");
    return true;
}

extern "C" bool PLMod_Unload() {
    std::lock_guard<std::mutex> guard(adapter_lock);
    hybrid_shutdown();
    enabled = false;
    loaded = false;
    record("unload", "runtime_stopped");
    return true;
}
