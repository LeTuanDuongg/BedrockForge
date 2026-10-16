#include <jni.h>
#include <string>
#include <android/log.h>
#include <unistd.h>
#include <dlfcn.h>
#include "engine_profiles.h"
extern "C" const char* hybrid_probe();
extern "C" int hybrid_load_mod(const char*);
extern "C" int hybrid_enable_mods();
extern "C" int hybrid_disable_mods();
extern "C" void hybrid_shutdown();
extern "C" int hybrid_load_mod(const char*);
extern "C" const char* hybrid_experimental_candidate_trace_start();
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeProbe(JNIEnv* env, jclass) {
    return env->NewStringUTF(hybrid_probe());
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeProfile(JNIEnv* env, jclass, jstring hash, jlong size, jint machine) {
    if (!hash || size < 0 || machine != hybrid::elf_machine_aarch64) return env->NewStringUTF("");
    const char* text = env->GetStringUTFChars(hash, nullptr);
    if (!text) return nullptr;
    const auto* profile = hybrid::select_engine_profile(text, static_cast<size_t>(size), static_cast<uint16_t>(machine));
    env->ReleaseStringUTFChars(hash, text);
    return env->NewStringUTF(profile ? profile->id : "");
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeDiagnostic(JNIEnv* env, jclass, jstring path) {
    std::string before = hybrid_probe();
    if (!path || before.rfind("engine_verified:", 0) != 0) return env->NewStringUTF(before.c_str());
    const char* text = env->GetStringUTFChars(path, nullptr);
    if (!text) return nullptr;
    int code = hybrid_load_mod(text);
    env->ReleaseStringUTFChars(path, text);
    std::string result = before + ";diagnostic_load=" + std::to_string(code);
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "native_diagnostic %s", result.c_str());
    return env->NewStringUTF(result.c_str());
}
extern "C" JNIEXPORT jint JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeDiagnosticLifecycle(JNIEnv*, jclass, jboolean active) {
    return active ? hybrid_enable_mods() : hybrid_disable_mods();
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeDiagnosticShutdown(JNIEnv* env, jclass) {
    hybrid_shutdown();
    return env->NewStringUTF(hybrid_probe());
}
extern "C" JNIEXPORT jboolean JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeExperimentalEnabled(JNIEnv*, jclass) {
#ifdef HYBRID_EXPERIMENTAL_NATIVE
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeExperimentalRegistryProbe(JNIEnv* env, jclass, jstring path) {
#ifndef HYBRID_EXPERIMENTAL_NATIVE
    (void)path;
    return env->NewStringUTF("experimental_module_load=disabled");
#else
    if (!path) return env->NewStringUTF("experimental_module_load=failed;reason=missing_path");
    const char* text = env->GetStringUTFChars(path, nullptr);
    if (!text) return nullptr;
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "experimental_module_load=begin;pid=%d;tid=%d", getpid(), gettid());
    const int code = hybrid_load_mod(text);
    env->ReleaseStringUTFChars(path, text);
    const std::string result = std::string("experimental_module_load=") + std::to_string(code);
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s", result.c_str());
    return env->NewStringUTF(result.c_str());
#endif
}
extern "C" JNIEXPORT jint JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeExperimentalRegistryTrigger(JNIEnv*, jclass) {
#ifdef HYBRID_EXPERIMENTAL_NATIVE
    void* module=dlopen("libhybrid_experimental_registry_probe.so",RTLD_NOW|RTLD_NOLOAD);
    if(!module)return -4;
    auto trigger=reinterpret_cast<int (*)()>(dlsym(module,"hybrid_experimental_trigger_registry_sample"));
    const int result=trigger?trigger():-4;
    dlclose(module);
    return result;
#else
    return -4;
#endif
}
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeExperimentalCandidateTraceStart(JNIEnv* env, jclass) {
#ifdef HYBRID_EXPERIMENTAL_NATIVE
    const char* result = hybrid_experimental_candidate_trace_start();
#else
    const char* result = "candidate_trace=disabled";
#endif
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE", "%s", result);
    return env->NewStringUTF(result);
}
#ifndef HYBRID_EXPERIMENTAL_NATIVE
extern "C" const char* hybrid_experimental_candidate_trace_start() { return "candidate_trace=disabled"; }
extern "C" const char* hybrid_experimental_candidate_trace_stop() { return "candidate_trace=disabled"; }
extern "C" const char* hybrid_experimental_hook_self_test() { return "self_hook_test=disabled"; }
extern "C" const char* hybrid_experimental_engine_candidate_hook_start() { return "engine_candidate_hook=disabled"; }
extern "C" const char* hybrid_experimental_engine_candidate_hook_stop() { return "engine_candidate_hook=disabled"; }
#endif
