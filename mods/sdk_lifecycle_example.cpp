#include "hybrid_mod.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <thread>

namespace {
HybridSdkSubscription subscription = 0;
const HybridSdkV1* sdk_api = nullptr;
std::atomic<bool> reader_running{false};
std::thread world_reader;

void log_line(bool ok, const char* text) {
    sdk_api->logging.write(ok ? HYBRID_SDK_LOG_INFO : HYBRID_SDK_LOG_WARNING,
                           "HybridSDKExample", text);
}

int logf(bool ok, const char* format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    log_line(ok, message);
    return written;
}

void query_registry_block() {
    const HybridIdentifier wanted{"minecraft", "cobblestone"};
    HybridBlockSnapshot block{};
    block.abi = HYBRID_SDK_ABI_V1;
    block.size = sizeof block;
    const auto status = sdk_api->blocks.find(&wanted, &block);
    if (status != HYBRID_SDK_OK) {
        logf(false, "sdk_block_lookup status=%d;source=public_sdk", status);
        return;
    }
    const bool valid = block.identifier.namespace_name && block.identifier.name &&
        !std::strcmp(block.identifier.namespace_name, wanted.namespace_name) &&
        !std::strcmp(block.identifier.name, wanted.name) && block.state_handle &&
        block.type_numeric_id == 4 && block.state_count == 1;
    logf(valid,
        "sdk_block_lookup status=%d;verified=%d;identifier=%s:%s;numeric=%u;states=%u;"
        "default_hash=%u;hash_assigned=%u;source=public_sdk",
        status, valid, block.identifier.namespace_name, block.identifier.name,
        block.type_numeric_id, block.state_count, block.default_state_hash,
        block.default_state_hash_assigned);
}

void read_world_block() {
    constexpr auto kPollDelay = std::chrono::milliseconds(250);
    bool logged_wait = false;
    while (reader_running.load(std::memory_order_acquire)) {
        HybridWorldSnapshot world{};
        world.abi = HYBRID_SDK_ABI_V1;
        world.size = sizeof world;
        const auto current_status = sdk_api->world.get_current(&world);
        if (current_status != HYBRID_SDK_OK) {
            if (!logged_wait) {
                logf(false, "sdk_world_context status=%d;waiting_for_live_blocksource=1;"
                            "source=public_sdk", current_status);
                logged_wait = true;
            }
            std::this_thread::sleep_for(kPollDelay);
            continue;
        }
        logf(true, "sdk_world_context status=0;world_id=%llu;dimension=%d;source=public_sdk",
             static_cast<unsigned long long>(world.world_id), world.dimension_id);

        // The Overworld floor is a fixed positive control at this coordinate.
        constexpr int32_t x = 0, y = -64, z = 0;
        if (world.dimension_id != 0) {
            logf(false, "sdk_world_block status=%d;dimension=%d;expected_dimension=0;"
                        "pos=%d,%d,%d;source=public_sdk",
                 HYBRID_SDK_UNSUPPORTED, world.dimension_id, x, y, z);
            return;
        }
        HybridBlockSnapshot block{};
        block.abi = HYBRID_SDK_ABI_V1;
        block.size = sizeof block;
        const auto status = sdk_api->world.get_block(world.world_id, x, y, z, &block);
        const char* name_space = block.identifier.namespace_name ?
            block.identifier.namespace_name : "";
        const char* name = block.identifier.name ? block.identifier.name : "";
        const bool verified = status == HYBRID_SDK_OK &&
            !std::strcmp(name_space, "minecraft") && !std::strcmp(name, "bedrock");
        logf(verified,
            "sdk_world_block status=%d;verified=%d;world_id=%llu;dimension=%d;pos=%d,%d,%d;"
            "identifier=%s:%s;numeric=%u;states=%u;state_hash=%u;hash_assigned=%u;"
            "source=public_sdk",
            status, verified, static_cast<unsigned long long>(world.world_id),
            world.dimension_id, x, y, z, name_space, name, block.type_numeric_id,
            block.state_count, block.default_state_hash, block.default_state_hash_assigned);
        return;
    }
}

void on_mod_event(const HybridSdkEvent* event, void*) {
    if (!event || event->abi != HYBRID_SDK_ABI_V1 ||
        !event->payload || event->payload_size < sizeof(HybridModLifecycleEvent)) return;
    const auto* lifecycle = static_cast<const HybridModLifecycleEvent*>(event->payload);
    if (event->event_id != HYBRID_EVENT_MOD_STARTED || !lifecycle->mod_id ||
        std::strcmp(lifecycle->mod_id, "hybridmcpe:sdk-events")) return;
    query_registry_block();
}

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost) ||
        !host->sdk || host->sdk->abi != HYBRID_SDK_ABI_V1 ||
        host->sdk->size < sizeof(HybridSdkV1)) return -1;
    sdk_api = host->sdk;
    const int32_t subscribed = sdk_api->events.subscribe(
        HYBRID_EVENT_MOD_STARTED, on_mod_event, nullptr, &subscription);
    if (subscribed != HYBRID_SDK_OK) {
        sdk_api = nullptr;
        return subscribed;
    }
    reader_running.store(true, std::memory_order_release);
    try {
        world_reader = std::thread(read_world_block);
    } catch (...) {
        reader_running.store(false, std::memory_order_release);
        sdk_api->events.unsubscribe(subscription);
        subscription = 0;
        sdk_api = nullptr;
        return -1;
    }
    return HYBRID_SDK_OK;
}

void stop() {
    reader_running.store(false, std::memory_order_release);
    if (subscription && sdk_api) sdk_api->events.unsubscribe(subscription);
    subscription = 0;
    if (world_reader.joinable()) world_reader.join();
    sdk_api = nullptr;
}

const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:sdk-events", 0, start, stop};
}

extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() {
    return &mod;
}
