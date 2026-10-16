#pragma once
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace hybrid {
struct Segment {uintptr_t address; size_t size; unsigned flags;};
struct EngineImage {
    uintptr_t base=0;
    std::vector<Segment> segments;
    bool contains(uintptr_t address,size_t size,unsigned flags) const {
        for(const auto& segment:segments){
            if((segment.flags&flags)==flags&&address>=segment.address&&
               address-segment.address<=segment.size&&size<=segment.size-(address-segment.address))return true;
        }
        return false;
    }
    bool address(uintptr_t va,uintptr_t& out) const {
        if(va>std::numeric_limits<uintptr_t>::max()-base)return false;
        out=base+va;return true;
    }
    bool pointer(uintptr_t va,uintptr_t& out) const {
        uintptr_t at;
        if(!address(va,at)||!contains(at,sizeof(out),4))return false;
        std::memcpy(&out,reinterpret_cast<const void*>(at),sizeof(out));return true;
    }
};
struct Layout {const char* name;uintptr_t name_va,typeinfo_va,vtable_va;uintptr_t slots[8];};
// ELF virtual addresses from RELA-backed RTTI, engine-rtti.json; not Ghidra's artificial image base.
inline constexpr Layout layouts[]={
    {"21ChestScreenController",0x29c775d,0x12ab6b18,0x12ab50a8,{0x86232d4,0x862b0c4,0x9b15f34,0x9a24938,0x9b14fec,0x9b15eb0,0x9b15ebc,0x9a25444}},
    {"25ContainerScreenController",0x2c0da8f,0x12bee608,0x12be1720,{0x9a245c8,0x9a24930,0x9b15f34,0x9a24938,0x9b14fec,0x9b15eb0,0x9b15ebc,0x9a25444}},
    {"15ChestBlockActor",0x33bf839,0x1312ed98,0x1312aaa8,{0x106edb8c,0x106edc74,0x106ee5fc,0x106eea98,0x106eedf8,0x106dfc18,0x106dfc1c,0x106dfc20}},
    {"21ContainerManagerModel",0x32e6232,0x130297b0,0x13028768,{0xf9582c0,0xf95852c,0xf9591d0,0xf9591d8,0xf9591e0,0xf9591e8,0xf9592a0,0xf959814}},
    {"9Inventory",0x33121e3,0x13034fd0,0x13034568,{0xf9da350,0xf9da354,0xf9da378,0xf884004,0xf6e28f0,0xf6e2944,0xf6e2a88,0xf6e2a90}}
};
inline bool validate_layout(const EngineImage& image,const Layout& layout){
    uintptr_t name,typeinfo,actual;
    if(!image.address(layout.name_va,name)||!image.address(layout.typeinfo_va,typeinfo)||
       !image.pointer(layout.typeinfo_va+8,actual)||actual!=name||
       !image.contains(name,std::strlen(layout.name)+1,4)||
       std::memcmp(reinterpret_cast<const void*>(name),layout.name,std::strlen(layout.name)+1)||
       layout.vtable_va<8||!image.pointer(layout.vtable_va-8,actual)||actual!=typeinfo)return false;
    for(unsigned i=0;i<8;++i){
        uintptr_t expected;
        if(!image.address(layout.slots[i],expected)||!image.contains(expected,4,5)||
           !image.pointer(layout.vtable_va+i*8,actual)||actual!=expected)return false;
    }
    return true;
}
}
