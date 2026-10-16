#include "hybrid_mod.h"
#include "engine_bridge.h"
#include "sha256.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
void check(bool ok){if(!ok){std::fputs("native check failed\n",stderr);std::exit(1);}}
int main(){
    constexpr uint16_t aarch64=183,x86_64=62;
    constexpr size_t engine_size=328419680;
    const auto* original=hybrid::select_engine_profile(
        "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a",engine_size,aarch64);
    check(original&&std::strcmp(original->id,"bedrock-1.26.52.3-arm64-original")==0);
    const auto* modified=hybrid::select_engine_profile(
        "a8942061e3743fce10952627c76615c9e5c0cb07b897efc33f8dabaec96bf4d6",engine_size,aarch64);
    check(modified&&std::strcmp(modified->id,"bedrock-1.26.52.3-arm64-modified")==0);
    check(!hybrid::select_engine_profile(
        "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a",engine_size-1,aarch64));
    check(!hybrid::select_engine_profile(
        "53c4a86a8f3dbd7f0d48e440f8607d63c55100aeb364c80d19afefa8097e233a",engine_size,x86_64));
    check(!hybrid::select_engine_profile("53c4a86a",engine_size,aarch64));
    check(!hybrid::select_engine_profile(
        "0000000000000000000000000000000000000000000000000000000000000000",engine_size,aarch64));
    hybrid::Sha256 empty;check(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hybrid::Sha256 abc;abc.update("abc",3);check(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hybrid::Sha256 million;for(int i=0;i<1000000;++i)million.update("a",1);check(million.finish()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    check(std::strcmp(hybrid_probe(),"engine_absent")==0);
    check(hybrid_enable_mods()==-1);
    check(hybrid_disable_mods()==0);
    check(hybrid_load_mod(nullptr)==-1);check(hybrid_load_mod("relative.so")==-1);
    check(hybrid_load_mod("/missing-mod.so")==-2);
    hybrid_shutdown();hybrid_shutdown();
    std::puts("engine profile selection, SHA256 vectors, absent-engine gating, lifecycle gating, path validation, shutdown: PASS");
}
