#include "engine_bridge.h"
#include <cstring>
#include <string>

namespace {
struct BlockDescriptor {
    const char* identifier;
};

[[maybe_unused]] constexpr BlockDescriptor target_block{"hybridmcpe"};

std::string probe() {
#ifndef HYBRID_EXPERIMENTAL_BLOCK_REGISTRATION
    return "block_registration=disabled;reason=build_option_off;identifier=hybridmcpe;engine_calls=0;memory_writes=0";
#else
    const auto verified = hybrid::verify_loaded_engine();
    if (!verified.verified || !verified.profile)
        return "block_registration=blocked;reason=engine_not_verified;identifier=hybridmcpe;engine_calls=0;memory_writes=0";
    if (std::strcmp(verified.profile->id, "bedrock-1.26.50.4-arm64") != 0)
        return std::string("block_registration=blocked;reason=unsupported_profile;identifier=hybridmcpe;profile=") +
            verified.profile->id + ";engine_calls=0;memory_writes=0";
    return std::string("block_registration=blocked;reason=constructor_and_state_contract_unverified;identifier=") +
        target_block.identifier + ";profile=" + verified.profile->id + ";sha256=" + verified.profile->sha256 +
        ";descriptor=prepared;construction=unavailable;registry_insert=unavailable;states=unavailable;palette=unavailable;engine_calls=0;memory_writes=0";
#endif
}
}

extern "C" const char* hybrid_experimental_block_registration_126504_probe() {
    thread_local std::string result;
    try {
        result = probe();
        return result.c_str();
    } catch (...) {
        return "block_registration=blocked;reason=diagnostic_exception;identifier=hybridmcpe;engine_calls=0;memory_writes=0";
    }
}
