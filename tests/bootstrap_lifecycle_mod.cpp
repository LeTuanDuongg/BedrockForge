#include "hybrid_mod.h"
#include <android/log.h>
#include <unistd.h>

namespace {
int start(const HybridHost* host){
    if(!host||host->abi!=HYBRID_ABI||host->size<sizeof(HybridHost))return -1;
    __android_log_print(ANDROID_LOG_INFO,"HybridMCPEGame","event=mod_start pid=%d abi=%u capabilities=%llu",getpid(),host->abi,static_cast<unsigned long long>(host->capabilities));
    return 0;
}
void stop(){__android_log_print(ANDROID_LOG_INFO,"HybridMCPEGame","event=mod_stop pid=%d",getpid());}
const HybridMod mod{HYBRID_ABI,sizeof(HybridMod),"hybrid:bootstrap_check",0,start,stop};
}
extern "C" const HybridMod* hybrid_mod_entry(){return &mod;}
