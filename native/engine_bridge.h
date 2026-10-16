#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "engine_profiles.h"

namespace hybrid {
struct EngineVerification {
    bool verified=false;
    uint64_t capabilities=0;
    std::string status="engine_absent";
    const EngineProfile* profile=nullptr;
};

// Verifies the loaded engine image and known layouts. No engine methods are called.
EngineVerification verify_loaded_engine();
}
