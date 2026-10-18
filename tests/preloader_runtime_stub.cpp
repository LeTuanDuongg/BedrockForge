#include "hybrid_mod.h"

#include <cstdio>

namespace {
char probe_status[320] = "engine_absent";
bool throw_from_probe = false;
int shutdowns = 0;
int enables = 0;
int disables = 0;
int enable_result = 0;
int disable_result = 0;
}

extern "C" const char* hybrid_probe() {
    if (throw_from_probe) throw 7;
    return probe_status;
}

extern "C" void hybrid_shutdown() {
    ++shutdowns;
}

extern "C" int hybrid_enable_mods() {
    ++enables;
    return enable_result;
}

extern "C" int hybrid_disable_mods() {
    ++disables;
    return disable_result;
}

extern "C" __attribute__((visibility("default")))
void hybrid_test_set_probe(const char* value) {
    if (!value) value = "";
    std::snprintf(probe_status, sizeof(probe_status), "%s", value);
}

extern "C" __attribute__((visibility("default")))
void hybrid_test_set_throw(bool value) {
    throw_from_probe = value;
}

extern "C" __attribute__((visibility("default")))
int hybrid_test_shutdown_count() {
    return shutdowns;
}

extern "C" __attribute__((visibility("default")))
void hybrid_test_set_enable_result(int value) {
    enable_result = value;
}

extern "C" __attribute__((visibility("default")))
void hybrid_test_set_disable_result(int value) {
    disable_result = value;
}

extern "C" __attribute__((visibility("default")))
int hybrid_test_enable_count() {
    return enables;
}

extern "C" __attribute__((visibility("default")))
int hybrid_test_disable_count() {
    return disables;
}
