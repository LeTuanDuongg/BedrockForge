#include "hybrid_mod.h"
#include <android/log.h>
#include <unistd.h>
namespace {
int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost)) return -1;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "diagnostic_mod=start abi=1 pid=%d capabilities=%llu",
        getpid(), static_cast<unsigned long long>(host->capabilities));
    return 0;
}
void stop() { __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "diagnostic_mod=stop pid=%d", getpid()); }
const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:diagnostic", 0, start, stop};
}
extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() { return &mod; }
