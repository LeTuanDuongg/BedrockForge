#include "engine_bridge.h"

namespace hybrid {
EngineVerification verify_loaded_engine() {
    return {true, 0, "engine_verified:1.26.52.3;layout_validated=5;profile=isolated_test_stub;capabilities=0;gameplay_binding=unavailable"};
}
}
