#include "hybrid_mod.h"

namespace {
int starts = 0;
int stops = 0;
bool fail_stop = false;

int start(const HybridHost* host) {
    if (!host || host->abi != HYBRID_ABI) return -1;
    ++starts;
    return 0;
}

void stop() {
    ++stops;
    if (fail_stop) throw 1;
}

const HybridMod mod{HYBRID_ABI, sizeof(HybridMod), "hybrid:test-stop-failure", 0, start, stop};
}

extern "C" const HybridMod* hybrid_mod_entry() { return &mod; }

extern "C" __attribute__((visibility("default"))) void hybrid_test_fail_stop(bool value) {
    fail_stop = value;
}

extern "C" __attribute__((visibility("default"))) int hybrid_test_starts() { return starts; }
extern "C" __attribute__((visibility("default"))) int hybrid_test_stops() { return stops; }
