#include <jni.h>
extern "C" const char* hybrid_probe();
extern "C" JNIEXPORT jstring JNICALL
Java_dev_hybridmcpe_launcher_MainActivity_nativeProbe(JNIEnv* env, jclass) {
    return env->NewStringUTF(hybrid_probe());
}
