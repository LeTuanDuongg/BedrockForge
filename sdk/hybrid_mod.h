#pragma once
#include <stdint.h>
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
const char* hybrid_probe(void);
int hybrid_load_mod(const char* absolute_path);
void hybrid_shutdown(void);
/* Return codes: 0 loaded; -1 invalid path; -2 engine not verified;
   -3 dlopen failed; -4 missing/invalid ABI; -5 missing capabilities;
   -6 duplicate ID; -7 start failed. Probe returns diagnostics. */
#ifdef __cplusplus
}
#endif
