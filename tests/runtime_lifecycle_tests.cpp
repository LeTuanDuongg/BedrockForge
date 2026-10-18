#include "hybrid_mod.h"
#include "adapter.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pl::mod {
class ModContext {};
}

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

template<class T>
T symbol(void* handle, const char* name) {
    dlerror();
    auto* address = dlsym(handle, name);
    check(address != nullptr, name);
    return reinterpret_cast<T>(address);
}
}

int main(int argc, char** argv) {
    check(argc == 2, "test mod path argument");
    void* test_mod = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    check(test_mod != nullptr, "open test mod");
    auto fail_stop = symbol<void (*)(bool)>(test_mod, "hybrid_test_fail_stop");
    auto starts = symbol<int (*)()>(test_mod, "hybrid_test_starts");
    auto stops = symbol<int (*)()>(test_mod, "hybrid_test_stops");

    check(hybrid_load_mod(argv[1]) == 0, "load with isolated verified-engine stub");
    check(starts() == 1, "initial start dispatched once");
    fail_stop(true);
    check(hybrid_disable_mods() == -1, "report failed stop");
    check(stops() == 1, "failed stop dispatched once");
    check(std::strcmp(hybrid_probe(), "runtime_cleanup_incomplete:mod_stop_failed;handles_retained") == 0,
          "failed stop reports cleanup failure");
    check(hybrid_enable_mods() == -4, "failed stop blocks enable");
    check(hybrid_load_mod("relative.so") == -8, "failed stop blocks additional loads");
    check(starts() == 1, "active mod is not started twice");

    fail_stop(false);
    check(hybrid_disable_mods() == 0, "disable retry clears cleanup block");
    check(std::strcmp(hybrid_probe(), "mods_disabled") == 0, "cleanup retry reports disabled state");
    check(hybrid_enable_mods() == 0, "resume after successful cleanup retry");
    check(starts() == 2, "resume starts stopped mod once");

    fail_stop(true);
    hybrid_shutdown();
    check(stops() == 3, "shutdown attempts active stop");
    check(std::strcmp(hybrid_probe(), "runtime_shutdown_incomplete:mod_stop_failed;handles_retained") == 0,
          "failed shutdown retains modules and reports blocked state");
    check(hybrid_enable_mods() == -4, "incomplete shutdown blocks enable");
    check(hybrid_load_mod(argv[1]) == -8, "incomplete shutdown blocks additional loads");

    fail_stop(false);
    hybrid_shutdown();
    check(stops() == 4, "shutdown retry stops retained module");
    check(std::strcmp(hybrid_probe(), "engine_verified:1.26.52.3;layout_validated=5;profile=isolated_test_stub;capabilities=0;gameplay_binding=unavailable") == 0,
          "successful retry completes shutdown and permits fresh verification");

    auto* registration = PLGetModRegistration();
    check(registration != nullptr, "Preloader registration available");
    pl::mod::ModContext context;
    check(registration->load(registration->instance, context), "adapter loads runtime fixture");
    check(hybrid_load_mod(argv[1]) == 0, "reload real mod lifecycle fixture");
    check(registration->enable(registration->instance, context), "adapter enables runtime");
    fail_stop(true);
    check(!registration->unload(registration->instance, context), "adapter rejects failed runtime shutdown");
    check(!registration->unload(registration->instance, context), "permanent stop failure rejects repeated unload");
    check(!registration->load(registration->instance, context), "failed adapter unload retains loaded state");
    check(!registration->enable(registration->instance, context), "failed adapter unload rejects enable");
    check(hybrid_load_mod(argv[1]) == -8, "adapter unload failure keeps runtime blocked");
    check(std::strcmp(hybrid_probe(), "runtime_shutdown_incomplete:mod_stop_failed;handles_retained") == 0,
          "adapter does not overwrite shutdown failure");
    fail_stop(false);
    check(registration->unload(registration->instance, context), "adapter unload retry succeeds after cleanup");
    check(!registration->unload(registration->instance, context), "completed unload rejects duplicate callback");
    check(dlclose(test_mod) == 0, "close test module");
    std::puts("PASS: failed stops retain handles; Preloader unload reports failure and retains state until cleanup succeeds. Isolated engine fixture only.");
}
