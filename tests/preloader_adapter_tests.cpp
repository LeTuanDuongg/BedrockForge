#include "adapter.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

namespace pl::mod {
class ModContext {};
}

namespace {
using RegistrationGetter = pl::mod::ModRegistration* (*)();
using ProbeSetter = void (*)(const char*);
using ThrowSetter = void (*)(bool);
using ResultSetter = void (*)(int);
using ShutdownCount = int (*)();
using LifecycleCount = int (*)();

void check(bool value, const char* what) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", what);
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
    check(argc == 2, "test module path argument");
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const char* open_error = module ? nullptr : dlerror();
    check(module != nullptr, open_error ? open_error : "dlopen test module");

    auto get_registration = symbol<RegistrationGetter>(module, "PLGetModRegistration");
    auto set_probe = symbol<ProbeSetter>(module, "hybrid_test_set_probe");
    auto set_throw = symbol<ThrowSetter>(module, "hybrid_test_set_throw");
    auto shutdown_count = symbol<ShutdownCount>(module, "hybrid_test_shutdown_count");
    auto set_enable_result = symbol<ResultSetter>(module, "hybrid_test_set_enable_result");
    auto set_disable_result = symbol<ResultSetter>(module, "hybrid_test_set_disable_result");
    auto enable_count = symbol<LifecycleCount>(module, "hybrid_test_enable_count");
    auto disable_count = symbol<LifecycleCount>(module, "hybrid_test_disable_count");

    auto* registration = get_registration();
    check(registration != nullptr, "registration returned");
    check(registration == get_registration(), "registration stable across calls");
    check(registration->instance != nullptr, "registration instance");
    check(registration->load && registration->enable && registration->disable &&
          registration->unload, "all lifecycle callbacks present");
    check(sizeof(*registration) == 40, "ARM64 registration layout");

    pl::mod::ModContext context;
    set_probe("engine_absent");
    check(!registration->load(registration->instance, context), "reject absent engine");
    check(!registration->enable(registration->instance, context), "reject enable before load");

    constexpr char verified[] =
        "engine_verified:1.26.52.3;layout_validated=5;capabilities=0";
    set_probe(verified);
    check(registration->load(registration->instance, context), "load verified profile");
    check(!registration->load(registration->instance, context), "reject duplicate load");
    set_enable_result(-1);
    check(!registration->enable(registration->instance, context), "propagate mod resume failure");
    check(enable_count() == 1, "enable dispatch count after failure");
    set_enable_result(0);
    check(registration->enable(registration->instance, context), "enable loaded runtime");
    check(!registration->enable(registration->instance, context), "reject duplicate enable");
    check(enable_count() == 2, "duplicate enable does not dispatch");

    set_probe("mods_enabled");
    set_disable_result(-1);
    check(!registration->disable(registration->instance, context), "propagate mod suspend failure");
    set_disable_result(0);
    check(registration->disable(registration->instance, context), "disable active runtime");
    check(disable_count() == 2, "disable dispatch and retry counts");
    check(shutdown_count() == 0, "disable suspends without unloading");
    set_probe("mods_disabled");
    check(registration->enable(registration->instance, context), "reenable after lifecycle status change");
    check(registration->unload(registration->instance, context), "unload active runtime");
    check(shutdown_count() == 1, "unload active runtime calls shutdown once");
    check(!registration->unload(registration->instance, context), "reject duplicate unload");

    set_probe(verified);
    set_throw(true);
    check(!registration->load(registration->instance, context), "contain probe exception");
    set_throw(false);
    check(registration->load(registration->instance, context), "load after callback error");
    check(registration->unload(registration->instance, context), "unload inactive runtime");
    check(shutdown_count() == 2, "inactive unload shuts down exactly once");
    check(!registration->load(nullptr, context), "reject null instance");

    check(dlclose(module) == 0, "close test module");
    std::puts("PASS: pinned Preloader registration ABI and lifecycle harness. Mock engine only; no live Preloader or Minecraft execution claimed.");
}
