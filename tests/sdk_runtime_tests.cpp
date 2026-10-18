#include "sdk_runtime.h"

#include <cstdio>
#include <cstdlib>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int events_seen = 0;
uint64_t last_sequence = 0;
void on_event(const HybridSdkEvent* event, void*) {
    check(event && event->abi == HYBRID_SDK_ABI_V1, "event ABI");
    check(event->event_id == HYBRID_EVENT_MOD_STARTED, "event type");
    check(event->payload_size == sizeof(HybridModLifecycleEvent), "event payload size");
    const auto* lifecycle = static_cast<const HybridModLifecycleEvent*>(event->payload);
    check(lifecycle && lifecycle->mod_id && lifecycle->mod_id[0], "mod lifecycle payload");
    check(event->sequence > last_sequence, "monotonic event sequence");
    last_sequence = event->sequence;
    ++events_seen;
}
}

int main() {
    const auto* api = hybrid::sdk_api_v1();
    check(api && api->abi == HYBRID_SDK_ABI_V1 && api->size == sizeof(HybridSdkV1), "SDK table");
    check(api->capabilities == (HYBRID_SDK_CAP_EVENTS | HYBRID_SDK_CAP_MOD_DEPENDENCIES |
                               HYBRID_SDK_CAP_LOGGING),
          "only implemented capabilities advertised");
    HybridSdkSubscription subscription = 0;
    check(api->events.subscribe(HYBRID_EVENT_MOD_STARTED, on_event, nullptr, &subscription) == HYBRID_SDK_OK,
          "subscribe lifecycle event");
    hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STARTED, "test:one");
    hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPED, "test:one");
    check(events_seen == 1, "filter by event type");
    check(api->events.unsubscribe(subscription) == HYBRID_SDK_OK, "unsubscribe");
    check(api->events.unsubscribe(subscription) == HYBRID_SDK_NOT_FOUND, "duplicate unsubscribe rejected");
    hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STARTED, "test:two");
    check(events_seen == 1, "unsubscribed listener not called");

    HybridPlayerSnapshot player{};
    player.abi = HYBRID_SDK_ABI_V1;
    player.size = sizeof(HybridPlayerSnapshot);
    HybridWorldSnapshot world{};
    world.abi = HYBRID_SDK_ABI_V1;
    world.size = sizeof(HybridWorldSnapshot);
    check(api->player.get_local(&player) == HYBRID_SDK_UNSUPPORTED, "player explicitly unsupported");
    check(api->world.get_current(&world) == HYBRID_SDK_UNSUPPORTED, "world explicitly unsupported");
    check(api->player.get_local(nullptr) == HYBRID_SDK_INVALID_ARGUMENT, "player arg validation");

    const char* core_only[]{"test:core"};
    const char* loaded[]{"test:core"};
    HybridSdkModManifestV1 manifest{HYBRID_SDK_ABI_V1, sizeof(HybridSdkModManifestV1),
        "test:dependent", "1.0.0", core_only, 1};
    check(hybrid::sdk_check_manifest(&manifest, loaded, 1) == HYBRID_SDK_OK, "dependency satisfied");
    check(hybrid::sdk_check_manifest(&manifest, nullptr, 0) == HYBRID_SDK_DEPENDENCY_MISSING,
          "missing dependency rejected");
    const char* duplicate_deps[]{"test:core", "test:core"};
    manifest.dependencies = duplicate_deps; manifest.dependency_count = 2;
    check(hybrid::sdk_check_manifest(&manifest, loaded, 1) == HYBRID_SDK_INVALID_MANIFEST,
          "duplicate dependencies rejected");
    std::puts("PASS: lifecycle event bus, explicit player/world Unsupported, manifest dependency checks");
}
