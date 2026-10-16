#include "hybrid_mod.h"
#include "engine_profile.h"
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {
constexpr auto tag="HybridMCPEGame";
struct EngineModule {bool found=false;};
int record_engine(dl_phdr_info* info,size_t,void* context){
    if(!info->dlpi_name)return 0;
    const char* leaf=std::strrchr(info->dlpi_name,'/');
    if(std::strcmp(leaf?leaf+1:info->dlpi_name,"libminecraftpe.so"))return 0;
    static_cast<EngineModule*>(context)->found=true;
    __android_log_print(ANDROID_LOG_INFO,tag,"event=engine_mapped pid=%d path=%s bias=%p",getpid(),info->dlpi_name,reinterpret_cast<void*>(info->dlpi_addr));
    return 1;
}
void* initialize(void*){
    EngineModule engine;
    // ELF relocations are ready when dependency constructors run; no engine methods are called.
    dl_iterate_phdr(record_engine,&engine);
    if(!engine.found){
        __android_log_print(ANDROID_LOG_ERROR,tag,"event=bootstrap_failed reason=engine_absent pid=%d",getpid());
        return nullptr;
    }
    const char* status=hybrid_probe();
    __android_log_print(ANDROID_LOG_INFO,tag,"event=fingerprint pid=%d expected_sha256=%s status=%s",getpid(),HYBRID_MODIFIED_ENGINE_SHA256,status);
    if(std::strstr(status,"engine_verified:1.26.52.3;layout_validated=5;")!=status)return nullptr;
    Dl_info self{};
    if(!dladdr(reinterpret_cast<void*>(&initialize),&self)||!self.dli_fname)return nullptr;
    std::string path=self.dli_fname;
    auto slash=path.find_last_of('/');
    if(slash==std::string::npos)return nullptr;
    path.resize(slash+1);
    path+="libhybrid_bootstrap_check.so";
    int result=hybrid_load_mod(path.c_str());
    __android_log_print(ANDROID_LOG_INFO,tag,"event=mod_load pid=%d result=%d status=%s",getpid(),result,hybrid_probe());
    if(result!=0)return nullptr;
    int duplicate=hybrid_load_mod(path.c_str());
    __android_log_print(ANDROID_LOG_INFO,tag,"event=duplicate_guard pid=%d result=%d",getpid(),duplicate);
    hybrid_shutdown();
    __android_log_print(ANDROID_LOG_INFO,tag,"event=lifecycle_complete pid=%d status=%s",getpid(),hybrid_probe());
    return nullptr;
}
__attribute__((constructor)) void on_library_loaded(){
    char process[128]{};
    if(FILE* file=std::fopen("/proc/self/cmdline","rb")){
        std::fread(process,1,sizeof(process)-1,file);
        std::fclose(file);
    }
    if(std::strcmp(process,"dev.hybridmcpe.research"))return;
    __android_log_print(ANDROID_LOG_INFO,tag,"event=dependency_constructor pid=%d process=%s",getpid(),process);
    initialize(nullptr);
}
}
