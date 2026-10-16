#include "adapter.h"
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool value) {
    if (!value) { std::fputs("Preloader adapter check failed\n", stderr); std::exit(1); }
}
}
int main() {
    JavaVM vm{};
    PreloaderMetadata metadata{};
    metadata.size = sizeof(metadata);
    metadata.mod_id = "hybridmcpe";
    metadata.library_path = "/test/libhybridmcpe.so";
    check(!PLMod_Load(nullptr, &metadata));
    check(!PLMod_Load(&vm, nullptr));
    metadata.size = 4;
    check(!PLMod_Load(&vm, &metadata));
    metadata.size = sizeof(metadata);
    // No engine is loaded in this test process; starting the runtime must fail.
    check(!PLMod_Load(&vm, &metadata));
    check(!PLMod_Enable());
    check(PLMod_Disable());
    check(PLMod_Unload());
    check(PLMod_Unload());
    std::puts("PASS: C lifecycle ABI, metadata validation, absent-engine rejection, shutdown. No live-engine execution claimed.");
}
