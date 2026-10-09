#include "runtime.hpp"
#include <jni.h>
#include <android/log.h>
#include <memory>
namespace {
std::unique_ptr<bf::Runtime> runtime;
void stop(){runtime.reset();}
std::string string(JNIEnv* env,jstring value){
 if(!value)throw std::runtime_error("Missing runtime path");
 const char* bytes=env->GetStringUTFChars(value,nullptr);if(!bytes)throw std::runtime_error("JNI string allocation failed");
 std::string result(bytes);env->ReleaseStringUTFChars(value,bytes);return result;
}
}
extern "C" JNIEXPORT void JNICALL Java_org_bedrockforge_host_HostNative_start(JNIEnv* env,jclass,jstring data){
 try {
  if(runtime)return;
  auto root=std::filesystem::path(string(env,data));
  runtime=std::make_unique<bf::Runtime>(root,false);runtime->start({});
  __android_log_print(ANDROID_LOG_INFO,"BedrockForge","Framework runtime started; mod profile discovery is not integrated yet");
 } catch(const std::exception& e){stop();env->ThrowNew(env->FindClass("java/lang/IllegalStateException"),e.what());}
}
extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*,void*){stop();}
