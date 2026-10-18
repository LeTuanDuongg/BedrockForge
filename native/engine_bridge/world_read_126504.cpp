// Read-only world/block access for the exactly pinned Bedrock 1.26.50.4 ARM64 engine.
//
// Certified contract (see targets/bedrock-1.26.50.4-arm64-world-contracts.json):
//   * `BlockSource` (RTTI "11BlockSource", vtable 0x130606d0) is a stack-constructed,
//     short-lived view object. Its constructor `0xfb8e16c` receives
//     (this, Level&, Dimension&, ChunkSource&, b4, b5, b6, b7) and stores
//     +0x28 = Level, +0x30 = ChunkSource, +0x38 = Dimension, +0x40 = inclusive
//     minimum height, +0x42 = exclusive maximum height.
//   * `BlockSource::getBlock(BlockPos const&)` is vtable slot 5 = `0xfb9162c`; it
//     height-checks the position, fetches the chunk through slot 41 (`0xfb90738`),
//     and returns the chunk's `Block const*` (or the registry default air block).
//   * The `Block` result is validated against the certified `Block` vtable. Its owning
//     BlockType identity and state metadata are copied from the observed fields; no
//     second registry traversal is needed. No engine pointer, BlockSource, Level,
//     Dimension or Block reference escapes this translation unit.
//
// Thread contract: the engine only reads chunks on threads it already owns. The public
// SDK is normally entered from the Android activity thread, so a world read is serviced
// by the observed engine thread through the constructor observation hook; the engine
// thread is preferred, and the read fails with UNSUPPORTED when no engine thread
// services it inside the deadline.
#include "world_read.h"
#include "hybrid_sdk.h"

#if defined(__ANDROID__) && defined(__aarch64__) && defined(HYBRID_EXPERIMENTAL_NATIVE)

#include "engine_bridge.h"
#include "engine_layout.h"
#include "self_memory.h"
#include <android/log.h>
#include <shadowhook.h>
#include <atomic>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <mutex>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

namespace hybrid {
namespace {
constexpr const char* profile_id = "bedrock-1.26.50.4-arm64";

constexpr uintptr_t kBlockSourceCtor = 0xfb8e16c;
constexpr uintptr_t kBlockSourceGet = 0xfb9162c;
constexpr uintptr_t kBlockSourceVtable = 0x130606d0;
constexpr uintptr_t kBlockSourceTypeinfo = 0x130614a8;
constexpr uintptr_t kBlockSourceName = 0x3333888;
constexpr uintptr_t kLevelVtable = 0x1305bb98;
constexpr uintptr_t kLevelTypeinfo = 0x1305ddd0;
constexpr uintptr_t kLevelName = 0x332f542;
constexpr uintptr_t kDimensionVtable = 0x12b1a548;
constexpr uintptr_t kDimensionTypeinfo = 0x130fcfa0;
constexpr uintptr_t kDimensionName = 0x339660d;
constexpr uintptr_t kBlockVtable = 0x130f01c0;
constexpr uintptr_t kBlockTypeinfo = 0x130f03a8;
constexpr uintptr_t kBlockName = 0x33885ec;

// BlockSource member offsets, certified by the constructor decompile; the fields that
// are read back are named below (Level +0x28, ChunkSource +0x30, Dimension +0x38).
constexpr uintptr_t kMaxHeightField = 0x40;
constexpr uintptr_t kMinHeightField = 0x42;
constexpr uintptr_t kDimensionIdField = 0x11c;

// Block member offsets, already certified by the existing guarded registry read.
constexpr uintptr_t kBlockTypeField = 0x68;
constexpr uintptr_t kTypeNameField = 0xd0;
constexpr uintptr_t kTypeNumericField = 0x14e;
constexpr uintptr_t kTypeStateBoundsField = 0x1e8;
constexpr uintptr_t kBlockStateHashField = 0x11c;
constexpr uintptr_t kBlockAssignField = 0x12a;

constexpr unsigned kMaxCaptures = 4;
constexpr uint64_t kPreferredThreadDelayNs = 20ull * 1000000ull;
constexpr uint64_t kServiceDeadlineNs = 250ull * 1000000ull;
// The engine builds temporary BlockSources continuously while a world is live, so a
// capture that nobody observes for this long belongs to a world that is gone.
constexpr uint64_t kCaptureFreshnessNs = 5ull * 1000000000ull;
constexpr uint64_t kObserveBeatNs = 200ull * 1000000ull;

constexpr uint32_t kIdle = 0;
constexpr uint32_t kFilling = 4;
constexpr uint32_t kRequested = 1;
constexpr uint32_t kServicing = 3;
constexpr uint32_t kDone = 2;

// Re-checked against the live image on every capture and every read, so a borrowed
// pointer left over from an earlier world can never be dereferenced by accident.
const Layout kBlockSourceLayout{"11BlockSource", kBlockSourceName, kBlockSourceTypeinfo,
    kBlockSourceVtable,
    {0xfb8e398, 0xfb8e494, 0xfb913ec, 0xfb914ec, 0xfb915ac, 0xfb9162c, 0xfb916ec, 0xfb917c4}};
const Layout kLevelLayout{"5Level", kLevelName, kLevelTypeinfo, kLevelVtable,
    {0xfb2d558, 0xfb2fa60, 0xfb29e9c, 0xfb2fa6c, 0xfb30498, 0xfb30e20, 0xfb30ec0, 0xfb30ed0}};
const Layout kDimensionLayout{"9Dimension", kDimensionName, kDimensionTypeinfo, kDimensionVtable,
    {0x89a5b6c, 0x89a5b88, 0x8d06760, 0x8d06770, 0x8d06e94, 0x8d06ea0, 0x8d06ea4, 0x8d06f84}};

struct Capture {
    uintptr_t level = 0;
    uintptr_t dimension = 0;
    int32_t dimension_id = 0;
    uint64_t generation = 0;
    uint64_t serial = 0;
};

struct Pending {
    uint64_t world_id = 0;
    int32_t x = 0, y = 0, z = 0;
    int32_t status = HYBRID_SDK_UNSUPPORTED;
    uint64_t started_ns = 0;
    EngineWorldBlock result{};
};

using BlockSourceCtor = void (*)(void*, void*, void*, void*, uint32_t, uint32_t, uint32_t,
                                 uint32_t);

std::atomic<uintptr_t> verified_base{0};
std::atomic<bool> hook_ready{false};
std::atomic<uint32_t> engine_thread{0};
std::atomic<uint32_t> pending_state{kIdle};
std::atomic<uint64_t> next_generation{1};
std::atomic<uint64_t> next_serial{1};
std::atomic<unsigned> capture_observations{0};
std::atomic<unsigned> constructor_hook_hits{0};
std::atomic<unsigned> capture_rejections{0};
std::atomic<uintptr_t> observed_level{0};
std::atomic<uintptr_t> observed_dimension{0};
std::atomic<uint64_t> observed_ns{0};
EngineImage image_cache;
Capture captures[kMaxCaptures];
Pending pending;
std::mutex capture_mutex;
BlockSourceCtor ctor_previous = nullptr;
void* ctor_stub = nullptr;

uint64_t now_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000000ull + static_cast<uint64_t>(t.tv_nsec);
}

bool rtti_named(const EngineImage& image, uintptr_t vtable_va, uintptr_t typeinfo_va,
                uintptr_t name_va, const char* name) {
    uintptr_t actual = 0;
    const size_t length = std::strlen(name) + 1;
    if (!image.pointer(vtable_va - 8, actual) || actual != image.base + typeinfo_va) return false;
    if (!image.pointer(typeinfo_va + 8, actual) || actual != image.base + name_va) return false;
    if (!image.contains(image.base + name_va, length, PF_R)) return false;
    return std::memcmp(reinterpret_cast<const void*>(image.base + name_va), name, length) == 0;
}

bool engine_layouts_hold(const EngineImage& image) {
    return validate_layout(image, kBlockSourceLayout) && validate_layout(image, kLevelLayout) &&
        validate_layout(image, kDimensionLayout) &&
        rtti_named(image, kBlockVtable, kBlockTypeinfo, kBlockName, "5Block");
}

int locate(dl_phdr_info* info, size_t, void* output) {
    if (!info->dlpi_name) return 0;
    const char* name = std::strrchr(info->dlpi_name, '/');
    if (std::strcmp(name ? name + 1 : info->dlpi_name, "libminecraftpe.so")) return 0;
    auto& image = *static_cast<EngineImage*>(output);
    image.base = info->dlpi_addr;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        uintptr_t address = 0;
        if (ph.p_type == PT_LOAD && image.address(ph.p_vaddr, address))
            image.segments.push_back({address, static_cast<size_t>(ph.p_memsz), ph.p_flags});
    }
    return 1;
}

bool admitted(EngineImage& image) {
    const auto expected = verified_base.load(std::memory_order_acquire);
    if (!expected) return false;
    image = image_cache;
    if (image.base != expected) return false;
    SelfMemory memory;
    constexpr uint32_t ctor_prologue[]{0xa9bb7bfd, 0xa90167fa, 0xa9025ff8, 0xa90357f6};
    constexpr uint32_t get_prologue[]{0xd10103ff, 0xa9017bfd, 0xa90257f6, 0xa9034ff4};
    uint32_t code[4]{};
    return image.contains(image.base + kBlockSourceCtor, sizeof ctor_prologue, PF_R | PF_X) &&
        image.contains(image.base + kBlockSourceGet, sizeof get_prologue, PF_R | PF_X) &&
        memory.read(image.base + kBlockSourceCtor, code, sizeof code) &&
        !std::memcmp(code, ctor_prologue, sizeof code) &&
        memory.read(image.base + kBlockSourceGet, code, sizeof code) &&
        !std::memcmp(code, get_prologue, sizeof code) && engine_layouts_hold(image);
}

// Same layout as the guarded registry read: a Bedrock hashed string.
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

bool well_formed_identifier(const char* text) {
    bool separator = false;
    size_t i = 0;
    for (; text[i] && i < 96; ++i) {
        const char c = text[i];
        if (c == ':') {
            if (separator || i == 0) return false;
            separator = true;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
              c == '-'))
            return false;
    }
    return separator && i != 0 && i < 96;
}

Capture* find_capture(int32_t dimension_id) {
    for (auto& capture : captures)
        if (capture.level && capture.dimension_id == dimension_id) return &capture;
    return nullptr;
}

void record_capture(void* self, void* level, void* dimension, void* chunk_source) {
    const auto base = verified_base.load(std::memory_order_acquire);
    auto reject = [](const char* reason, uintptr_t observed = 0) {
        if (capture_rejections.fetch_add(1, std::memory_order_relaxed) < 6)
            __android_log_print(ANDROID_LOG_WARN, "HybridMCPE",
                "sdk_world_capture_rejected;reason=%s;observed=0x%llx;pid=%d;tid=%d",
                reason, static_cast<unsigned long long>(observed), getpid(), gettid());
    };
    if (!base || !self || !level || !dimension || !chunk_source) {
        reject("missing_argument");
        return;
    }
    const auto level_address = reinterpret_cast<uintptr_t>(level);
    const auto dimension_address = reinterpret_cast<uintptr_t>(dimension);

    // Hot path. The engine constructs temporary BlockSources thousands of times per
    // second, so repeat observations of the world it is already using only refresh the
    // liveness beat: no syscall-backed read, no lock, no table write.
    const auto now = now_ns();
    const bool same_context = observed_level.load(std::memory_order_relaxed) == level_address &&
        observed_dimension.load(std::memory_order_relaxed) == dimension_address;
    const auto last_observed = observed_ns.load(std::memory_order_relaxed);
    const bool stale_context = !last_observed || now - last_observed > kCaptureFreshnessNs;
    if (same_context && !stale_context) {
        if (now - last_observed > kObserveBeatNs) observed_ns.store(now, std::memory_order_relaxed);
        return;
    }

    // The constructor's own arguments, read directly. These are live pointers the engine
    // just passed to the function we hooked, exactly as the constructor dereferences them;
    // nothing here is an address recovered from engine memory.
    const auto source = reinterpret_cast<uintptr_t>(self);
    const auto chunk_address = reinterpret_cast<uintptr_t>(chunk_source);
    uintptr_t vtable = 0;
    std::memcpy(&vtable, reinterpret_cast<const void*>(source), sizeof vtable);
    if (vtable != base + kBlockSourceVtable) {
        reject("blocksource_vtable", vtable);
        return;
    }
    int16_t minimum = 0, maximum = 0;
    std::memcpy(&maximum, reinterpret_cast<const void*>(source + kMaxHeightField), sizeof maximum);
    std::memcpy(&minimum, reinterpret_cast<const void*>(source + kMinHeightField), sizeof minimum);
    if (maximum <= minimum) {
        reject("height_range", (static_cast<uintptr_t>(static_cast<uint16_t>(maximum)) << 16) |
                               static_cast<uint16_t>(minimum));
        return;
    }
    std::memcpy(&vtable, reinterpret_cast<const void*>(level_address), sizeof vtable);
    if (vtable != base + kLevelVtable) {
        reject("level_vtable", vtable);
        return;
    }
    std::memcpy(&vtable, reinterpret_cast<const void*>(dimension_address), sizeof vtable);
    if (vtable != base + kDimensionVtable) {
        reject("dimension_vtable", vtable);
        return;
    }
    uintptr_t chunk_vtable = 0;
    std::memcpy(&chunk_vtable, reinterpret_cast<const void*>(chunk_address), sizeof chunk_vtable);
    if (chunk_vtable < 8) {
        reject("chunk_vtable", chunk_vtable);
        return;
    }
    int16_t dimension_id = 0;
    std::memcpy(&dimension_id, reinterpret_cast<const void*>(dimension_address + kDimensionIdField),
                sizeof dimension_id);

    const uint32_t tid = static_cast<uint32_t>(gettid());
    uint32_t expected_thread = 0;
    // The first thread seen constructing a BlockSource is the engine thread that
    // deferred reads are handed to; observations from every engine thread are recorded.
    engine_thread.compare_exchange_strong(expected_thread, tid, std::memory_order_acq_rel);

    std::lock_guard<std::mutex> guard(capture_mutex);
    Capture* capture = find_capture(dimension_id);
    if (!capture) {
        capture = &captures[0];
        for (auto& candidate : captures)
            if ((!candidate.level && !capture->level) || candidate.serial < capture->serial)
                capture = &candidate;
        capture->generation = next_generation.fetch_add(1, std::memory_order_relaxed);
    } else if (capture->level != level_address || capture->dimension != dimension_address ||
               (same_context && stale_context)) {
        capture->generation = next_generation.fetch_add(1, std::memory_order_relaxed);
    }
    capture->level = level_address;
    capture->dimension = dimension_address;
    capture->dimension_id = dimension_id;
    capture->serial = next_serial.fetch_add(1, std::memory_order_relaxed);
    observed_level.store(level_address, std::memory_order_relaxed);
    observed_dimension.store(dimension_address, std::memory_order_relaxed);
    observed_ns.store(now_ns(), std::memory_order_relaxed);
    if (capture_observations.fetch_add(1, std::memory_order_relaxed) < 4)
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
            "sdk_world_capture;pid=%d;tid=%d;dimension=%d;min=%d;max=%d;world_generation=%llu",
            getpid(), gettid(), static_cast<int>(dimension_id), static_cast<int>(minimum),
            static_cast<int>(maximum), static_cast<unsigned long long>(capture->generation));
}

bool capture_still_valid(const EngineImage& image, const Capture& capture) {
    if (!capture.level || !capture.dimension) return false;
    // Only the world the engine is using right now may be read. A capture whose Level and
    // Dimension the engine has stopped constructing BlockSources for belongs to a world
    // that was left behind, and its borrowed pointers are never dereferenced again.
    if (observed_level.load(std::memory_order_relaxed) != capture.level ||
        observed_dimension.load(std::memory_order_relaxed) != capture.dimension)
        return false;
    if (now_ns() - observed_ns.load(std::memory_order_relaxed) > kCaptureFreshnessNs) return false;
    SelfMemory memory;
    uintptr_t vtable = 0;
    if (!memory.read(capture.level, &vtable, sizeof vtable) || vtable != image.base + kLevelVtable)
        return false;
    if (!memory.read(capture.dimension, &vtable, sizeof vtable) ||
        vtable != image.base + kDimensionVtable)
        return false;
    return validate_layout(image, kLevelLayout) && validate_layout(image, kDimensionLayout);
}

bool find_capture(uint64_t world_id, Capture& out) {
    const auto generation = world_id >> 32;
    const auto dimension_id = static_cast<int32_t>(static_cast<uint32_t>(world_id));
    std::lock_guard<std::mutex> guard(capture_mutex);
    for (const auto& capture : captures) {
        if (capture.generation != generation || capture.dimension_id != dimension_id) continue;
        out = capture;
        return true;
    }
    return false;
}

// Runs on an engine thread only. The chunk lookup is the engine's own read path.
int32_t read_block_here(const EngineImage& image, uint64_t world_id, int32_t x, int32_t y,
                        int32_t z, void* source_object, void* level_object,
                        void* dimension_object, EngineWorldBlock& out) {
    if (!source_object || !level_object || !dimension_object) return HYBRID_SDK_UNSUPPORTED;
    Capture capture;
    if (!find_capture(world_id, capture) || capture.level != reinterpret_cast<uintptr_t>(level_object) ||
        capture.dimension != reinterpret_cast<uintptr_t>(dimension_object) ||
        !capture_still_valid(image, capture)) return HYBRID_SDK_UNSUPPORTED;

    SelfMemory memory;
    int16_t live_dimension_id = 0;
    if (!memory.read(capture.dimension + kDimensionIdField, &live_dimension_id,
                     sizeof live_dimension_id) || live_dimension_id != capture.dimension_id)
        return HYBRID_SDK_UNSUPPORTED;
    const auto source = reinterpret_cast<uintptr_t>(source_object);
    uintptr_t vtable = 0;
    uintptr_t source_level = 0, source_chunk = 0, source_dimension = 0;
    int16_t minimum = 0, maximum = 0;
    const bool built = memory.read(source, &vtable, sizeof vtable) &&
        vtable == image.base + kBlockSourceVtable &&
        memory.read(source + 0x28, &source_level, sizeof source_level) &&
        source_level == capture.level &&
        memory.read(source + 0x30, &source_chunk, sizeof source_chunk) && source_chunk &&
        memory.read(source + 0x38, &source_dimension, sizeof source_dimension) &&
        source_dimension == capture.dimension &&
        memory.read(source + kMinHeightField, &minimum, sizeof minimum) &&
        memory.read(source + kMaxHeightField, &maximum, sizeof maximum) && maximum > minimum;
    if (built && (y < minimum || y >= maximum)) return HYBRID_SDK_NOT_FOUND;
    uintptr_t block = 0;
    if (built) {
        struct Position {
            int32_t x, y, z;
        } position{x, y, z};
        using GetBlock = uintptr_t (*)(void*, void*);
        block = reinterpret_cast<GetBlock>(image.base + kBlockSourceGet)(source_object, &position);
    }
    if (!built || !block) return HYBRID_SDK_UNSUPPORTED;

    if (!memory.read(block, &vtable, sizeof vtable) || vtable != image.base + kBlockVtable)
        return HYBRID_SDK_UNSUPPORTED;
    uintptr_t type = 0;
    if (!memory.read(block + kBlockTypeField, &type, sizeof type) || !type)
        return HYBRID_SDK_UNSUPPORTED;
    uintptr_t type_vtable = 0;
    if (!memory.read(type, &type_vtable, sizeof type_vtable) || type_vtable < 8 ||
        !image.contains(type_vtable - 8, 16, PF_R))
        return HYBRID_SDK_UNSUPPORTED;

    EngineWorldBlock result{};
    if (!read_name(memory, type + kTypeNameField, result.identifier) ||
        !well_formed_identifier(result.identifier))
        return HYBRID_SDK_UNSUPPORTED;
    uint16_t numeric = 0;
    if (!memory.read(type + kTypeNumericField, &numeric, sizeof numeric))
        return HYBRID_SDK_UNSUPPORTED;
    uintptr_t bounds[3]{};
    if (!memory.read(type + kTypeStateBoundsField, bounds, sizeof bounds) || bounds[1] < bounds[0] ||
        bounds[2] < bounds[1] || ((bounds[1] - bounds[0]) % 8) || (bounds[1] - bounds[0]) / 8 > 4096)
        return HYBRID_SDK_UNSUPPORTED;
    uint32_t state_hash = 0;
    uint8_t assigned = 0;
    if (!memory.read(block + kBlockStateHashField, &state_hash, sizeof state_hash) ||
        !memory.read(block + kBlockAssignField, &assigned, sizeof assigned) || assigned > 1)
        return HYBRID_SDK_UNSUPPORTED;

    result.numeric_id = numeric;
    result.state_count = static_cast<uint32_t>((bounds[1] - bounds[0]) / 8);
    result.state_hash = state_hash;
    result.hash_assigned = assigned;
    result.dimension_id = capture.dimension_id;
    out = result;
    return HYBRID_SDK_OK;
}

void service_pending(void* source, void* level, void* dimension) {
    if (pending_state.load(std::memory_order_acquire) != kRequested) return;
    const uint32_t tid = static_cast<uint32_t>(gettid());
    const uint32_t owner = engine_thread.load(std::memory_order_relaxed);
    if (owner && tid != owner && now_ns() - pending.started_ns < kPreferredThreadDelayNs) return;
    uint32_t expected = kRequested;
    if (!pending_state.compare_exchange_strong(expected, kServicing, std::memory_order_acq_rel))
        return;
    EngineImage image;
    EngineWorldBlock result{};
    int32_t status = HYBRID_SDK_UNSUPPORTED;
    if (admitted(image))
        status = read_block_here(image, pending.world_id, pending.x, pending.y, pending.z,
                                 source, level, dimension, result);
    pending.result = result;
    pending.status = status;
    if (status == HYBRID_SDK_OK)
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
            "sdk_world_service;pid=%d;tid=%d;engine_tid=%u;preferred=%d;dimension=%d;pos=%d,%d,%d;"
            "identifier=%s;numeric=%u;states=%u",
            getpid(), gettid(), owner, static_cast<int>(tid == owner), pending.result.dimension_id,
            pending.x, pending.y, pending.z, pending.result.identifier, pending.result.numeric_id,
            pending.result.state_count);
    pending_state.store(kDone, std::memory_order_release);
}

void blocksource_ctor_proxy(void* self, void* level, void* dimension, void* chunk_source,
                            uint32_t flag4, uint32_t flag5, uint32_t flag6, uint32_t flag7) {
    SHADOWHOOK_STACK_SCOPE();
    const auto hit = constructor_hook_hits.fetch_add(1, std::memory_order_relaxed);
    if (hit < 3)
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
            "sdk_world_ctor_hit;pid=%d;tid=%d;self=0x%llx;level=0x%llx;dimension=0x%llx;"
            "chunk_source=0x%llx;sample=%u",
            getpid(), gettid(), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(self)),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(level)),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(dimension)),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(chunk_source)), hit + 1);
    if (ctor_previous)
        ctor_previous(self, level, dimension, chunk_source, flag4, flag5, flag6, flag7);
    record_capture(self, level, dimension, chunk_source);
    service_pending(self, level, dimension);
}

bool install_hook() {
    if (hook_ready.load(std::memory_order_acquire)) return true;
    const auto base = verified_base.load(std::memory_order_acquire);
    if (!base) return false;
    if (ctor_stub && ctor_previous) {
        hook_ready.store(true, std::memory_order_release);
        return true;
    }
    if (shadowhook_init(SHADOWHOOK_MODE_SHARED, false)) return false;
    ctor_stub = shadowhook_hook_func_addr(reinterpret_cast<void*>(base + kBlockSourceCtor),
        reinterpret_cast<void*>(&blocksource_ctor_proxy),
        reinterpret_cast<void**>(&ctor_previous));
    const bool ready = ctor_stub && ctor_previous;
    hook_ready.store(ready, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
        "sdk_world_hook;install=%d;errno=%d;pid=%d;target_rva=0x%llx",
        static_cast<int>(ready), shadowhook_get_errno(), getpid(),
        static_cast<unsigned long long>(kBlockSourceCtor));
    return ready;
}
}

void engine_world_read_configure(const EngineVerification& verification) {
    if (!verification.verified || !verification.profile ||
        std::strcmp(verification.profile->id, profile_id))
        return;
    EngineImage image;
    if (!dl_iterate_phdr(locate, &image) || !image.base) return;
    verified_base.store(image.base, std::memory_order_release);
    image_cache = image;
    capture_observations.store(0, std::memory_order_release);
    constructor_hook_hits.store(0, std::memory_order_release);
    capture_rejections.store(0, std::memory_order_release);
    EngineImage check;
    if (!admitted(check)) {
        verified_base.store(0, std::memory_order_release);
        image_cache = EngineImage{};
        return;
    }
    install_hook();
}

void engine_world_read_shutdown() {
    bool detached = true;
    if (ctor_stub) {
        const int result = shadowhook_unhook(ctor_stub);
        if (!result) ctor_stub = nullptr;
        else detached = false;
        __android_log_print(ANDROID_LOG_INFO, "HybridMCPE",
            "sdk_world_hook;cleanup=%d;pid=%d;retained=%d", result, getpid(),
            static_cast<int>(ctor_stub != nullptr));
    }
    hook_ready.store(false, std::memory_order_release);
    pending_state.store(kIdle, std::memory_order_release);
    engine_thread.store(0, std::memory_order_release);
    std::lock_guard<std::mutex> guard(capture_mutex);
    for (auto& capture : captures) capture = Capture{};
    image_cache = EngineImage{};
    verified_base.store(0, std::memory_order_release);
    if (detached) ctor_previous = nullptr;
}

bool engine_world_read_supported() {
    return verified_base.load(std::memory_order_acquire) != 0 &&
        hook_ready.load(std::memory_order_acquire);
}

int32_t engine_world_current(EngineWorldRef& out) {
    EngineImage image;
    if (!admitted(image)) return HYBRID_SDK_UNSUPPORTED;
    Capture capture;
    {
        std::lock_guard<std::mutex> guard(capture_mutex);
        const Capture* newest = nullptr;
        for (const auto& candidate : captures)
            if (candidate.level && (!newest || candidate.serial > newest->serial))
                newest = &candidate;
        if (!newest) return HYBRID_SDK_UNSUPPORTED;
        capture = *newest;
    }
    if (!capture_still_valid(image, capture)) return HYBRID_SDK_UNSUPPORTED;
    out.world_id = (capture.generation << 32) | static_cast<uint64_t>(
        static_cast<uint32_t>(capture.dimension_id));
    out.dimension_id = capture.dimension_id;
    return HYBRID_SDK_OK;
}

int32_t engine_world_block(uint64_t world_id, int32_t x, int32_t y, int32_t z,
                           EngineWorldBlock& out) {
    if (!world_id) return HYBRID_SDK_INVALID_ARGUMENT;
    EngineImage image;
    if (!admitted(image)) return HYBRID_SDK_UNSUPPORTED;
    // No capture for this generation/dimension means the world it named is gone; fail
    // fast instead of making the caller wait for a service that can never succeed.
    Capture capture;
    if (!find_capture(world_id, capture) || !capture_still_valid(image, capture))
        return HYBRID_SDK_UNSUPPORTED;
    const uint32_t tid = static_cast<uint32_t>(gettid());
    const uint32_t owner = engine_thread.load(std::memory_order_relaxed);
    // A game-thread caller outside the constructor hook cannot block waiting for its
    // own next BlockSource. It has no live BlockSource object to read from.
    if (owner && tid == owner) return HYBRID_SDK_UNSUPPORTED;
    uint32_t finished = kDone;
    pending_state.compare_exchange_strong(finished, kIdle, std::memory_order_acq_rel);
    uint32_t expected = kIdle;
    if (!pending_state.compare_exchange_strong(expected, kFilling, std::memory_order_acq_rel))
        return HYBRID_SDK_UNSUPPORTED;
    pending.world_id = world_id;
    pending.x = x;
    pending.y = y;
    pending.z = z;
    pending.status = HYBRID_SDK_UNSUPPORTED;
    pending.started_ns = now_ns();
    pending_state.store(kRequested, std::memory_order_release);
    const uint64_t deadline = pending.started_ns + kServiceDeadlineNs;
    int32_t status = HYBRID_SDK_UNSUPPORTED;
    for (;;) {
        const uint32_t state = pending_state.load(std::memory_order_acquire);
        if (state == kDone) {
            status = pending.status;
            if (status == HYBRID_SDK_OK) out = pending.result;
            pending_state.store(kIdle, std::memory_order_release);
            return status;
        }
        if (state == kRequested && now_ns() > deadline) {
            uint32_t wanted = kRequested;
            if (pending_state.compare_exchange_strong(wanted, kIdle, std::memory_order_acq_rel))
                return HYBRID_SDK_UNSUPPORTED;
            continue;
        }
        if (now_ns() > deadline + kServiceDeadlineNs) {
            return HYBRID_SDK_UNSUPPORTED;
        }
        ::usleep(200);
    }
}
}
#else
namespace hybrid {
void engine_world_read_configure(const EngineVerification&) {}
void engine_world_read_shutdown() {}
bool engine_world_read_supported() { return false; }
int32_t engine_world_current(EngineWorldRef& out) {
    (void)out;
    return HYBRID_SDK_UNSUPPORTED;
}
int32_t engine_world_block(uint64_t, int32_t, int32_t, int32_t, EngineWorldBlock&) {
    return HYBRID_SDK_UNSUPPORTED;
}
}
#endif
