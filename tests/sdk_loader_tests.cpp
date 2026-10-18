#include "hybrid_mod.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    check(argc == 3, "two native example module paths");
    check(hybrid_load_mod(argv[2]) == -9, "dependent mod rejected before dependency");
    check(std::strcmp(hybrid_probe(), "mod_dependency_missing:load_dependencies_first") == 0,
          "missing dependency status");
    check(hybrid_load_mod(argv[1]) == 0, "event example loads");
    check(hybrid_load_mod(argv[2]) == 0, "player/world example loads after dependency");
    check(hybrid_disable_mods() == 0, "SDK example mods stop cleanly");
    check(hybrid_enable_mods() == 0, "SDK example mods restart cleanly");
    hybrid_shutdown();
    check(std::strcmp(hybrid_probe(), "engine_verified:1.26.52.3;layout_validated=5;profile=isolated_test_stub;capabilities=0") == 0,
          "SDK loader test shuts down fixture");
    std::puts("PASS: dependent native examples enforce load order and retain ABI 1 lifecycle");
}
