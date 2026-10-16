#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace hybrid {
struct EngineVerification {
    bool verified=false;
    uint64_t capabilities=0;
    std::string status="engine_absent";
};

// Verifies the loaded engine image and known layouts. No engine methods are called.
EngineVerification verify_loaded_engine(const char* target_sha256,size_t target_size);
}
