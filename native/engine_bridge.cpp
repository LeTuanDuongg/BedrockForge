#include "engine_bridge.h"
#include "engine_layout.h"
#include "sha256.h"
#include "self_memory.h"
#include "registry_diagnostic.h"
#include "experimental_hooks.h"
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

std::string experimental_registry_getter_probe(){
#ifndef HYBRID_EXPERIMENTAL_NATIVE
    return "experimental_registry_getter=disabled";
#else
    const auto verified=verify_loaded_engine();
    if(!verified.verified||!verified.profile)return "experimental_registry_getter=blocked;reason=engine_not_verified";
    if(std::strcmp(verified.profile->id,"bedrock-1.26.50.4-arm64"))
        return "experimental_registry_getter=blocked;reason=unsupported_profile";
    Engine engine;dl_iterate_phdr(find_engine,&engine);
    uintptr_t owner,registry,getter;
    if(!engine.image.address(0x134d7da0,owner)||!engine.image.address(0x134d7db8,registry)||
       !engine.image.address(0x103e2d54,getter)||!engine.image.contains(owner,0x158,PF_R|PF_W)||
       !engine.image.contains(getter,12,PF_R|PF_X))
        return "experimental_registry_getter=blocked;reason=mapping_mismatch";
    SelfMemory memory;
    constexpr uint32_t expected_shape_masked[]={0x90000000,0x91000000,0xd65f03c0};
    constexpr uint32_t shape_mask[]={0x9f00001f,0xffc003ff,0xffffffff};
    uint32_t code[3]{};
    if(!memory.available()||!memory.read(getter,code,sizeof code))
        return "experimental_registry_getter=blocked;reason=code_unreadable";
    for(size_t i=0;i<3;++i)if((code[i]&shape_mask[i])!=expected_shape_masked[i])
        return "experimental_registry_getter=blocked;reason=instruction_mismatch";
    using Getter=uintptr_t (*)();
    const auto actual=reinterpret_cast<Getter>(getter)();
    if(actual!=registry||!engine.image.contains(actual,0x158,PF_R|PF_W))
        return "experimental_registry_getter=failed;reason=return_mismatch";
    char result[192];
    std::snprintf(result,sizeof result,
        "experimental_registry_getter=ok;pid=%d;tid=%d;profile=%s;registry=0x%llx;abi=arm64_no_arguments_return_pointer;mutation=none",
        getpid(),gettid(),verified.profile->id,static_cast<unsigned long long>(actual));
    return result;
#endif
}

std::string experimental_registry_lifecycle_snapshot(unsigned phase){
#ifndef HYBRID_EXPERIMENTAL_NATIVE
    (void)phase;
    return "vanilla_block_lookup=disabled";
#else
    const char* phase_name=nullptr;
    switch(phase){
        case 0: phase_name="native_mod_abi_start"; break;
        case 1: phase_name="main_menu_manual"; break;
        case 2: phase_name="world_loaded_manual"; break;
        default: return "registry_sample=blocked;reason=invalid_phase";
    }
    const auto verified=verify_loaded_engine();
    if(!verified.verified||!verified.profile)return "vanilla_block_lookup=blocked;reason=engine_not_verified";
    if(std::strcmp(verified.profile->id,"bedrock-1.26.50.4-arm64"))
        return "vanilla_block_lookup=blocked;reason=unsupported_profile";
    Engine engine;dl_iterate_phdr(find_engine,&engine);
    uintptr_t owner,registry,vtable,typeinfo,name,getter,lookup;
    constexpr char expected_name[]="N7Bedrock5OwnerI17BlockTypeRegistryEE";
    if(!engine.image.address(0x134d7da0,owner)||!engine.image.address(0x134d7db8,registry)||
       !engine.image.address(0x13104308,vtable)||!engine.image.address(0x13104318,typeinfo)||
       !engine.image.address(0x339eb9c,name)||!engine.image.address(0x103e2d54,getter)||
       !engine.image.address(0x103e5648,lookup)||!engine.image.contains(owner,0x158,PF_R|PF_W)||
       !engine.image.contains(vtable-8,32,PF_R)||!engine.image.contains(typeinfo,16,PF_R)||
       !engine.image.contains(name,sizeof expected_name,PF_R)||!engine.image.contains(getter,12,PF_R|PF_X)||
       !engine.image.contains(lookup,16,PF_R|PF_X))
        return "vanilla_block_lookup=blocked;reason=mapping_mismatch";
    SelfMemory memory;
    if(!memory.available())return "vanilla_block_lookup=blocked;reason=self_memory_denied";
    constexpr uint32_t expected_getter[]={0xb00187a0,0x9136e000,0xd65f03c0};
    constexpr uint32_t expected_lookup[]={0x2a1f03e2,0x2a1f03e3,0x2a1f03e4,0x14000196};
    uintptr_t owner_vptr=0,owner_typeinfo=0,owner_name=0;
    char owner_text[sizeof expected_name]{};uint32_t getter_code[3]{},lookup_code[4]{};
    if(!memory.read(owner,&owner_vptr,sizeof owner_vptr)||owner_vptr!=vtable||
       !memory.read(vtable-8,&owner_typeinfo,sizeof owner_typeinfo)||owner_typeinfo!=typeinfo||
       !memory.read(typeinfo+8,&owner_name,sizeof owner_name)||owner_name!=name||
       !memory.read(name,owner_text,sizeof owner_text)||std::memcmp(owner_text,expected_name,sizeof expected_name)||
       !memory.read(getter,getter_code,sizeof getter_code)||std::memcmp(getter_code,expected_getter,sizeof getter_code)||
       !memory.read(lookup,lookup_code,sizeof lookup_code)||std::memcmp(lookup_code,expected_lookup,sizeof lookup_code))
        return "vanilla_block_lookup=blocked;reason=registry_or_abi_mismatch";
    // The verified getter returns this static address. Worker threads call no engine code.
    uintptr_t root=0;
    if(!memory.read(registry+8,&root,sizeof root))
        return "registry_sample=blocked;reason=root_unreadable";
    const auto describe=[&](uintptr_t object,uintptr_t& object_vptr,char* out,size_t capacity)->bool{
        if(!object||!out||capacity<2||!memory.read(object,&object_vptr,sizeof object_vptr)||object_vptr<8||
           !engine.image.contains(object_vptr-8,16,PF_R))return false;
        uintptr_t dynamic_type=0,dynamic_name=0;
        if(!memory.read(object_vptr-8,&dynamic_type,sizeof dynamic_type)||
           !engine.image.contains(dynamic_type,16,PF_R)||
           !memory.read(dynamic_type+8,&dynamic_name,sizeof dynamic_name)||
           !engine.image.contains(dynamic_name,1,PF_R))return false;
        for(size_t i=0;i+1<capacity;++i){
            if(dynamic_name>UINTPTR_MAX-i||!engine.image.contains(dynamic_name+i,1,PF_R)||
               !memory.read(dynamic_name+i,&out[i],1))return false;
            if(!out[i])return true;
        }
        return false;
    };
    char header[512];
    std::snprintf(header,sizeof header,
        "registry_sample=done;pid=%d;tid=%d;profile=%s;engine_sha256=%s;phase=%s;registry=0x%llx;root=0x%llx;engine_calls=0;mutation=none;key_encoding=observed_hash64_plus_inline_ascii;value_ownership=unverified",
        getpid(),gettid(),verified.profile->id,verified.profile->sha256,phase_name,
        static_cast<unsigned long long>(registry),static_cast<unsigned long long>(root));
    std::string result=header;
    uintptr_t queue[9]{root},visited[4]{};
    size_t pending=1,head=0,count=0;
    while(head<pending&&count<4){
        const uintptr_t node=queue[head++];
        if(!node)continue;
        bool seen=false;
        for(size_t i=0;i<count;++i)if(visited[i]==node)seen=true;
        if(seen)continue;
        unsigned char bytes[0x50]{},again[0x50]{};
        if(node%alignof(uintptr_t)||!memory.read(node,bytes,sizeof bytes)||
           !memory.read(node,again,sizeof again)||std::memcmp(bytes,again,sizeof bytes)){
            result+=";node_sample=unstable_or_unreadable";break;
        }
        uintptr_t left=0,right=0,value=0,value_vptr=0;
        std::memcpy(&left,bytes,sizeof left);
        std::memcpy(&right,bytes+8,sizeof right);
        std::memcpy(&value,bytes+0x48,sizeof value);
        char key_hex[81]{},id_candidate[33]{},type[128]{},entry[576];
        constexpr char hex[]="0123456789abcdef";
        for(size_t i=0;i<40;++i){key_hex[2*i]=hex[bytes[0x20+i]>>4];key_hex[2*i+1]=hex[bytes[0x20+i]&15];}
        bool id_valid=true;
        size_t id_len=0;
        for(size_t i=8;i<40;++i){
            const unsigned char c=bytes[0x20+i];
            if(!c)break;
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='_'||c=='-')){id_valid=false;break;}
            if(id_len+1>=sizeof id_candidate){id_valid=false;break;}
            id_candidate[id_len++]=static_cast<char>(c);
        }
        if(!id_len)id_valid=false;
        const bool typed=describe(value,value_vptr,type,sizeof type);
        const int constructor_match=hybrid_experimental_cobblestone_constructor_match(value);
        std::snprintf(entry,sizeof entry,
            ";node%zu=0x%llx,key20_hex=%s,id_candidate=%s,value48=0x%llx,value_rtti=%s;cobblestone_ctor_match=%d",
            count,static_cast<unsigned long long>(node),key_hex,id_valid?id_candidate:"unverified",
            static_cast<unsigned long long>(value),typed?type:"unvalidated",constructor_match);
        result+=entry;
        visited[count++]=node;
        queue[pending++]=left;queue[pending++]=right;
    }
    uintptr_t final_root=0,final_vptr=0;
    if(!memory.read(registry+8,&final_root,sizeof final_root)||final_root!=root||
       !memory.read(owner,&final_vptr,sizeof final_vptr)||final_vptr!=vtable)
        return "registry_sample=discarded;reason=owner_or_root_changed;mutation=none";
    result+=";sampled_nodes="+std::to_string(count)+";root_unchanged=1;snapshot=best_effort_not_locked;lookup=not_invoked_key_abi_unverified";
    return result;
#endif
}

std::string experimental_vanilla_block_lookup_probe(){
    return experimental_registry_lifecycle_snapshot(2);
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

extern "C" const char* hybrid_experimental_registry_getter_probe(){
    thread_local std::string result;
    try{result=hybrid::experimental_registry_getter_probe();return result.c_str();}
    catch(...){return "experimental_registry_getter=failed;reason=exception";}
}

extern "C" const char* hybrid_experimental_vanilla_block_lookup_probe(){
    thread_local std::string result;
    try{result=hybrid::experimental_vanilla_block_lookup_probe();return result.c_str();}
    catch(...){return "vanilla_block_lookup=failed;reason=exception";}
}

extern "C" const char* hybrid_experimental_registry_lifecycle_snapshot(unsigned phase){
    thread_local std::string result;
    try{result=hybrid::experimental_registry_lifecycle_snapshot(phase);return result.c_str();}
    catch(...){return "registry_sample=failed;reason=exception";}
}
