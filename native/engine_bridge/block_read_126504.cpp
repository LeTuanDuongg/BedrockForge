#include "block_read.h"
#include "hybrid_sdk.h"

#if defined(__ANDROID__) && defined(__aarch64__)
#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"
#include <array>
#include <atomic>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <android/log.h>

namespace hybrid {
namespace {
constexpr const char* profile_id = "bedrock-1.26.50.4-arm64";
constexpr uintptr_t registry_va = 0x134d7db8;
constexpr uintptr_t getter_va = 0x103e2d54;
constexpr uintptr_t acquire_va = 0x103e2784;
constexpr uintptr_t release_va = 0x103e839c;
std::atomic<uintptr_t> verified_base{0};

int find_image(dl_phdr_info* info, size_t, void* data) {
    if (!info->dlpi_name) return 0;
    const char* name = std::strrchr(info->dlpi_name, '/');
    if (std::strcmp(name ? name + 1 : info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& image = *static_cast<EngineImage*>(data);
    image.base = info->dlpi_addr;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        uintptr_t address = 0;
        if (ph.p_type == PT_LOAD && image.address(ph.p_vaddr, address))
            image.segments.push_back({address, static_cast<size_t>(ph.p_memsz), ph.p_flags});
    }
    return 1;
}

// Same non-trivial ARM64 indirect-return guard ABI already used by the existing
// exact-build bridge. Release before returning any copied data to SDK callers.
struct RegistryGuard {
    uintptr_t mutex;
    uintptr_t shared_state;
    RegistryGuard() = default;
    RegistryGuard(const RegistryGuard&) = delete;
    ~RegistryGuard() noexcept {}
};
struct GuardRelease {
    RegistryGuard& guard;
    uintptr_t address;
    ~GuardRelease() { reinterpret_cast<void (*)(RegistryGuard*)>(address)(&guard); }
};

bool admitted(EngineImage& image) {
    const auto expected_base = verified_base.load(std::memory_order_acquire);
    if (!expected_base) return false;
    dl_iterate_phdr(find_image, &image);
    if (!image.base || image.base != expected_base) return false;
    for (uintptr_t va : {getter_va, acquire_va, release_va}) {
        uintptr_t address = 0;
        if (!image.address(va, address) || !image.contains(address, 16, PF_R | PF_X)) return false;
    }
    SelfMemory memory;
    constexpr uint32_t expected[]{0xb00187a0, 0x9136e000, 0xd65f03c0};
    uint32_t code[3]{};
    uintptr_t owner_vptr = 0;
    return image.contains(image.base + registry_va - 24, 0x170, PF_R | PF_W) &&
        memory.read(image.base + getter_va, code, sizeof code) &&
        !std::memcmp(code, expected, sizeof code) &&
        memory.read(image.base + registry_va - 24, &owner_vptr, sizeof owner_vptr) &&
        owner_vptr == image.base + 0x13104308;
}

bool read_name(const SelfMemory& memory, uintptr_t address, char (&out)[97]) {
    unsigned char string[24]{};
    if (!memory.read(address, string, sizeof string)) return false;
    uint64_t length = string[0] >> 1;
    if (string[0] & 1) {
        uintptr_t bytes = 0;
        std::memcpy(&length, string + 8, sizeof length);
        std::memcpy(&bytes, string + 16, sizeof bytes);
        if (!length || length > 96 || !bytes || !memory.read(bytes, out, length)) return false;
    } else {
        if (!length || length > 22) return false;
        std::memcpy(out, string + 1, length);
    }
    out[length] = 0;
    return std::strlen(out) == length;
}
}

void engine_block_read_configure(const EngineVerification& verification) {
    EngineImage image;
    if (verification.verified && verification.profile &&
        !std::strcmp(verification.profile->id, profile_id))
        dl_iterate_phdr(find_image, &image);
    verified_base.store(image.base, std::memory_order_release);
}

bool engine_block_read_supported() {
    EngineImage image;
    return admitted(image);
}

int32_t engine_block_find(const char* identifier, EngineBlockRead& out) {
    EngineImage image;
    if (!identifier || !admitted(image)) return HYBRID_SDK_UNSUPPORTED;
    const uintptr_t base = image.base;
    const auto registry = reinterpret_cast<uintptr_t (*)()>(base + getter_va)();
    if (registry != base + registry_va) return HYBRID_SDK_UNSUPPORTED;
    auto guard = reinterpret_cast<RegistryGuard (*)(uintptr_t)>(base + acquire_va)(registry);
    GuardRelease release{guard, base + release_va};
    if (!guard.mutex) return HYBRID_SDK_UNSUPPORTED;
    SelfMemory memory;
    uintptr_t root = 0;
    if (!memory.read(registry + 8, &root, sizeof root) || !root) return HYBRID_SDK_UNSUPPORTED;

    // Reuse the observed hash/name tree layout, under the engine registry guard.
    // Do NOT call 0x103e5648: its 16-byte return/fallback ABI is not certified.
    // Bounded identifier comparison also avoids manufacturing an engine string/key.
    std::array<uintptr_t, 128> pending{};
    size_t depth = 1, visited = 0;
    pending[0] = root;
    while (depth) {
        const auto node = pending[--depth];
        if (!node || ++visited > 8192) return HYBRID_SDK_UNSUPPORTED;
        uintptr_t children[2]{}, type = 0, vptr = 0;
        char key[97]{};
        if (!memory.read(node, children, sizeof children) || !read_name(memory, node + 0x28, key))
            return HYBRID_SDK_UNSUPPORTED;
        if (std::strcmp(key, identifier) == 0) {
            EngineBlockRead result;
            uintptr_t state = 0, state_vptr = 0, owner = 0, bounds[3]{};
            uint16_t numeric = 0;
            uint8_t assigned = 0;
            if (!memory.read(node + 0x48, &type, sizeof type) || !type ||
                !memory.read(type, &vptr, sizeof vptr) || vptr < 8 ||
                !image.contains(vptr - 8, 16, PF_R) ||
                !read_name(memory, type + 0xd0, result.identifier) ||
                std::strcmp(result.identifier, identifier) ||
                !memory.read(type + 0x14e, &numeric, sizeof numeric) ||
                !memory.read(type + 0x1e8, bounds, sizeof bounds) ||
                !memory.read(type + 0x200, &state, sizeof state) || !state ||
                bounds[1] < bounds[0] || bounds[2] < bounds[1] ||
                (bounds[1] - bounds[0]) % 8 || (bounds[1] - bounds[0]) / 8 > 4096 ||
                !memory.read(state, &state_vptr, sizeof state_vptr) ||
                state_vptr != base + 0x130f01c0 ||
                !memory.read(state + 0x68, &owner, sizeof owner) || owner != type ||
                !memory.read(state + 0x11c, &result.state_hash, sizeof result.state_hash) ||
                !memory.read(state + 0x12a, &assigned, sizeof assigned) || assigned > 1)
                return HYBRID_SDK_UNSUPPORTED;
            const size_t count = (bounds[1] - bounds[0]) / 8;
            bool member = false;
            for (size_t i = 0; i < count; ++i) {
                uintptr_t entry = 0;
                if (!memory.read(bounds[0] + i * 8, &entry, sizeof entry)) return HYBRID_SDK_UNSUPPORTED;
                member |= entry == state;
            }
            if (!member) return HYBRID_SDK_UNSUPPORTED;
            result.numeric_id = numeric;
            result.state_count = static_cast<uint32_t>(count);
            result.hash_assigned = assigned;
            out = result;
            __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
                "sdk_block_engine=read;profile=%s;identifier=%s;getter_match=1;guard_held=1;"
                "owner_match=1;state_member=1;numeric=%u;states=%u;default_hash=%u;hash_assigned=%u",
                profile_id, result.identifier, result.numeric_id, result.state_count,
                result.state_hash, result.hash_assigned);
            return HYBRID_SDK_OK;
        }
        for (const auto child : children) {
            if (!child) continue;
            if (depth == pending.size()) return HYBRID_SDK_UNSUPPORTED;
            pending[depth++] = child;
        }
    }
    return HYBRID_SDK_NOT_FOUND;
}
}
#else
namespace hybrid {
void engine_block_read_configure(const EngineVerification&) {}
bool engine_block_read_supported() { return false; }
int32_t engine_block_find(const char*, EngineBlockRead&) { return HYBRID_SDK_UNSUPPORTED; }
}
#endif
