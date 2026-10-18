#pragma once

#include "hybrid_sdk.h"

namespace hybrid {
const HybridSdkV1* sdk_api_v1();
void sdk_dispatch_lifecycle(uint32_t event_id, const char* mod_id);
int sdk_check_manifest(const HybridSdkModManifestV1* manifest,
                       const char* const* loaded_ids, size_t loaded_count);
}
