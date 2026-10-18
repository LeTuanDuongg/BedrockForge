#include "sha256.h"
#include "engine_layout.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
void check(bool ok){if(!ok){std::fputs("self-check failed\n",stderr);std::exit(1);}}
}
int main(){
    hybrid::Sha256 empty;check(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hybrid::Sha256 abc;abc.update("abc",3);check(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hybrid::Sha256 million;for(int i=0;i<1000000;++i)million.update("a",1);check(million.finish()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    unsigned char memory[512]{};const auto base=reinterpret_cast<uintptr_t>(memory);
    hybrid::EngineImage image{base,{{base,sizeof memory,4},{base+256,64,5}}};
    hybrid::Layout layout{"Test",16,64,96,{256,260,264,268,272,276,280,284}};
    auto put=[&](size_t va,uintptr_t value){std::memcpy(memory+va,&value,sizeof value);};
    std::memcpy(memory+16,"Test",5);put(72,base+16);put(88,base+64);
    for(unsigned i=0;i<8;++i)put(96+i*8,base+layout.slots[i]);
    check(hybrid::validate_layout(image,layout));
    put(72,base+17);check(!hybrid::validate_layout(image,layout));put(72,base+16);
    put(88,base+65);check(!hybrid::validate_layout(image,layout));put(88,base+64);
    put(96,base+257);check(!hybrid::validate_layout(image,layout));put(96,base+256);
    memory[16]='X';check(!hybrid::validate_layout(image,layout));memory[16]='T';
    image.segments.pop_back();check(!hybrid::validate_layout(image,layout));
    check(!image.contains(base+511,2,4));check(!image.contains(base-1,1,4));
    uintptr_t result;check(!image.address(std::numeric_limits<uintptr_t>::max(),result));
    image.segments.clear();check(!hybrid::validate_layout(image,layout));
    std::puts("PASS: SHA256 vectors, RTTI/vtable pointer validation, readable/executable bounds, overflow guards");
}
