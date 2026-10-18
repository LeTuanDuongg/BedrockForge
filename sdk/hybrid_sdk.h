#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HYBRID_SDK_ABI_V1 1u
#define HYBRID_SDK_MAX_DEPENDENCIES 64u

typedef enum HybridSdkStatus {
    HYBRID_SDK_OK = 0,
    HYBRID_SDK_INVALID_ARGUMENT = -1,
    HYBRID_SDK_UNSUPPORTED = -2,
    HYBRID_SDK_NOT_FOUND = -3,
    HYBRID_SDK_DEPENDENCY_MISSING = -4,
    HYBRID_SDK_INVALID_MANIFEST = -5
} HybridSdkStatus;

typedef uint64_t HybridSdkCapability;
#define HYBRID_SDK_CAP_EVENTS (1ull << 0)
#define HYBRID_SDK_CAP_MOD_DEPENDENCIES (1ull << 1)
#define HYBRID_SDK_CAP_LOGGING (1ull << 2)
#define HYBRID_SDK_CAP_PLAYER_READ (1ull << 3)
#define HYBRID_SDK_CAP_WORLD_READ (1ull << 4)
#define HYBRID_SDK_CAP_COMMANDS (1ull << 5)
#define HYBRID_SDK_CAP_BLOCKS (1ull << 6)
#define HYBRID_SDK_CAP_ITEMS (1ull << 7)
#define HYBRID_SDK_CAP_INVENTORY (1ull << 8)
#define HYBRID_SDK_CAP_ENTITIES (1ull << 9)
#define HYBRID_SDK_CAP_GUI (1ull << 10)
#define HYBRID_SDK_CAP_RENDERING (1ull << 11)
#define HYBRID_SDK_CAP_INPUT (1ull << 12)
#define HYBRID_SDK_CAP_SCHEDULER (1ull << 13)
#define HYBRID_SDK_CAP_CONFIG (1ull << 14)
#define HYBRID_SDK_CAP_PERMISSIONS (1ull << 15)
#define HYBRID_SDK_CAP_SERVICES (1ull << 16)
#define HYBRID_SDK_CAP_LOCALIZATION (1ull << 17)
#define HYBRID_SDK_CAP_NETWORK (1ull << 18)
#define HYBRID_SDK_CAP_SCOREBOARD (1ull << 19)

typedef enum HybridSdkLogLevel {
    HYBRID_SDK_LOG_DEBUG = 0,
    HYBRID_SDK_LOG_INFO = 1,
    HYBRID_SDK_LOG_WARNING = 2,
    HYBRID_SDK_LOG_ERROR = 3
} HybridSdkLogLevel;

typedef struct HybridVec3 { float x, y, z; } HybridVec3;
typedef struct HybridIdentifier { const char* namespace_name; const char* name; } HybridIdentifier;

typedef enum HybridSdkEventId {
    HYBRID_EVENT_MOD_STARTED = 1,
    HYBRID_EVENT_MOD_STOPPING = 2,
    HYBRID_EVENT_MOD_STOPPED = 3
} HybridSdkEventId;

typedef struct HybridSdkEvent {
    uint32_t abi;
    uint32_t size;
    uint32_t event_id;
    uint32_t reserved;
    uint64_t sequence;
    const void* payload;
    size_t payload_size;
} HybridSdkEvent;

typedef struct HybridModLifecycleEvent {
    uint32_t abi;
    uint32_t size;
    const char* mod_id;
} HybridModLifecycleEvent;

typedef uint64_t HybridSdkSubscription;
typedef void (*HybridSdkEventCallback)(const HybridSdkEvent* event, void* user_data);

typedef struct HybridPlayerSnapshot {
    uint32_t abi, size;
    uint64_t entity_id;
    HybridVec3 position;
    int32_t dimension_id;
    uint32_t flags;
} HybridPlayerSnapshot;

typedef struct HybridWorldSnapshot {
    uint32_t abi, size;
    uint64_t world_id;
    int32_t dimension_id;
    uint32_t flags;
    uint64_t game_tick;
} HybridWorldSnapshot;

typedef struct HybridBlockSnapshot {
    uint32_t abi, size;
    /* SDK-owned strings, valid until process exit. No engine pointer escapes. */
    HybridIdentifier identifier;
    /* Opaque snapshot token, NOT an engine address or a persistent runtime ID. */
    uint64_t state_handle;
    /* Optional size-tagged tail: metadata copied while the registry is guarded. */
    uint32_t type_numeric_id;
    uint32_t state_count;
    uint32_t default_state_hash;
    uint32_t default_state_hash_assigned;
} HybridBlockSnapshot;

typedef struct HybridItemStackSnapshot {
    uint32_t abi, size;
    HybridIdentifier identifier;
    uint32_t count;
    uint32_t auxiliary_value;
} HybridItemStackSnapshot;

typedef struct HybridInventorySnapshot {
    uint32_t abi, size;
    uint64_t inventory_id;
    uint32_t slot_count;
    uint32_t flags;
} HybridInventorySnapshot;

typedef struct HybridEntitySnapshot {
    uint32_t abi, size;
    uint64_t entity_id;
    HybridIdentifier identifier;
    HybridVec3 position;
    int32_t dimension_id;
    uint32_t flags;
} HybridEntitySnapshot;

typedef struct HybridBlockDescriptor {
    uint32_t abi, size;
    HybridIdentifier identifier;
    const char* display_name;
    const char* material_id;
    uint32_t flags;
} HybridBlockDescriptor;

typedef struct HybridItemDescriptor {
    uint32_t abi, size;
    HybridIdentifier identifier;
    const char* display_name;
    uint32_t max_stack_size;
    uint32_t flags;
} HybridItemDescriptor;

typedef void (*HybridSdkCommandCallback)(const char* arguments, void* user_data);
typedef struct HybridCommandDescriptor {
    uint32_t abi, size;
    const char* name;
    const char* description;
    const char* permission;
    HybridSdkCommandCallback callback;
    void* user_data;
} HybridCommandDescriptor;

typedef void (*HybridSdkRenderCallback)(uint32_t stage, void* user_data);
typedef void (*HybridSdkInputCallback)(uint32_t event_type, const void* event_data,
                                       size_t event_size, void* user_data);
typedef void (*HybridSdkTaskCallback)(void* user_data);

typedef struct HybridSdkEventsApi {
    int32_t (*subscribe)(uint32_t event_id, HybridSdkEventCallback callback,
                         void* user_data, HybridSdkSubscription* out_subscription);
    int32_t (*unsubscribe)(HybridSdkSubscription subscription);
} HybridSdkEventsApi;

typedef struct HybridSdkPlayerApi {
    int32_t (*get_local)(HybridPlayerSnapshot* out_player);
    int32_t (*get_by_id)(uint64_t entity_id, HybridPlayerSnapshot* out_player);
    int32_t (*send_message)(uint64_t entity_id, const char* message);
    int32_t (*teleport)(uint64_t entity_id, const HybridVec3* position, int32_t dimension_id);
} HybridSdkPlayerApi;

typedef struct HybridSdkWorldApi {
    int32_t (*get_current)(HybridWorldSnapshot* out_world);
    int32_t (*get_block)(uint64_t world_id, int32_t x, int32_t y, int32_t z,
                         HybridBlockSnapshot* out_block);
    int32_t (*set_block)(uint64_t world_id, int32_t x, int32_t y, int32_t z,
                         const HybridIdentifier* identifier);
    int32_t (*get_time)(uint64_t world_id, uint64_t* out_time);
    int32_t (*set_time)(uint64_t world_id, uint64_t time);
} HybridSdkWorldApi;

typedef struct HybridSdkCommandApi {
    int32_t (*execute)(const char* command, char* output, size_t output_capacity,
                       size_t* output_size);
    int32_t (*register_command)(const HybridCommandDescriptor* descriptor);
    int32_t (*unregister_command)(const char* name);
} HybridSdkCommandApi;

typedef struct HybridSdkBlockApi {
    int32_t (*register_block)(const HybridBlockDescriptor* descriptor);
    /* Read-only default state. Set out_block.abi/size before calling.
       BLOCKS means lookup only; registration remains UNSUPPORTED.
       NOT_FOUND: no such key. UNSUPPORTED: wrong engine or state not ready.
       Failure leaves output unchanged. The original snapshot prefix is accepted. */
    int32_t (*find)(const HybridIdentifier* identifier, HybridBlockSnapshot* out_block);
} HybridSdkBlockApi;

typedef struct HybridSdkItemApi {
    int32_t (*register_item)(const HybridItemDescriptor* descriptor);
    int32_t (*find)(const HybridIdentifier* identifier, HybridItemStackSnapshot* out_item);
} HybridSdkItemApi;

typedef struct HybridSdkInventoryApi {
    int32_t (*get_player_inventory)(uint64_t player_id, HybridInventorySnapshot* out_inventory);
    int32_t (*get_slot)(uint64_t inventory_id, uint32_t slot, HybridItemStackSnapshot* out_item);
    int32_t (*transfer)(uint64_t inventory_id, uint32_t from_slot, uint32_t to_slot,
                        uint32_t count);
} HybridSdkInventoryApi;

typedef struct HybridSdkEntityApi {
    int32_t (*find)(uint64_t entity_id, HybridEntitySnapshot* out_entity);
    int32_t (*spawn)(uint64_t world_id, const HybridIdentifier* identifier,
                     const HybridVec3* position, uint64_t* out_entity_id);
    int32_t (*remove)(uint64_t entity_id);
} HybridSdkEntityApi;

typedef struct HybridSdkGuiApi {
    int32_t (*open_native)(const char* screen_id, const void* data, size_t data_size);
    int32_t (*show_form)(const char* form_id, const void* form, size_t form_size);
    int32_t (*close)(const char* screen_id);
} HybridSdkGuiApi;

typedef struct HybridSdkRenderingApi {
    int32_t (*register_callback)(uint32_t stage, HybridSdkRenderCallback callback,
                                 void* user_data, uint64_t* out_token);
    int32_t (*unregister_callback)(uint64_t token);
} HybridSdkRenderingApi;

typedef struct HybridSdkInputApi {
    int32_t (*subscribe)(HybridSdkInputCallback callback, void* user_data, uint64_t* out_token);
    int32_t (*unsubscribe)(uint64_t token);
} HybridSdkInputApi;

typedef struct HybridSdkSchedulerApi {
    int32_t (*post_game_thread)(HybridSdkTaskCallback callback, void* user_data);
    int32_t (*post_delayed)(uint64_t delay_ticks, HybridSdkTaskCallback callback, void* user_data);
} HybridSdkSchedulerApi;

typedef struct HybridSdkLoggingApi {
    int32_t (*write)(uint32_t level, const char* tag, const char* message);
} HybridSdkLoggingApi;

typedef struct HybridSdkConfigApi {
    int32_t (*read)(const char* key, char* output, size_t capacity, size_t* output_size);
    int32_t (*write)(const char* key, const char* value);
} HybridSdkConfigApi;

typedef struct HybridSdkPermissionsApi {
    int32_t (*check)(uint64_t player_id, const char* permission, int32_t* out_granted);
} HybridSdkPermissionsApi;

typedef struct HybridSdkServicesApi {
    int32_t (*find)(const char* service_id, void** out_service);
} HybridSdkServicesApi;

typedef struct HybridSdkLocalizationApi {
    int32_t (*translate)(const char* key, char* output, size_t capacity, size_t* output_size);
} HybridSdkLocalizationApi;

typedef struct HybridSdkNetworkApi {
    int32_t (*send)(uint64_t peer_id, const void* data, size_t size);
} HybridSdkNetworkApi;

typedef struct HybridSdkScoreboardApi {
    int32_t (*get_score)(const char* objective, uint64_t entity_id, int64_t* out_score);
    int32_t (*set_score)(const char* objective, uint64_t entity_id, int64_t score);
} HybridSdkScoreboardApi;

/* Stable C function tables; fields are size-tagged and must not be reordered. */
typedef struct HybridSdkV1 {
    uint32_t abi;
    uint32_t size;
    uint64_t capabilities;
    HybridSdkEventsApi events;
    HybridSdkPlayerApi player;
    HybridSdkWorldApi world;
    HybridSdkCommandApi commands;
    HybridSdkBlockApi blocks;
    HybridSdkItemApi items;
    HybridSdkInventoryApi inventory;
    HybridSdkEntityApi entities;
    HybridSdkGuiApi gui;
    HybridSdkRenderingApi rendering;
    HybridSdkInputApi input;
    HybridSdkSchedulerApi scheduler;
    HybridSdkLoggingApi logging;
    HybridSdkConfigApi config;
    HybridSdkPermissionsApi permissions;
    HybridSdkServicesApi services;
    HybridSdkLocalizationApi localization;
    HybridSdkNetworkApi network;
    HybridSdkScoreboardApi scoreboard;
} HybridSdkV1;

/* Optional symbol hybrid_sdk_manifest_v1 adds version and load-order dependencies. */
typedef struct HybridSdkModManifestV1 {
    uint32_t abi;
    uint32_t size;
    const char* mod_id;
    const char* version;
    const char* const* dependencies;
    size_t dependency_count;
} HybridSdkModManifestV1;

#ifdef __cplusplus
}
#endif
