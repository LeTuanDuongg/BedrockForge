#include "hybrid_mod.h"
#include "engine_bridge.h"
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {
struct Loaded {void* handle;const HybridMod* mod;std::string id;bool active;};
struct Runtime {
    std::mutex lock;
    std::vector<Loaded> loaded;
    std::string status="engine_absent";
    bool verified=false;
    HybridHost host{HYBRID_ABI,sizeof(HybridHost),0};
};
// Dependency constructors can arrive before other translation units initialize.
Runtime& runtime(){static Runtime state;return state;}
bool check_engine(Runtime& state){
    const auto result=hybrid::verify_loaded_engine();
    state.status=result.status;state.host.capabilities=result.capabilities;
    return result.verified;
}
bool start_mod(Runtime& state, Loaded& loaded){
    try{return loaded.mod->start(&state.host)==0;}catch(...){return false;}
}
bool stop_mod(Loaded& loaded){
    try{loaded.mod->stop();return true;}catch(...){return false;}
}
}
extern "C" const char* hybrid_probe(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    if(!state.verified)state.verified=check_engine(state);
    // Thread-local storage keeps returned diagnostics valid across other callers.
    thread_local std::string result;result=state.status;return result.c_str();
}
extern "C" int hybrid_load_mod(const char* path){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& status=state.status;
    auto& loaded=state.loaded;
    if(!path||path[0]!='/'){status="mod_path_must_be_absolute";return -1;}
    if(!state.verified)state.verified=check_engine(state);
    if(!state.verified)return -2;
    void* handle=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!handle){status=std::string("mod_dlopen_failed:")+dlerror();return -3;}
    auto fail=[&](int rc,const char* reason){status=reason;dlclose(handle);return rc;};
    auto entry=reinterpret_cast<HybridModEntry>(dlsym(handle,"hybrid_mod_entry"));
    if(!entry)return fail(-4,"mod_entry_missing");
    const HybridMod* mod=entry();
    if(!mod||mod->abi!=HYBRID_ABI||mod->size<sizeof(HybridMod)||!mod->id||!mod->id[0]||!mod->start||!mod->stop)return fail(-4,"mod_abi_invalid");
    if(mod->required_capabilities&~state.host.capabilities)return fail(-5,"mod_blocked:required_engine_capabilities_unavailable");
    for(const auto& prior:loaded)if(prior.id==mod->id)return fail(-6,"mod_duplicate_id");
    Loaded loaded_mod{handle,mod,mod->id,false};
    if(!start_mod(state,loaded_mod))return fail(-7,"mod_start_failed");
    loaded_mod.active=true;
    try{loaded.push_back(std::move(loaded_mod));}
    catch(...){stop_mod(loaded_mod);return fail(-7,"mod_registry_allocation_failed");}
    status="mod_loaded:"+loaded.back().id;return 0;
}
extern "C" int hybrid_enable_mods(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    if(!state.verified&&!check_engine(state))return -1;
    std::vector<Loaded*> started;
    try{started.reserve(state.loaded.size());}catch(...){state.status="mod_resume_allocation_failed";return -2;}
    for(auto& entry:state.loaded){
        if(entry.active)continue;
        if(!start_mod(state,entry)){
            bool rollback_ok=true;
            for(auto it=started.rbegin();it!=started.rend();++it){
                rollback_ok=stop_mod(**it)&&rollback_ok;
                (*it)->active=false;
            }
            state.status=rollback_ok?"mod_resume_failed":"mod_resume_rollback_failed";
            return -3;
        }
        entry.active=true;started.push_back(&entry);
    }
    state.status="mods_enabled";return 0;
}
extern "C" int hybrid_disable_mods(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    bool stopped=true;
    for(auto it=state.loaded.rbegin();it!=state.loaded.rend();++it){
        if(!it->active)continue;
        stopped=stop_mod(*it)&&stopped;
        it->active=false;
    }
    state.status=stopped?"mods_disabled":"mod_stop_failed";
    return stopped?0:-1;
}
extern "C" void hybrid_shutdown(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& loaded=state.loaded;
    for(auto it=loaded.rbegin();it!=loaded.rend();++it){
        if(it->active)stop_mod(*it);
        it->active=false;
        dlclose(it->handle);
    }
    loaded.clear();state.verified=false;state.host.capabilities=0;state.status="runtime_stopped";
}
