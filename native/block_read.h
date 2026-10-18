#pragma once
#include <cstdint>

namespace hybrid {
struct EngineVerification;
// Called only by the existing runtime after full engine fingerprint verification.
void engine_block_read_configure(const EngineVerification& verification);
// Private copied result; neither BlockType nor Block pointers cross the bridge.
struct EngineBlockRead {
    char identifier[97]{};
    uint32_t numeric_id = 0;
    uint32_t state_count = 0;
    uint32_t state_hash = 0;
    uint32_t hash_assigned = 0;
};
bool engine_block_read_supported();
int32_t engine_block_find(const char* identifier, EngineBlockRead& out);
}
