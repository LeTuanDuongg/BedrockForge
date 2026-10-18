#include "hybrid_mod.h"
#include "engine_bridge.h"
#include "sdk_runtime.h"
#include "block_read.h"
#include "world_read.h"
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
    bool lifecycle_blocked=false;
    bool shutdown_pending=false;
    HybridHost host{HYBRID_ABI,sizeof(HybridHost),0,nullptr};
    Runtime(){host.sdk=hybrid::sdk_api_v1();}
};
constexpr const char* cleanup_failed="runtime_cleanup_incomplete:mod_stop_failed;handles_retained";
constexpr const char* shutdown_failed="runtime_shutdown_incomplete:mod_stop_failed;handles_retained";
// Dependency constructors can arrive before other translation units initialize.
Runtime& runtime(){static Runtime state;return state;}
bool check_engine(Runtime& state){
    const auto result=hybrid::verify_loaded_engine();
    state.status=result.status;state.host.capabilities=result.capabilities;
    hybrid::engine_block_read_configure(result);
    hybrid::engine_world_read_configure(result);
    state.host.sdk=hybrid::sdk_api_v1();
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
    if(!state.lifecycle_blocked&&!state.verified)state.verified=check_engine(state);
    // Thread-local storage keeps returned diagnostics valid across other callers.
    thread_local std::string result;result=state.status;return result.c_str();
}
extern "C" int hybrid_load_mod(const char* path){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& status=state.status;
    auto& loaded=state.loaded;
    if(state.lifecycle_blocked){status=state.shutdown_pending?shutdown_failed:cleanup_failed;return -8;}
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
    dlerror();
    auto manifest_entry=reinterpret_cast<HybridSdkManifestEntryV1>(dlsym(handle,"hybrid_sdk_manifest_v1"));
    if(manifest_entry){
        const auto* manifest=manifest_entry();
        if(!manifest||manifest->abi!=HYBRID_SDK_ABI_V1||manifest->size<sizeof(HybridSdkModManifestV1)||
           !manifest->mod_id||std::strcmp(manifest->mod_id,mod->id)!=0)
            return fail(-4,"mod_manifest_invalid");
        std::vector<const char*> active_ids;
        try{
            active_ids.reserve(loaded.size());
            for(const auto& prior:loaded)if(prior.active)active_ids.push_back(prior.id.c_str());
        }catch(...){return fail(-7,"mod_dependency_check_allocation_failed");}
        const int dependency_result=hybrid::sdk_check_manifest(manifest,active_ids.data(),active_ids.size());
        if(dependency_result==HYBRID_SDK_DEPENDENCY_MISSING)
            return fail(-9,"mod_dependency_missing:load_dependencies_first");
        if(dependency_result!=HYBRID_SDK_OK)return fail(-4,"mod_manifest_invalid");
    }
    Loaded loaded_mod{handle,mod,mod->id,false};
    if(!start_mod(state,loaded_mod))return fail(-7,"mod_start_failed");
    loaded_mod.active=true;
    try{loaded.push_back(std::move(loaded_mod));}
    catch(...){stop_mod(loaded_mod);return fail(-7,"mod_registry_allocation_failed");}
    hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STARTED,loaded.back().id.c_str());
    status="mod_loaded:"+loaded.back().id;return 0;
}
extern "C" int hybrid_enable_mods(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    if(state.lifecycle_blocked){state.status=state.shutdown_pending?shutdown_failed:cleanup_failed;return -4;}
    if(!state.verified&&!check_engine(state))return -1;
    std::vector<Loaded*> started;
    try{started.reserve(state.loaded.size());}catch(...){state.status="mod_resume_allocation_failed";return -2;}
    for(auto& entry:state.loaded){
        if(entry.active)continue;
        if(!start_mod(state,entry)){
            bool rollback_ok=true;
            for(auto it=started.rbegin();it!=started.rend();++it){
                hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPING,(*it)->id.c_str());
                if(stop_mod(**it)){
                    (*it)->active=false;
                    hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPED,(*it)->id.c_str());
                }
                else rollback_ok=false;
            }
            if(rollback_ok)state.status="mod_resume_failed";
            else{
                state.lifecycle_blocked=true;
                state.status="runtime_cleanup_incomplete:resume_rollback_failed;handles_retained";
            }
            return -3;
        }
        entry.active=true;started.push_back(&entry);
        hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STARTED,entry.id.c_str());
    }
    state.status="mods_enabled";return 0;
}
extern "C" int hybrid_disable_mods(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    bool stopped=true;
    for(auto it=state.loaded.rbegin();it!=state.loaded.rend();++it){
        if(!it->active)continue;
        hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPING,it->id.c_str());
        if(stop_mod(*it))it->active=false;
        else stopped=false;
        if(!it->active)hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPED,it->id.c_str());
    }
    if(!stopped){
        state.lifecycle_blocked=true;
        state.status=state.shutdown_pending?shutdown_failed:cleanup_failed;
    }else if(state.shutdown_pending){
        state.lifecycle_blocked=true;
        state.status=shutdown_failed;
    }else{
        state.lifecycle_blocked=false;
        state.status="mods_disabled";
    }
    return stopped?0:-1;
}
extern "C" void hybrid_shutdown(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& loaded=state.loaded;
    state.shutdown_pending=true;
    bool stopped=true;
    for(auto it=loaded.rbegin();it!=loaded.rend();++it){
        if(it->active){
            hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPING,it->id.c_str());
            if(!stop_mod(*it)){stopped=false;continue;}
            it->active=false;
            hybrid::sdk_dispatch_lifecycle(HYBRID_EVENT_MOD_STOPPED,it->id.c_str());
        }
    }
    state.verified=false;state.host.capabilities=0;
    hybrid::engine_world_read_shutdown();
    hybrid::engine_block_read_configure({});
    if(!stopped){
        state.lifecycle_blocked=true;
        state.status=shutdown_failed;
        return;
    }
    for(auto& entry:loaded)dlclose(entry.handle);
    loaded.clear();state.lifecycle_blocked=false;state.shutdown_pending=false;state.status="runtime_stopped";
}
