#include "sdk_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    HybridBlockSnapshot block{};
    block.abi = HYBRID_SDK_ABI_V1;
    block.size = sizeof block;
    block.state_handle = 0x1234;
    const HybridBlockSnapshot before = block;
    const HybridIdentifier vanilla{"minecraft", "cobblestone"};
    check(api->blocks.find(&vanilla, &block) == HYBRID_SDK_UNSUPPORTED,
          "absent engine is not a mock vanilla block");
    check(!std::memcmp(&block, &before, sizeof block), "failed query leaves output unchanged");
    check(api->blocks.find(nullptr, &block) == HYBRID_SDK_INVALID_ARGUMENT, "null block identifier");
    check(api->blocks.find(&vanilla, nullptr) == HYBRID_SDK_INVALID_ARGUMENT, "null block output");
    const HybridIdentifier malformed[]{ {nullptr, "stone"}, {"", "stone"},
        {"minecraft", nullptr}, {"minecraft", ""}, {"Minecraft", "stone"},
        {"minecraft", "minecraft:stone"}, {"minecraft", "stone with spaces"} };
    for (const auto& id : malformed)
        check(api->blocks.find(&id, &block) == HYBRID_SDK_INVALID_ARGUMENT, "invalid identifier rejected");
    block.abi = 99;
    check(api->blocks.find(&vanilla, &block) == HYBRID_SDK_INVALID_ARGUMENT, "block ABI validated");
    block.abi = HYBRID_SDK_ABI_V1;
    block.size = offsetof(HybridBlockSnapshot, type_numeric_id) - 1;
    check(api->blocks.find(&vanilla, &block) == HYBRID_SDK_INVALID_ARGUMENT, "short output rejected");
    block.size = offsetof(HybridBlockSnapshot, type_numeric_id);
    check(api->blocks.find(&vanilla, &block) == HYBRID_SDK_UNSUPPORTED, "original ABI snapshot prefix accepted");
    HybridBlockDescriptor descriptor{};
    check(api->blocks.register_block(&descriptor) == HYBRID_SDK_UNSUPPORTED, "custom registration stays disabled");
    check(api->logging.write(HYBRID_SDK_LOG_ERROR + 1, "test", "bad level") == HYBRID_SDK_INVALID_ARGUMENT,
          "logging rejects invalid public level");

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
    std::puts("PASS: lifecycle, dependency checks, Block ABI/arguments/failure safety; absent engine stays Unsupported");
}
