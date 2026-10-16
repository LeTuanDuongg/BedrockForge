#include <jni.h>
#include <string>
#include <android/log.h>
#include "engine_profiles.h"
extern "C" const char* hybrid_probe();
extern "C" int hybrid_load_mod(const char*);
extern "C" int hybrid_enable_mods();
extern "C" int hybrid_disable_mods();
extern "C" void hybrid_shutdown();
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
