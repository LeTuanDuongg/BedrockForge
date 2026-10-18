#include "hybrid_mod.h"

#include <cstdio>

namespace {
HybridSdkSubscription subscription = 0;
const HybridSdkV1* sdk_api = nullptr;

void on_mod_event(const HybridSdkEvent* event, void*) {
    if (!event || event->abi != HYBRID_SDK_ABI_V1 ||
        event->payload_size < sizeof(HybridModLifecycleEvent)) return;
    const auto* lifecycle = static_cast<const HybridModLifecycleEvent*>(event->payload);
    std::printf("sdk_event sequence=%llu type=%u mod=%s\n",
                static_cast<unsigned long long>(event->sequence), event->event_id,
                lifecycle->mod_id ? lifecycle->mod_id : "");
}

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost) ||
        !host->sdk || host->sdk->abi != HYBRID_SDK_ABI_V1 ||
        host->sdk->size < sizeof(HybridSdkV1)) return -1;
    sdk_api = host->sdk;
    return sdk_api->events.subscribe(HYBRID_EVENT_MOD_STARTED, on_mod_event, nullptr, &subscription);
}

void stop() {
    if (!subscription) return;
    if (sdk_api && sdk_api->events.unsubscribe(subscription) == HYBRID_SDK_OK) subscription = 0;
}

const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:sdk-events", 0, start, stop};
const HybridSdkModManifestV1 manifest{
    HYBRID_SDK_ABI_V1, sizeof(HybridSdkModManifestV1), mod.id, "1.0.0", nullptr, 0};
}

extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() {
    return &mod;
}
extern "C" __attribute__((visibility("default"))) const HybridSdkModManifestV1*
hybrid_sdk_manifest_v1() { return &manifest; }
