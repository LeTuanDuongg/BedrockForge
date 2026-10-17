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
std::string experimental_registry_getter_probe();
// Historical name: this diagnostic now samples memory only; the inferred lookup ABI is not called.
std::string experimental_vanilla_block_lookup_probe();
std::string experimental_registry_lifecycle_snapshot(unsigned phase);
}
extern "C" const char* hybrid_experimental_registry_getter_probe();
extern "C" const char* hybrid_experimental_vanilla_block_lookup_probe();
extern "C" const char* hybrid_experimental_registry_lifecycle_snapshot(unsigned phase);
extern "C" const char* hybrid_experimental_candidate_trace_start();
extern "C" const char* hybrid_experimental_candidate_trace_stop();
