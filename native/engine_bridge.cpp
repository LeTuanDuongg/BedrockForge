#include "engine_bridge.h"
#include "engine_layout.h"
#include "sha256.h"
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <cstdio>
#include <cstring>

namespace hybrid {
namespace {
struct Engine {std::string path;EngineImage image;};
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
}

EngineVerification verify_loaded_engine(){
    EngineVerification result;
    Engine engine;dl_iterate_phdr(find_engine,&engine);
    if(engine.path.empty()){result.status="engine_absent";return result;}
    FILE* f=std::fopen(engine.path.c_str(),"rb");
    if(!f){result.status="engine_file_unreadable";return result;}
    Elf64_Ehdr elf{};
    if(std::fread(&elf,1,sizeof elf,f)!=sizeof elf||std::memcmp(elf.e_ident,ELFMAG,SELFMAG)||
       elf.e_ident[EI_CLASS]!=ELFCLASS64||elf.e_ident[EI_DATA]!=ELFDATA2LSB||elf.e_machine!=EM_AARCH64){
        std::fclose(f);result.status="engine_wrong_elf";return result;
    }
    std::rewind(f);Sha256 hash;unsigned char buffer[65536];size_t total=0,n;
    while((n=std::fread(buffer,1,sizeof buffer,f))){hash.update(buffer,n);total+=n;}
    bool error=std::ferror(f);std::fclose(f);
    if(error){result.status="engine_read_failed";return result;}
    const auto digest=hash.finish();
    const auto* profile=select_engine_profile(digest.c_str(),total,elf.e_machine);
    if(!profile){result.status="engine_profile_unknown";return result;}
    for(const auto& layout:layouts){
        if(!validate_layout(engine.image,layout)){
            result.status=std::string("engine_profile_matched:1.26.52.3;profile=")+profile->id+
                ";layout_failed="+layout.name+
                ";capabilities=0;gameplay_binding=unavailable";
            return result;
        }
    }
    result.verified=true;
    // RTTI and vtable layout checks verify identity only; no gameplay contract is certified.
    result.capabilities=0;
    result.status=std::string("engine_verified:")+profile->minecraft_version+
        ";layout_validated=5;profile="+profile->id+";sha256="+profile->sha256+
        ";arch=AArch64;introspection=rtti_vtables_read_only;capabilities=0;gameplay_binding=unavailable";
    return result;
}
}
