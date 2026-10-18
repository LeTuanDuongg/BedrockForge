// Public SDK boundary fixture ONLY. This does not validate Minecraft integration.
#include "sdk_runtime.h"
#include "block_read.h"
#include "world_read.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
bool world_supported = true;
int32_t world_status = HYBRID_SDK_OK;
int32_t current_status = HYBRID_SDK_OK;
uint64_t served_world_id = 0x0000000700000000ull;
int32_t served_x = 0, served_y = 0, served_z = 0;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
namespace hybrid {
bool engine_block_read_supported() { return true; }
int32_t engine_block_find(const char* identifier, EngineBlockRead& out) {
    std::snprintf(out.identifier, sizeof out.identifier, "%s", identifier);
    out.numeric_id = 4;
    out.state_count = 1;
    out.state_hash = 0x12345678;
    out.hash_assigned = 1;
    return HYBRID_SDK_OK;
}
bool engine_world_read_supported() { return world_supported; }
int32_t engine_world_current(EngineWorldRef& out) {
    if (current_status != HYBRID_SDK_OK) return current_status;
    out.world_id = served_world_id;
    out.dimension_id = static_cast<int32_t>(served_world_id & 0xffffffffu);
    return HYBRID_SDK_OK;
}
int32_t engine_world_block(uint64_t world_id, int32_t x, int32_t y, int32_t z,
                           EngineWorldBlock& out) {
    served_world_id = world_id;
    served_x = x; served_y = y; served_z = z;
    if (world_status != HYBRID_SDK_OK) return world_status;
    std::snprintf(out.identifier, sizeof out.identifier, "minecraft:bedrock");
    out.numeric_id = 7;
    out.state_count = 1;
    out.state_hash = 0x0f0f0f0f;
    out.hash_assigned = 1;
    out.dimension_id = static_cast<int32_t>(world_id & 0xffffffffu);
    return HYBRID_SDK_OK;
}
}
int main() {
    const auto* sdk = hybrid::sdk_api_v1();
    check(sdk->capabilities & HYBRID_SDK_CAP_WORLD_READ, "backend enables world read capability");
    check(sdk->capabilities & HYBRID_SDK_CAP_BLOCKS, "block read capability still published alongside");
    check(sdk->world.set_block(served_world_id, 0, 0, 0, nullptr) ==
          HYBRID_SDK_INVALID_ARGUMENT, "write path still refuses null identifier");
    const HybridIdentifier stone{"minecraft", "stone"};
    check(sdk->world.set_block(served_world_id, 0, 0, 0, &stone) == HYBRID_SDK_UNSUPPORTED,
          "world write remains unsupported");
    uint64_t time = 0;
    check(sdk->world.get_time(served_world_id, &time) == HYBRID_SDK_UNSUPPORTED,
          "world time read remains unsupported");

    HybridWorldSnapshot world{};
    world.abi = HYBRID_SDK_ABI_V1; world.size = sizeof world;
    check(sdk->world.get_current(&world) == HYBRID_SDK_OK, "world handle copied");
    check(world.world_id == served_world_id && world.dimension_id == 0 &&
          world.game_tick == 0 && world.flags == 0, "world snapshot fields");
    HybridWorldSnapshot bad{};
    bad.abi = HYBRID_SDK_ABI_V1; bad.size = sizeof(HybridWorldSnapshot) - 1;
    check(sdk->world.get_current(&bad) == HYBRID_SDK_INVALID_ARGUMENT, "undersized world out refused");
    check(sdk->world.get_current(nullptr) == HYBRID_SDK_INVALID_ARGUMENT, "null world out refused");

    HybridBlockSnapshot block{};
    block.abi = HYBRID_SDK_ABI_V1; block.size = sizeof block;
    check(sdk->world.get_block(world.world_id, 3, -60, -7, &block) == HYBRID_SDK_OK,
          "world block copied");
    check(served_x == 3 && served_y == -60 && served_z == -7, "integer coordinates forwarded");
    check(!std::strcmp(block.identifier.namespace_name, "minecraft") &&
          !std::strcmp(block.identifier.name, "bedrock") && block.type_numeric_id == 7 &&
          block.state_count == 1 && block.default_state_hash == 0x0f0f0f0f &&
          block.default_state_hash_assigned == 1, "block metadata copied");
    check(block.state_handle != 0, "opaque nonzero token");
    const HybridIdentifier cobblestone{"minecraft", "cobblestone"};
    check(sdk->blocks.find(&cobblestone, &block) == HYBRID_SDK_OK,
          "registry read still served");
    check(block.state_handle != 0, "registry snapshot token retained");

    // The stored key is the engine identifier; the split is published, not borrowed.
    const auto first = block;
    char ns[] = "minecraft", name[] = "cobblestone";
    const HybridIdentifier caller{ns, name};
    HybridBlockSnapshot owned{};
    owned.abi = HYBRID_SDK_ABI_V1; owned.size = sizeof owned;
    check(sdk->world.get_block(world.world_id, 0, -64, 0, &owned) == HYBRID_SDK_OK,
          "world block read for string ownership check");
    ns[0] = 'x'; name[0] = 'x';
    check(!std::strcmp(owned.identifier.namespace_name, "minecraft") &&
          !std::strcmp(owned.identifier.name, "bedrock"), "world strings do not borrow caller buffers");
    check(!std::strcmp(first.identifier.name, "cobblestone"), "earlier snapshot strings retained");
    check(sdk->blocks.find(&caller, &owned) == HYBRID_SDK_OK, "caller identifier still validated");
    owned.abi = HYBRID_SDK_ABI_V1; owned.size = sizeof owned;

    struct OldSnapshot {
        uint32_t abi, size;
        HybridIdentifier identifier;
        uint64_t handle;
    };
    static_assert(sizeof(OldSnapshot) == offsetof(HybridBlockSnapshot, type_numeric_id));
    struct LegacyBuffer { OldSnapshot snapshot; unsigned char canary[32]; } legacy{};
    legacy.snapshot.abi = HYBRID_SDK_ABI_V1; legacy.snapshot.size = sizeof(OldSnapshot);
    std::memset(legacy.canary, 0xa5, sizeof legacy.canary);
    check(sdk->world.get_block(world.world_id, 0, -64, 0,
          reinterpret_cast<HybridBlockSnapshot*>(&legacy.snapshot)) == HYBRID_SDK_OK,
          "old block snapshot prefix still supported by world.get_block");
    for (auto byte : legacy.canary) check(byte == 0xa5, "no write beyond caller snapshot size");

    check(sdk->world.get_block(0, 0, 0, 0, &block) == HYBRID_SDK_INVALID_ARGUMENT,
          "zero world handle refused");
    check(sdk->world.get_block(world.world_id, 0, 0, 0, nullptr) == HYBRID_SDK_INVALID_ARGUMENT,
          "null block out refused");
    for (const auto failure : {HYBRID_SDK_NOT_FOUND, HYBRID_SDK_UNSUPPORTED}) {
        world_status = failure;
        const auto before = block;
        check(sdk->world.get_block(world.world_id, 0, 0, 0, &block) == failure,
              "backend failures propagated");
        check(!std::memcmp(&before, &block, sizeof block), "failure output unchanged");
    }
    world_status = HYBRID_SDK_OK;

    current_status = HYBRID_SDK_UNSUPPORTED;
    world_supported = false;
    const auto* absent = hybrid::sdk_api_v1();
    check(!(absent->capabilities & HYBRID_SDK_CAP_WORLD_READ),
          "unavailable world backend unpublishes the capability");
    check(absent->world.get_current(&world) == HYBRID_SDK_UNSUPPORTED,
          "absent world backend reads unsupported");
    check(absent->world.get_block(1, 0, 0, 0, &block) == HYBRID_SDK_UNSUPPORTED,
          "absent world backend block reads unsupported");
    std::puts("PASS: SDK world boundary fixture (NOT engine integration): capability, coordinates, "
              "strings, old ABI, tokens, failures");
}
