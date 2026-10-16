#pragma once

#include <cstddef>

// Minimal ABI declaration verified against LeviLaunchroid v1.5.24's Preloader
// submodule at 7ca94daedfa30d6d4c265fc9b591768e8dc1f5af. Keep ModContext opaque.
namespace pl::mod {
class ModContext;
using LifecycleFunction = bool (*)(void* instance, ModContext& context);

struct ModRegistration {
    void* instance;
    LifecycleFunction load;
    LifecycleFunction enable;
    LifecycleFunction disable;
    LifecycleFunction unload;
};

static_assert(sizeof(void*) != 8 || sizeof(ModRegistration) == 40);
static_assert(sizeof(void*) != 8 || offsetof(ModRegistration, load) == 8);
static_assert(sizeof(void*) != 8 || offsetof(ModRegistration, unload) == 32);
}

#if defined(__GNUC__) || defined(__clang__)
#define HYBRID_PRELOADER_EXPORT __attribute__((visibility("default")))
#else
#define HYBRID_PRELOADER_EXPORT
#endif

extern "C" HYBRID_PRELOADER_EXPORT pl::mod::ModRegistration*
PLGetModRegistration();
