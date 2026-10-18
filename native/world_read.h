#pragma once
#include <cstdint>

namespace hybrid {
struct EngineVerification;
// Called only by the existing runtime after full engine fingerprint verification.
void engine_world_read_configure(const EngineVerification& verification);
// Drops every borrowed engine pointer and removes the observation hook.
void engine_world_read_shutdown();
bool engine_world_read_supported();

// Opaque process-lifetime world token. It is a generation/dimension handle, never an
// engine address, and it is revalidated against the live engine on every read.
struct EngineWorldRef {
    uint64_t world_id = 0;
    int32_t dimension_id = 0;
};
int32_t engine_world_current(EngineWorldRef& out);

// Private copied result; neither BlockType nor Block pointers cross the bridge.
struct EngineWorldBlock {
    char identifier[97]{};
    uint32_t numeric_id = 0;
    uint32_t state_count = 0;
    uint32_t state_hash = 0;
    uint32_t hash_assigned = 0;
    int32_t dimension_id = 0;
};
// x, y, z are world block coordinates. World reads must be serviced by an engine
// thread; the backend defers to the observed engine thread and reports UNSUPPORTED
// when no engine thread services the request in time.
int32_t engine_world_block(uint64_t world_id, int32_t x, int32_t y, int32_t z,
                           EngineWorldBlock& out);
}
