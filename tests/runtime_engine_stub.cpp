#include "engine_bridge.h"

namespace hybrid {
EngineVerification verify_loaded_engine() {
    return {true, 0, "engine_verified:test;capabilities=0"};
}
}
