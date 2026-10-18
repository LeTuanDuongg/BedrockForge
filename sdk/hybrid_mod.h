#pragma once
#include <stdint.h>
#include "hybrid_sdk.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HYBRID_ABI 1u
#define HYBRID_CAP_CONTAINER 1ull
#define HYBRID_CAP_CHEST_UI 2ull
#define HYBRID_CAP_PERSISTENCE 4ull
typedef struct HybridHost {
    uint32_t abi;
    uint32_t size;
    uint64_t capabilities;
    /* Appended ABI 1 field. Older modules ignore bytes beyond their known prefix. */
    const HybridSdkV1* sdk;
} HybridHost;
typedef struct HybridMod {
    uint32_t abi;
    uint32_t size;
    const char* id;
    uint64_t required_capabilities;
    int (*start)(const HybridHost*);
    void (*stop)(void);
} HybridMod;
/* Each plugin exports hybrid_mod_entry. Descriptor and strings stay valid until unload. */
typedef const HybridMod* (*HybridModEntry)(void);
typedef const HybridSdkModManifestV1* (*HybridSdkManifestEntryV1)(void);
const char* hybrid_probe(void);
int hybrid_load_mod(const char* absolute_path);
/* Suspend or resume loaded ABI 1 mods without unloading their libraries. */
int hybrid_enable_mods(void);
int hybrid_disable_mods(void);
void hybrid_shutdown(void);
/* Load codes: 0 loaded; -1 invalid path; -2 engine not verified;
   -3 dlopen failed; -4 missing/invalid ABI or SDK manifest; -5 missing capabilities;
   -6 duplicate ID; -7 start failed; -8 cleanup/shutdown incomplete;
   -9 required SDK dependency is not loaded and active.
   Enable codes: 0 success; -1 engine not verified; -2 allocation failure;
   -3 mod resume failed; -4 cleanup/shutdown incomplete. Disable returns 0
   when all active mods stop, -1 if any stop callback fails. A failed stop
   blocks load/enable; shutdown retains module handles until cleanup succeeds. */
#ifdef __cplusplus
}
#endif
