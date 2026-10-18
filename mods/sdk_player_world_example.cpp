#include "hybrid_mod.h"

#include <cstdio>

namespace {
constexpr const char* dependencies[]{"hybridmcpe:sdk-events"};
int player_status = HYBRID_SDK_UNSUPPORTED;
int world_status = HYBRID_SDK_UNSUPPORTED;

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI || host->size < sizeof(HybridHost) ||
        !host->sdk || host->sdk->abi != HYBRID_SDK_ABI_V1 ||
        host->sdk->size < sizeof(HybridSdkV1)) return -1;
    HybridPlayerSnapshot player{};
    player.abi = HYBRID_SDK_ABI_V1;
    player.size = sizeof(HybridPlayerSnapshot);
    HybridWorldSnapshot world{};
    world.abi = HYBRID_SDK_ABI_V1;
    world.size = sizeof(HybridWorldSnapshot);
    player_status = host->sdk->player.get_local(&player);
    world_status = host->sdk->world.get_current(&world);
    if (host->sdk->logging.write(HYBRID_SDK_LOG_INFO, "SDKExample",
            "Player/World snapshots are unavailable until a verified client binding exists") != HYBRID_SDK_OK)
        std::printf("sdk_player_world player=%d world=%d\n", player_status, world_status);
    return 0;
}

void stop() {}

const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybridmcpe:sdk-player-world-example",
                    0, start, stop};
const HybridSdkModManifestV1 manifest{
    HYBRID_SDK_ABI_V1, sizeof(HybridSdkModManifestV1), mod.id, "1.0.0",
    dependencies, sizeof(dependencies) / sizeof(dependencies[0])};
}

extern "C" __attribute__((visibility("default"))) const HybridMod* hybrid_mod_entry() {
    return &mod;
}
extern "C" __attribute__((visibility("default"))) const HybridSdkModManifestV1*
hybrid_sdk_manifest_v1() { return &manifest; }
