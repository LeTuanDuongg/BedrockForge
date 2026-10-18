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
#if defined(__ANDROID__) && defined(__aarch64__)
    auto* heap=static_cast<uint64_t*>(std::malloc(sizeof(uint64_t)));
    check(heap!=nullptr);
    *heap=expected;
    const uintptr_t tagged=(reinterpret_cast<uintptr_t>(heap)&0x00ffffffffffffffULL)|0xb400000000000000ULL;
    check(memory.read(tagged,&actual,sizeof actual));
    check(actual==expected);
    check(!memory.read(0xb400000000000001ULL,&actual,sizeof actual));
    std::free(heap);
    std::puts("ARM64 tagged owned allocation and tagged inaccessible read: PASS (reader test only)");
#endif
    std::puts("self memory valid, inaccessible, overflow, null and empty reads: PASS (reader test only)");
}
