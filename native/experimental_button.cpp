#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"
#include <android/log.h>
#include <shadowhook.h>
#include <atomic>
#include <cstring>
#include <link.h>
#include <mutex>
#include <time.h>
#include <unistd.h>

namespace {
constexpr uintptr_t kSchedule = 0xfb8edc8;
constexpr uintptr_t kButtonReturn = 0x10114c44;
constexpr uintptr_t kButtonVtable = 0x130dbf68;
std::mutex control_lock;
std::atomic<bool> modify{false};
std::atomic<unsigned> observations{0};
std::atomic<unsigned> calls{0};
uintptr_t engine_base = 0;
void* stub = nullptr;
void* original = nullptr;

constexpr unsigned delay(bool enabled, bool selected, unsigned ticks) {
    return enabled && selected && (ticks == 20 || ticks == 30) ? 200 : ticks;
}
static_assert(delay(true, true, 20) == 200 && delay(true, true, 30) == 200 &&
    delay(false, true, 20) == 20 && delay(false, true, 30) == 30 && delay(true, false, 20) == 20);

uint64_t now() {
    timespec t{}; clock_gettime(CLOCK_BOOTTIME, &t);
    return uint64_t(t.tv_sec)*1000000000ULL+t.tv_nsec;
}

// Six register arguments are consumed by the complete scheduler FDE; no x8/sret or FP inputs.
uintptr_t schedule_proxy(uintptr_t region, uintptr_t position, uintptr_t state,
        unsigned ticks, unsigned flag4, unsigned flag5) {
    SHADOWHOOK_STACK_SCOPE();
    const uintptr_t caller = reinterpret_cast<uintptr_t>(SHADOWHOOK_RETURN_ADDRESS());
    if (calls.fetch_add(1, std::memory_order_relaxed) < 16)
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=scheduler_entry;pid=%d;tid=%d;caller_rva=0x%llx;ticks=%u",
            getpid(), gettid(), static_cast<unsigned long long>(caller-engine_base), ticks);
    bool selected = false;
    uintptr_t block = 0, vtable = 0;
    int coordinates[3]{};
    if (caller == engine_base+kButtonReturn && (ticks == 20 || ticks == 30)) {
        hybrid::SelfMemory memory;
        selected = state <= UINTPTR_MAX-0x68 && memory.read(state+0x68, &block, sizeof block) &&
            memory.read(block, &vtable, sizeof vtable) && vtable == engine_base+kButtonVtable &&
            memory.read(position, coordinates, sizeof coordinates);
    }
    const unsigned replacement = delay(modify.load(std::memory_order_acquire), selected, ticks);
    if (selected) {
        const unsigned hit = observations.fetch_add(1, std::memory_order_relaxed)+1;
        if (hit <= 32) __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
            "button_hook=hit;pid=%d;tid=%d;elapsed_ns=%llu;caller_rva=0x10114c44;state=%p;block=%p;vtable=%p;pos=%d,%d,%d;before=%u;after=%u;flags=%u,%u;hit=%u",
            getpid(), gettid(), static_cast<unsigned long long>(now()), reinterpret_cast<void*>(state),
            reinterpret_cast<void*>(block), reinterpret_cast<void*>(vtable), coordinates[0], coordinates[1],
            coordinates[2], ticks, replacement, flag4, flag5, hit);
    }
    const uintptr_t result = SHADOWHOOK_CALL_PREV(schedule_proxy, region, position, state, replacement, flag4, flag5);
    if (selected && observations.load(std::memory_order_relaxed) <= 32)
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=original_returned;pid=%d;tid=%d;elapsed_ns=%llu",
            getpid(), gettid(), static_cast<unsigned long long>(now()));
    return result;
}

int locate(dl_phdr_info* info, size_t, void* output) {
    const char* name = info->dlpi_name ? std::strrchr(info->dlpi_name, '/') : nullptr;
    if (!info->dlpi_name || std::strcmp(name ? name+1 : info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& image = *static_cast<hybrid::EngineImage*>(output);
    image.base = info->dlpi_addr;
    for (unsigned i=0; i<info->dlpi_phnum; ++i) {
        const auto& p = info->dlpi_phdr[i];
        uintptr_t address = 0;
        if (p.p_type == PT_LOAD && image.address(p.p_vaddr, address))
            image.segments.push_back({address, static_cast<size_t>(p.p_memsz), p.p_flags});
    }
    return 1;
}
}

// Private experimental commands: observe, extend stone-button delay, disable/cleanup.
extern "C" int hybrid_experimental_button_control(unsigned operation) {
    std::lock_guard<std::mutex> lock(control_lock);
    if (operation > 2) return -1;
    if (operation == 2) {
        modify.store(false, std::memory_order_release);
        if (!stub) return 0;
        const int result = shadowhook_unhook(stub);
        if (!result) { stub = nullptr; original = nullptr; }
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=cleanup;result=%d;pid=%d;hits=%u;retained=%d",
            result, getpid(), observations.load(), stub != nullptr);
        return result;
    }
    if (operation == 1) {
        if (!stub || !observations.load(std::memory_order_acquire)) return -2;
        modify.store(true, std::memory_order_release);
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=modification_enabled;pid=%d;ticks=200;elapsed_ns=%llu",
            getpid(), static_cast<unsigned long long>(now()));
        return 0;
    }
    modify.store(false, std::memory_order_release);
    if (stub) return 0;
    const auto verified = hybrid::verify_loaded_engine();
    if (!verified.verified || !verified.profile || std::strcmp(verified.profile->id, "bedrock-1.26.50.4-arm64")) return -3;
    hybrid::EngineImage image;
    dl_iterate_phdr(locate, &image);
    if (!image.base || !image.contains(image.base+kSchedule, 16, PF_R|PF_X) ||
            !image.contains(image.base+kButtonReturn-4, 4, PF_R|PF_X) ||
            !image.contains(image.base+kButtonVtable, 8, PF_R)) return -4;
    hybrid::SelfMemory memory;
    const unsigned expected[4]{0xd10183ff, 0xa9017bfd, 0xf90013f9, 0xa9035ff8};
    unsigned bytes[4]{}, call = 0;
    if (!memory.read(image.base+kSchedule, bytes, sizeof bytes) || std::memcmp(bytes, expected, sizeof bytes) ||
            !memory.read(image.base+kButtonReturn-4, &call, 4) || call != 0x97e9e862) return -5;
    engine_base = image.base;
    observations.store(0);
    calls.store(0);
    const int init = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init) return -6;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=install_begin;pid=%d;tid=%d;target=%p;engine_sha256=fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1",
        getpid(), gettid(), reinterpret_cast<void*>(engine_base+kSchedule));
    stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(engine_base+kSchedule),
        reinterpret_cast<void*>(&schedule_proxy), &original);
    const int result = stub && original ? 0 : -7;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "button_hook=install_complete;result=%d;errno=%d;pid=%d;modification=0",
        result, shadowhook_get_errno(), getpid());
    return result;
}
