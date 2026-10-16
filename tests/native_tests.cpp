#include "hybrid_mod.h"
#include "sha256.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
void check(bool ok){if(!ok){std::fputs("native check failed\n",stderr);std::exit(1);}}
int main(){
    hybrid::Sha256 empty;check(empty.finish()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    hybrid::Sha256 abc;abc.update("abc",3);check(abc.finish()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    hybrid::Sha256 million;for(int i=0;i<1000000;++i)million.update("a",1);check(million.finish()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    check(std::strcmp(hybrid_probe(),"engine_absent")==0);
    check(hybrid_enable_mods()==-1);
    check(hybrid_disable_mods()==0);
    check(hybrid_load_mod(nullptr)==-1);check(hybrid_load_mod("relative.so")==-1);
    check(hybrid_load_mod("/missing-mod.so")==-2);
    hybrid_shutdown();hybrid_shutdown();
    std::puts("SHA256 vectors, absent-engine gating, lifecycle gating, path validation, shutdown: PASS");
}
