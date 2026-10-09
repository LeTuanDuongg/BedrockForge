#ifndef BEDROCKFORGE_H
#define BEDROCKFORGE_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
#define BF_EXPORT __declspec(dllexport)
#else
#define BF_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define BF_API_VERSION 2u
typedef uint64_t bf_handle;
typedef int32_t bf_result;
enum { BF_OK=0, BF_INVALID=1, BF_NOT_FOUND=2, BF_CONFLICT=3,
       BF_UNSUPPORTED=4, BF_IO=5, BF_DISABLED=6, BF_INTERNAL=7 };
/* All strings UTF-8, bounded and NUL terminated. Values copied by host.
   Handle IDs are scoped to the mod session. Calls occur on the host thread.
   No game memory addresses or C++ types cross this boundary. */
typedef struct { char id[128], name[128], category[64], icon[256]; uint32_t max_stack; } bf_item;
typedef struct { char item[128], metadata[256]; uint32_t count; } bf_stack;
typedef struct { char id[128], provider[64]; bf_stack output; uint32_t ingredient_count; bf_stack ingredients[9]; } bf_recipe;
typedef struct { bf_handle handle; char identity[128]; uint32_t slots; uint64_t revision; } bf_container;
typedef void (*bf_event_fn)(void* user, const char* topic, const char* payload);
typedef struct bf_api {
  uint32_t size, version;
  void* context; /* opaque host cookie; never forwarded to scripts */
  bf_result (*capability)(void*, const char*);
  void (*log)(void*, const char*);
  bf_result (*register_item)(void*, const bf_item*);
  bf_result (*item_at)(void*, uint32_t, bf_item*);
  bf_result (*register_recipe)(void*, const bf_recipe*);
  bf_result (*recipe_at)(void*, uint32_t, bf_recipe*);
  bf_result (*create_container)(void*, const char* key, uint32_t slots, bf_container*);
  bf_result (*container_info)(void*, bf_handle, bf_container*);
  bf_result (*read_slot)(void*, bf_handle, uint32_t slot, bf_stack*);
  /* Deposit/withdraw are logical-only services, never mint real game items. */
  bf_result (*deposit)(void*, bf_handle, uint32_t, const bf_stack*, uint64_t expected_revision);
  bf_result (*withdraw)(void*, bf_handle, uint32_t, uint32_t, uint64_t, bf_stack*);
  bf_result (*transfer)(void*, bf_handle, uint32_t, uint64_t, bf_handle, uint32_t, uint64_t, uint32_t);
  bf_result (*subscribe)(void*, const char*, bf_event_fn, void*, bf_handle*);
  bf_result (*unsubscribe)(void*, bf_handle);
  bf_result (*publish)(void*, const char*, const char*);
  bf_result (*ui_open)(void*, const char* view_id, const char* model);
  bf_result (*message)(void*, const char* destination, const char* payload);
} bf_api;
typedef struct {
  uint32_t size, api_version;
  const char* id;
  const char* version;
  const char* const* dependencies;
  uint32_t dependency_count;
  bf_result (*load)(const bf_api*);
  void (*unload)(void);
} bf_mod;
typedef const bf_mod* (*bf_mod_entry_fn)(void);
/* ABI v2 supports multiple required dependencies. Exceptions must not escape. */
BF_EXPORT const bf_mod* bf_mod_entry(void);
#ifdef __cplusplus
}
#endif
#endif
