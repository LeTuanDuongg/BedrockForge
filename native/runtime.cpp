#include "hybrid_mod.h"
#include "sha256.h"
#include "engine_layout.h"
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <cstdio>
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
};
// Dependency constructors can arrive before other translation units initialize.
Runtime& runtime(){static Runtime state;return state;}
constexpr HybridHost host{HYBRID_ABI,sizeof(HybridHost),0};
struct Engine {std::string path;hybrid::EngineImage image;};
int find_engine(dl_phdr_info* info,size_t,void* out){
    if(!info->dlpi_name)return 0;
    const char* name=std::strrchr(info->dlpi_name,'/');
    if(std::strcmp(name?name+1:info->dlpi_name,"libminecraftpe.so"))return 0;
    auto& engine=*static_cast<Engine*>(out);
    engine.path=info->dlpi_name;engine.image.base=info->dlpi_addr;
    for(unsigned i=0;i<info->dlpi_phnum;++i){
        const auto& ph=info->dlpi_phdr[i];uintptr_t address;
        if(ph.p_type==PT_LOAD&&engine.image.address(ph.p_vaddr,address))
            engine.image.segments.push_back({address,static_cast<size_t>(ph.p_memsz),ph.p_flags});
    }
    return 1;
}
bool check_engine(Runtime& state){
    auto& status=state.status;
    Engine engine;dl_iterate_phdr(find_engine,&engine);
    if(engine.path.empty()){status="engine_absent";return false;}
    FILE* f=std::fopen(engine.path.c_str(),"rb");
    if(!f){status="engine_file_unreadable";return false;}
    Elf64_Ehdr elf{};
    if(std::fread(&elf,1,sizeof elf,f)!=sizeof elf||std::memcmp(elf.e_ident,ELFMAG,SELFMAG)||elf.e_ident[EI_CLASS]!=ELFCLASS64||elf.e_ident[EI_DATA]!=ELFDATA2LSB||elf.e_machine!=EM_AARCH64){std::fclose(f);status="engine_wrong_elf";return false;}
    std::rewind(f);hybrid::Sha256 hash;unsigned char buffer[65536];size_t total=0,n;
    while((n=std::fread(buffer,1,sizeof buffer,f))){hash.update(buffer,n);total+=n;}
    bool error=std::ferror(f);std::fclose(f);
    if(error){status="engine_read_failed";return false;}
    if(total!=target_size||hash.finish()!=target){status="engine_identity_mismatch";return false;}
    for(const auto& layout:hybrid::layouts){
        if(!hybrid::validate_layout(engine.image,layout)){
            status=std::string("engine_verified:1.26.52.3;layout_failed:")+layout.name+";capabilities=0;gameplay_binding=unavailable";
            return false;
        }
    }
    status="engine_verified:1.26.52.3;layout_validated=5;introspection=rtti_vtables_read_only;capabilities=0;gameplay_binding=unavailable";return true;
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
    if(mod->required_capabilities&~host.capabilities)return fail(-5,"mod_blocked:required_engine_capabilities_unavailable");
    for(const auto& prior:loaded)if(prior.id==mod->id)return fail(-6,"mod_duplicate_id");
    if(mod->start(&host)!=0)return fail(-7,"mod_start_failed");
    loaded.push_back({handle,mod,mod->id});status="mod_loaded:"+loaded.back().id;return 0;
}
extern "C" void hybrid_shutdown(){
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    auto& loaded=state.loaded;
    for(auto it=loaded.rbegin();it!=loaded.rend();++it){it->mod->stop();dlclose(it->handle);}
    loaded.clear();state.verified=false;state.status="runtime_stopped";
}
