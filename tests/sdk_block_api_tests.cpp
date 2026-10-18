// Public SDK boundary fixture ONLY. This does not validate Minecraft integration.
#include "sdk_runtime.h"
#include "block_read.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
int32_t backend_status = HYBRID_SDK_OK;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
namespace hybrid {
bool engine_block_read_supported() { return true; }
int32_t engine_block_find(const char* identifier, EngineBlockRead& out) {
    if (backend_status != HYBRID_SDK_OK) return backend_status;
    std::snprintf(out.identifier, sizeof out.identifier, "%s", identifier);
    out.numeric_id = 4;
    out.state_count = 1;
    out.state_hash = 0x12345678;
    out.hash_assigned = 1;
    return HYBRID_SDK_OK;
}
}
int main() {
    const auto* sdk = hybrid::sdk_api_v1();
    check(sdk->capabilities & HYBRID_SDK_CAP_BLOCKS, "backend enables read capability");
    char ns[] = "minecraft", name[] = "cobblestone";
    const HybridIdentifier id{ns, name};
    HybridBlockSnapshot block{};
    block.abi = HYBRID_SDK_ABI_V1; block.size = sizeof block;
    check(sdk->blocks.find(&id, &block) == HYBRID_SDK_OK, "copied backend result");
    check(block.size == sizeof block && block.type_numeric_id == 4 && block.state_count == 1 &&
          block.default_state_hash == 0x12345678 && block.default_state_hash_assigned == 1,
          "default-state metadata copied");
    check(block.state_handle != 0, "opaque nonzero token");
    const auto first = block;
    ns[0] = 'x'; name[0] = 'x';
    check(!std::strcmp(first.identifier.namespace_name, "minecraft") &&
          !std::strcmp(first.identifier.name, "cobblestone"), "strings do not borrow caller buffers");
    const HybridIdentifier stone{"minecraft", "stone"};
    check(sdk->blocks.find(&stone, &block) == HYBRID_SDK_OK && block.state_handle != first.state_handle,
          "snapshot tokens not engine addresses or stable IDs");
    check(!std::strcmp(first.identifier.name, "cobblestone"), "earlier snapshot strings retained");
    struct OldSnapshot {
        uint32_t abi, size;
        HybridIdentifier identifier;
        uint64_t handle;
    };
    static_assert(sizeof(OldSnapshot) == offsetof(HybridBlockSnapshot, type_numeric_id));
    struct LegacyBuffer { OldSnapshot snapshot; unsigned char canary[32]; } legacy{};
    legacy.snapshot.abi = HYBRID_SDK_ABI_V1; legacy.snapshot.size = sizeof(OldSnapshot);
    std::memset(legacy.canary, 0xa5, sizeof legacy.canary);
    check(sdk->blocks.find(&stone, reinterpret_cast<HybridBlockSnapshot*>(&legacy.snapshot)) == HYBRID_SDK_OK,
          "old snapshot prefix still supported");
    for (auto byte : legacy.canary) check(byte == 0xa5, "no write beyond caller snapshot size");
    for (const auto failure : {HYBRID_SDK_NOT_FOUND, HYBRID_SDK_UNSUPPORTED}) {
        backend_status = failure;
        const auto before = block;
        check(sdk->blocks.find(&stone, &block) == failure, "backend failures propagated");
        check(!std::memcmp(&before, &block, sizeof block), "failure output unchanged");
    }
    HybridBlockDescriptor descriptor{};
    check(sdk->blocks.register_block(&descriptor) == HYBRID_SDK_UNSUPPORTED,
          "read capability never enables registration");
    std::puts("PASS: SDK Block boundary fixture (NOT engine integration): metadata, strings, old ABI, tokens, failures");
}
