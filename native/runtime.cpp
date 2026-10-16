#include "hybrid_mod.h"
#include "engine_bridge.h"
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#ifdef HYBRID_MODIFIED_GAME
#include "engine_profile.h"
#endif

namespace {
#ifdef HYBRID_MODIFIED_GAME
constexpr auto target=HYBRID_MODIFIED_ENGINE_SHA256;
constexpr size_t target_size=HYBRID_MODIFIED_ENGINE_SIZE;
#else
constexpr auto target="53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a";
constexpr size_t target_size=328419680;
#endif
struct Loaded {void* handle;const HybridMod* mod;std::string id;};
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
    const auto result=hybrid::verify_loaded_engine(target,target_size);
    state.status=result.status;state.host.capabilities=result.capabilities;
    return result.verified;
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
    if(mod->start(&state.host)!=0)return fail(-7,"mod_start_failed");
    loaded.push_back({handle,mod,mod->id});status="mod_loaded:"+loaded.back().id;return 0;
}
extern "C" void hybrid_shutdown(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& loaded=state.loaded;
    for(auto it=loaded.rbegin();it!=loaded.rend();++it){it->mod->stop();dlclose(it->handle);}
    loaded.clear();state.verified=false;state.host.capabilities=0;state.status="runtime_stopped";
}
