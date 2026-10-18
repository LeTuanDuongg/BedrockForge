#include "experimental_hooks.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

int main() {
    for (unsigned run = 1; run <= 3; ++run) {
        // ShadowHook retires entry trampolines for 15 seconds; allow address reuse.
        if (run > 1) sleep(16);
        const char* result = hybrid_experimental_hook_self_test();
        std::printf("hook_lifecycle run=%u result=%s\n", run, result ? result : "null");
        if (!result || std::strncmp(result, "self_hook_test=passed;", 22) != 0) return 1;
    }
    return 0;
}
