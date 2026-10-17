#include "experimental_hook_self_test.h"

#include <android/log.h>
#include <shadowhook.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
using TestFunction = int (*)(int, int);
constexpr const char* kTag = "HybridMCPE";
std::mutex g_lock;
std::atomic<unsigned> g_proxy_hits{0};
std::atomic<unsigned> g_original_calls{0};
std::atomic<unsigned> g_test_runs{0};
TestFunction g_original = nullptr;
void* g_stub = nullptr;

__attribute__((noinline, used, visibility("hidden"))) int test_target(int left, int right) {
    volatile int result = left + right;
    return result;
}

__attribute__((noinline)) int test_proxy(int left, int right) {
    g_proxy_hits.fetch_add(1, std::memory_order_relaxed);
    const TestFunction original = g_original;
    if (!original) return -1;
    g_original_calls.fetch_add(1, std::memory_order_relaxed);
    return original(left, right);
}

const char* fail(const char* stage, int error) {
    static thread_local char result[160];
    const char* message = error ? shadowhook_to_errmsg(error) : "none";
    std::snprintf(result, sizeof result, "self_hook_test=failed;stage=%s;errno=%d;message=%s",
        stage, error, message ? message : "unknown");
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s", result);
    return result;
}

void log_target_words(const char* stage, const TestFunction target, const uint32_t* expected=nullptr) {
    uint32_t words[4]{};
    std::memcpy(words, reinterpret_cast<const void*>(target), sizeof words);
    const bool matches=!expected||std::memcmp(words,expected,sizeof words)==0;
    __android_log_print(ANDROID_LOG_INFO,kTag,
        "self_hook_code=%s;target=%p;words=%08x,%08x,%08x,%08x;matches_original=%d",
        stage,reinterpret_cast<const void*>(target),words[0],words[1],words[2],words[3],matches);
}
}

extern "C" const char* hybrid_experimental_hook_self_test() {
    std::lock_guard<std::mutex> guard(g_lock);
    if (g_stub) return "self_hook_test=failed;stage=previous_hook_not_cleaned";
    const unsigned run=g_test_runs.fetch_add(1,std::memory_order_relaxed)+1;

    __android_log_print(ANDROID_LOG_INFO, kTag, "self_hook_stage=shadowhook_init_begin");
    const int init = shadowhook_init(SHADOWHOOK_MODE_SHARED, false);
    if (init != 0) return fail("shadowhook_init", init);
    __android_log_print(ANDROID_LOG_INFO, kTag, "self_hook_stage=shadowhook_init_complete;version=%s",
        shadowhook_get_version());

    const TestFunction target = &test_target;
    uint32_t original_words[4]{};
    std::memcpy(original_words,reinterpret_cast<const void*>(target),sizeof original_words);
    log_target_words("before_install",target);
    const int before = target(19, 23);
    if (before != 42) return fail("baseline_call", 0);
    __android_log_print(ANDROID_LOG_INFO, kTag, "self_hook_stage=install_begin;target=%p", reinterpret_cast<void*>(target));
    g_proxy_hits.store(0, std::memory_order_relaxed);
    g_original_calls.store(0, std::memory_order_relaxed);
    g_original = nullptr;
    g_stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&test_proxy),
        reinterpret_cast<void**>(&g_original));
    if (!g_stub || !g_original) {
        const int error = shadowhook_get_errno();
        if (g_stub) {
            const int unhook = shadowhook_unhook(g_stub);
            if (unhook == 0) g_stub = nullptr;
        }
        return fail("install", error);
    }
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "self_hook_stage=install_complete;run=%u;stub=%p;original=%p", run,g_stub,reinterpret_cast<void*>(g_original));
    log_target_words("after_install",target,original_words);

    const int hooked = target(19, 23);
    const unsigned hits = g_proxy_hits.load(std::memory_order_relaxed);
    const unsigned calls = g_original_calls.load(std::memory_order_relaxed);
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "self_hook_stage=invoked;run=%u;return=%d;proxy_hits=%u;original_calls=%u",run,hooked,hits,calls);
    if (hooked != 42 || hits != 1 || calls != 1) {
        const int error = shadowhook_get_errno();
        const int unhook = shadowhook_unhook(g_stub);
        if (unhook == 0) { g_stub = nullptr; g_original = nullptr; }
        return fail("behavior_check", error);
    }

    __android_log_print(ANDROID_LOG_INFO, kTag, "self_hook_stage=unhook_begin;stub=%p", g_stub);
    const int unhook = shadowhook_unhook(g_stub);
    if (unhook != 0) return fail("unhook", shadowhook_get_errno());
    g_stub = nullptr;
    g_original = nullptr;
    log_target_words("after_unhook",target,original_words);
    const int after = target(19, 23);
    const bool clean = after == 42 && g_proxy_hits.load(std::memory_order_relaxed) == 1 &&
        g_original_calls.load(std::memory_order_relaxed) == 1;
    __android_log_print(ANDROID_LOG_INFO, kTag,
        "self_hook_stage=cleanup_complete;return=%d;proxy_hits=%u;original_calls=%u;clean=%d",
        after, g_proxy_hits.load(std::memory_order_relaxed), g_original_calls.load(std::memory_order_relaxed), clean);
    if (!clean) return fail("post_unhook_check", 0);
    return "self_hook_test=passed;proxy_hits=1;original_calls=1;return=42;cleanup=verified";
}
