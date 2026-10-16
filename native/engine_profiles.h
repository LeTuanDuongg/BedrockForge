#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hybrid {
struct EngineProfile {
    const char* id;
    const char* minecraft_version;
    const char* sha256;
    size_t size;
    uint16_t elf_machine;
    uint8_t validated_layout_count;
};

inline constexpr uint16_t elf_machine_aarch64=183;
inline constexpr EngineProfile engine_profiles[]={
    {"bedrock-1.26.52.3-arm64-original","1.26.52.3",
     "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a",
     328419680,elf_machine_aarch64,5},
    {"bedrock-1.26.52.3-arm64-modified","1.26.52.3",
     "a8942061e3743fce10952627c76615c9e5c0cb07b897efc33f8dabaec96bf4d6",
     328419680,elf_machine_aarch64,5},
    {"bedrock-1.26.50.4-arm64","1.26.50.4",
     "fac2be5b17ef79419ebd66fe4f9e362a48d7b2067d5e1f649e65c36edf2927e1",
     328419680,elf_machine_aarch64,0}
};

inline const EngineProfile* select_engine_profile(const char* sha256,size_t size,uint16_t elf_machine){
    if(!sha256||std::strlen(sha256)!=64)return nullptr;
    for(const auto& profile:engine_profiles){
        if(profile.size==size&&profile.elf_machine==elf_machine&&
           std::strcmp(profile.sha256,sha256)==0)return &profile;
    }
    return nullptr;
}
}
