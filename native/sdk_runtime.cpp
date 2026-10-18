#include "sdk_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace {
struct Subscription {
    HybridSdkSubscription id;
    uint32_t event_id;
    HybridSdkEventCallback callback;
    void* user_data;
};

std::recursive_mutex& event_mutex() {
    static std::recursive_mutex value;
    return value;
}
std::vector<Subscription>& subscriptions() {
    static std::vector<Subscription> value;
    return value;
}
HybridSdkSubscription& next_subscription() {
    static HybridSdkSubscription value = 1;
    return value;
}
uint64_t& next_sequence() {
    static uint64_t value = 1;
    return value;
}

int32_t subscribe(uint32_t event_id, HybridSdkEventCallback callback, void* user_data,
                  HybridSdkSubscription* out_subscription) {
    if (!event_id || !callback || !out_subscription) return HYBRID_SDK_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> guard(event_mutex());
    if (!next_subscription()) return HYBRID_SDK_UNSUPPORTED;
    const auto id = next_subscription()++;
    try {
        subscriptions().push_back({id, event_id, callback, user_data});
    } catch (...) {
        return HYBRID_SDK_UNSUPPORTED;
    }
    *out_subscription = id;
    return HYBRID_SDK_OK;
}

int32_t unsubscribe(HybridSdkSubscription id) {
    if (!id) return HYBRID_SDK_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> guard(event_mutex());
    auto& entries = subscriptions();
    const auto found = std::find_if(entries.begin(), entries.end(),
                                    [id](const Subscription& entry) { return entry.id == id; });
    if (found == entries.end()) return HYBRID_SDK_NOT_FOUND;
    entries.erase(found);
    return HYBRID_SDK_OK;
}

int32_t unsupported() { return HYBRID_SDK_UNSUPPORTED; }
int32_t log_message(uint32_t level, const char* tag, const char* message) {
    if (!tag || !message) return HYBRID_SDK_INVALID_ARGUMENT;
    if (level > 5) return HYBRID_SDK_INVALID_ARGUMENT;
    std::fprintf(stderr, "[HybridMCPE:%u] %s: %s\n", level, tag, message);
    return HYBRID_SDK_OK;
}

const HybridSdkV1 api{
    HYBRID_SDK_ABI_V1, sizeof(HybridSdkV1),
    HYBRID_SDK_CAP_EVENTS | HYBRID_SDK_CAP_MOD_DEPENDENCIES | HYBRID_SDK_CAP_LOGGING,
    {subscribe, unsubscribe},
    {
        [](HybridPlayerSnapshot* out) -> int32_t { return out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, HybridPlayerSnapshot* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, const char* message) -> int32_t { return id && message ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, const HybridVec3* position, int32_t) -> int32_t { return id && position ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](HybridWorldSnapshot* out) -> int32_t { return out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, int32_t, int32_t, int32_t, HybridBlockSnapshot* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, int32_t, int32_t, int32_t, const HybridIdentifier* block) -> int32_t { return id && block ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, uint64_t* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id, uint64_t) -> int32_t { return id ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](const char* command, char* output, size_t capacity, size_t*) -> int32_t { return command && (!capacity || output) ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const HybridCommandDescriptor* descriptor) -> int32_t { return descriptor ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const char* name) -> int32_t { return name && name[0] ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](const HybridBlockDescriptor* descriptor) -> int32_t { return descriptor ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const HybridIdentifier* id, HybridBlockSnapshot* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](const HybridItemDescriptor* descriptor) -> int32_t { return descriptor ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const HybridIdentifier* id, HybridItemStackSnapshot* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](uint64_t player, HybridInventorySnapshot* out) -> int32_t { return player && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t inventory, uint32_t, HybridItemStackSnapshot* out) -> int32_t { return inventory && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t inventory, uint32_t, uint32_t, uint32_t count) -> int32_t { return inventory && count ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](uint64_t id, HybridEntitySnapshot* out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t world, const HybridIdentifier* id, const HybridVec3* position, uint64_t* out) -> int32_t { return world && id && position && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t id) -> int32_t { return id ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](const char* id, const void* data, size_t size) -> int32_t { return id && id[0] && (!size || data) ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const char* id, const void* form, size_t size) -> int32_t { return id && id[0] && (!size || form) ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const char* id) -> int32_t { return id && id[0] ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](uint32_t, HybridSdkRenderCallback callback, void*, uint64_t*) -> int32_t { return callback ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t token) -> int32_t { return token ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](HybridSdkInputCallback callback, void*, uint64_t*) -> int32_t { return callback ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t token) -> int32_t { return token ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {
        [](HybridSdkTaskCallback callback, void*) -> int32_t { return callback ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](uint64_t, HybridSdkTaskCallback callback, void*) -> int32_t { return callback ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {log_message},
    {
        [](const char* key, char* output, size_t capacity, size_t*) -> int32_t { return key && (!capacity || output) ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const char* key, const char* value) -> int32_t { return key && value ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    },
    {[](uint64_t, const char* permission, int32_t* granted) -> int32_t { return permission && granted ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }},
    {[](const char* id, void** out) -> int32_t { return id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }},
    {[](const char* key, char* output, size_t capacity, size_t*) -> int32_t { return key && (!capacity || output) ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }},
    {[](uint64_t, const void* data, size_t size) -> int32_t { return size && data ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }},
    {
        [](const char* objective, uint64_t id, int64_t* out) -> int32_t { return objective && id && out ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; },
        [](const char* objective, uint64_t id, int64_t) -> int32_t { return objective && id ? unsupported() : HYBRID_SDK_INVALID_ARGUMENT; }
    }
};
}

namespace hybrid {
const HybridSdkV1* sdk_api_v1() { return &api; }

void sdk_dispatch_lifecycle(uint32_t event_id, const char* mod_id) {
    if (event_id < HYBRID_EVENT_MOD_STARTED || event_id > HYBRID_EVENT_MOD_STOPPED || !mod_id)
        return;
    std::lock_guard<std::recursive_mutex> guard(event_mutex());
    HybridModLifecycleEvent payload{HYBRID_SDK_ABI_V1, sizeof(HybridModLifecycleEvent), mod_id};
    HybridSdkEvent event{HYBRID_SDK_ABI_V1, sizeof(HybridSdkEvent), event_id, 0,
                         next_sequence()++, &payload, sizeof(payload)};
    const auto snapshot = subscriptions();
    for (const auto& entry : snapshot) {
        if (entry.event_id != event_id) continue;
        const auto still_subscribed = std::find_if(subscriptions().begin(), subscriptions().end(),
            [&entry](const Subscription& active) { return active.id == entry.id; });
        if (still_subscribed == subscriptions().end()) continue;
        try { entry.callback(&event, entry.user_data); } catch (...) {}
    }
}

int sdk_check_manifest(const HybridSdkModManifestV1* manifest,
                       const char* const* loaded_ids, size_t loaded_count) {
    if (!manifest || manifest->abi != HYBRID_SDK_ABI_V1 ||
        manifest->size < sizeof(HybridSdkModManifestV1) || !manifest->mod_id ||
        !manifest->mod_id[0] || !manifest->version || !manifest->version[0] ||
        manifest->dependency_count > HYBRID_SDK_MAX_DEPENDENCIES ||
        (manifest->dependency_count && !manifest->dependencies) ||
        (loaded_count && !loaded_ids)) return HYBRID_SDK_INVALID_MANIFEST;

    for (size_t i = 0; i < manifest->dependency_count; ++i) {
        const char* dependency = manifest->dependencies[i];
        if (!dependency || !dependency[0] || std::strcmp(dependency, manifest->mod_id) == 0)
            return HYBRID_SDK_INVALID_MANIFEST;
        for (size_t j = 0; j < i; ++j)
            if (std::strcmp(dependency, manifest->dependencies[j]) == 0)
                return HYBRID_SDK_INVALID_MANIFEST;
        bool found = false;
        for (size_t j = 0; j < loaded_count; ++j)
            if (loaded_ids[j] && std::strcmp(dependency, loaded_ids[j]) == 0) found = true;
        if (!found) return HYBRID_SDK_DEPENDENCY_MISSING;
    }
    return HYBRID_SDK_OK;
}
}
