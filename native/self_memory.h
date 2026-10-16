#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sys/uio.h>
#include <unistd.h>

namespace hybrid {
// Kernel-assisted reads fail on inaccessible memory instead of dereferencing engine pointers.
class SelfMemory {
public:
    SelfMemory()=default;
    SelfMemory(const SelfMemory&)=delete;
    SelfMemory& operator=(const SelfMemory&)=delete;
    bool available() const {
        const uint64_t marker=0x123456789abcdef0ULL;uint64_t copy=0;
        return read(reinterpret_cast<uintptr_t>(&marker),&copy,sizeof copy)&&copy==marker;
    }
    bool read(uintptr_t address,void* out,size_t size) const {
        if(!out||!size||address>static_cast<uintptr_t>(std::numeric_limits<ssize_t>::max())||
           size>static_cast<uintptr_t>(std::numeric_limits<ssize_t>::max())-address)return false;
        iovec local{out,size},remote{reinterpret_cast<void*>(address),size};
        ssize_t result;
        do {result=process_vm_readv(getpid(),&local,1,&remote,1,0);} while(result<0&&errno==EINTR);
        return result>=0&&static_cast<size_t>(result)==size;
    }
};
}
