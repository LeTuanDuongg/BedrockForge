#include "self_memory.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
void check(bool ok){if(!ok){std::fputs("self memory reader check failed\n",stderr);std::exit(1);}}
int main(){
    hybrid::SelfMemory memory;
    check(memory.available());
    const uint64_t expected=0x123456789abcdef0ULL;uint64_t actual=0;
    check(memory.read(reinterpret_cast<uintptr_t>(&expected),&actual,sizeof actual));
    check(actual==expected);
    check(!memory.read(0,&actual,sizeof actual));
    check(!memory.read(1,&actual,sizeof actual));
    check(!memory.read(std::numeric_limits<uintptr_t>::max(),&actual,sizeof actual));
    check(!memory.read(reinterpret_cast<uintptr_t>(&expected),nullptr,sizeof actual));
    check(!memory.read(reinterpret_cast<uintptr_t>(&expected),&actual,0));
    std::puts("self memory valid, inaccessible, overflow, null and empty reads: PASS (reader test only)");
}
