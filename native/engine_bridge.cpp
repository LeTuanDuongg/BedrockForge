#include "engine_bridge.h"
#include "engine_layout.h"
#include "sha256.h"
#include "self_memory.h"
#include "registry_diagnostic.h"
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
    constexpr size_t layout_count=sizeof(layouts)/sizeof(layouts[0]);
    if(profile->validated_layout_count>layout_count){
        result.status="engine_profile_layout_contract_invalid";return result;
    }
    for(size_t i=0;i<profile->validated_layout_count;++i){
        const auto& layout=layouts[i];
        if(!validate_layout(engine.image,layout)){
            result.status=std::string("engine_profile_matched:")+profile->minecraft_version+";profile="+profile->id+
                ";layout_failed="+layout.name+
                ";capabilities=0;gameplay_binding=unavailable";
            return result;
        }
    }
    result.verified=true;
    result.profile=profile;
    // RTTI and vtable layout checks verify identity only; no gameplay contract is certified.
    result.capabilities=0;
    result.status=std::string("engine_verified:")+profile->minecraft_version+
        ";layout_validated="+std::to_string(profile->validated_layout_count)+
        ";profile="+profile->id+";sha256="+profile->sha256+
        ";arch=AArch64;introspection="+(profile->validated_layout_count?"rtti_vtables_read_only":"fingerprint_only")+
        ";capabilities=0;gameplay_binding=unavailable";
    return result;
}

std::string observe_block_registry(){
    const auto verified=verify_loaded_engine();
    if(!verified.verified||!verified.profile)return "registry_observation=unavailable;reason=engine_not_verified";
    if(std::strcmp(verified.profile->id,"bedrock-1.26.50.4-arm64"))
        return "registry_observation=unavailable;reason=profile_unsupported";
    Engine engine;dl_iterate_phdr(find_engine,&engine);
    uintptr_t owner,registry,vtable,typeinfo,name,getter;
    if(!engine.image.address(0x134d7da0,owner)||!engine.image.address(0x134d7db8,registry)||
       !engine.image.address(0x13104308,vtable)||!engine.image.address(0x13104318,typeinfo)||
       !engine.image.address(0x339eb9c,name)||!engine.image.address(0x103e2d54,getter)||
       !engine.image.contains(owner,0x170,PF_R|PF_W)||!engine.image.contains(vtable-8,32,PF_R)||
       !engine.image.contains(typeinfo,16,PF_R)||
       !engine.image.contains(getter,12,PF_R|PF_X))
        return "registry_observation=unavailable;reason=mapping_mismatch";
    SelfMemory memory;
    if(!memory.available())return "registry_observation=unavailable;reason=self_memory_denied";
    constexpr char expected_name[]="N7Bedrock5OwnerI17BlockTypeRegistryEE";
    constexpr uint32_t expected_getter[]={0xb00187a0,0x9136e000,0xd65f03c0};
    uintptr_t actual_vptr=0,actual_typeinfo=0,actual_name=0,first=0,last=0,final_vptr=0;
    char actual_text[sizeof expected_name]{};uint32_t actual_code[3]{};
    if(!engine.image.contains(name,sizeof expected_name,PF_R)||
       !memory.read(owner,&actual_vptr,sizeof actual_vptr)||actual_vptr!=vtable||
       !memory.read(vtable-8,&actual_typeinfo,sizeof actual_typeinfo)||actual_typeinfo!=typeinfo||
       !memory.read(typeinfo+8,&actual_name,sizeof actual_name)||actual_name!=name||
       !memory.read(name,actual_text,sizeof actual_text)||std::memcmp(actual_text,expected_name,sizeof expected_name)||
       !memory.read(getter,actual_code,sizeof actual_code)||std::memcmp(actual_code,expected_getter,sizeof actual_code))
        return "registry_observation=unavailable;reason=owner_contract_mismatch";
    // This field is only sampled. Its pointee is never dereferenced and nonzero does not imply readiness.
    if(!memory.read(registry+0x130,&first,sizeof first)||
       !memory.read(registry+0x130,&last,sizeof last)||
       !memory.read(owner,&final_vptr,sizeof final_vptr)||first!=last||final_vptr!=vtable)
        return "registry_observation=unavailable;reason=snapshot_unstable_or_unreadable";
    char sample[512];
    std::snprintf(sample,sizeof sample,
        "registry_observation=ok;pid=%d;tid=%d;profile=%s;owner_rva=0x134d7da0;registry_rva=0x134d7db8;direct_access_pointer=0x%llx;direct_access_nonzero=%d;callable=0;registration=unavailable",
        getpid(),gettid(),verified.profile->id,static_cast<unsigned long long>(first),first!=0);
    return sample;
}
}

extern "C" const char* hybrid_diagnostic_registry_snapshot(){
    thread_local std::string result;
    try{result=hybrid::observe_block_registry();return result.c_str();}
    catch(...){return "registry_observation=unavailable;reason=diagnostic_exception";}
}
