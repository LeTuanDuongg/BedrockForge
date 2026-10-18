#include "iron_chest_container.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace hybrid::iron_chest;

namespace {
void check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

ItemStack item(const char* id, uint32_t count, std::vector<uint8_t> metadata = {}) {
    return ItemStack{id, 0, count, std::move(metadata)};
}

Transaction tx(uint64_t id, uint64_t revision, Operation operation) {
    return Transaction{id, revision, {std::move(operation)}};
}

Operation insert(ItemStack stack) {
    Operation op;
    op.type = OperationType::insert;
    op.stack = std::move(stack);
    return op;
}

Operation transfer(OperationType type, size_t from, size_t to, uint32_t count) {
    Operation op;
    op.type = type;
    op.from = from;
    op.to = to;
    op.count = count;
    return op;
}

Operation extract(size_t from, uint32_t count) {
    Operation op;
    op.type = OperationType::extract;
    op.from = from;
    op.count = count;
    return op;
}

Operation swap_slots(size_t from, size_t to) {
    Operation op;
    op.type = OperationType::swap;
    op.from = from;
    op.to = to;
    return op;
}

ItemStack slot(Container& container, size_t index) {
    ItemStack result;
    check(container.get_slot(index, result), "read valid slot");
    return result;
}

uint64_t total_count(const std::array<ItemStack, slot_count>& slots, const char* item_id) {
    uint64_t total = 0;
    for (const auto& stack : slots) if (stack.item_id == item_id) total += stack.count;
    return total;
}
}

int main() {
    StackLimits limits;
    check(limits.set("mod:ingot", 0, 16), "configure non-default stack limit");
    check(limits.set("minecraft:stone", 0, 64), "configure second stack limit");
    check(limits.set("mod:overflow", 0, std::numeric_limits<uint32_t>::max()), "configure uint32 limit");
    check(!limits.set("", 0, 1) && !limits.set("mod:bad", 0, 0), "reject invalid stack limits");

    Container chest;
    auto result = chest.apply(tx(1, 0, insert(item("mod:ingot", 20, {1, 2, 3}))), limits);
    check(result.committed && result.revision == 1, "insert distributes across slots");
    check(slot(chest, 0).count == 16 && slot(chest, 1).count == 4, "custom limit applied");
    std::array<ItemStack, slot_count> conservation{};
    uint64_t conservation_revision = 0;
    check(chest.snapshot(conservation, conservation_revision) &&
          total_count(conservation, "mod:ingot") == 20, "insertion conserves item count");

    result = chest.apply(tx(2, 1, extract(0, 5)), limits);
    check(result.committed && result.extracted.size() == 1 && result.extracted[0].count == 5,
          "extract returns removed stack");
    check(slot(chest, 0).count == 11, "extract reduces source");
    check(chest.snapshot(conservation, conservation_revision) &&
          total_count(conservation, "mod:ingot") + result.extracted[0].count == 20,
          "extraction conserves inventory plus returned items");

    result = chest.apply(tx(3, 2, transfer(OperationType::split, 0, 2, 3)), limits);
    check(result.committed && slot(chest, 0).count == 8 && slot(chest, 2).count == 3,
          "split moves items to empty slot");
    result = chest.apply(tx(4, 3, transfer(OperationType::merge, 2, 1, 2)), limits);
    check(result.committed && slot(chest, 1).count == 6 && slot(chest, 2).count == 1,
          "merge moves requested count");
    result = chest.apply(tx(5, 4, swap_slots(1, 2)), limits);
    check(result.committed && slot(chest, 1).count == 1 && slot(chest, 2).count == 6,
          "swap exchanges slots");
    check(chest.snapshot(conservation, conservation_revision) &&
          total_count(conservation, "mod:ingot") == 15, "split, merge and swap conserve item count");

    const auto before_revision = result.revision;
    Transaction rollback{6, before_revision, {insert(item("minecraft:stone", 1)), extract(54, 1)}};
    result = chest.apply(rollback, limits);
    check(!result.committed && result.extracted.empty() && result.revision == before_revision,
          "failed transaction returns no partial outputs or revision change");
    check(slot(chest, 3).count == 0, "failed transaction rolls back earlier insert");
    check(!chest.apply(tx(5, before_revision, insert(item("minecraft:stone", 1))), limits).committed,
          "reject duplicate transaction id");
    check(!chest.apply(tx(7, 0, insert(item("minecraft:stone", 1))), limits).committed,
          "reject stale expected revision");
    check(!chest.apply(tx(7, before_revision, insert(item("unknown:item", 1))), limits).committed,
          "reject item without configured limit");
    check(!chest.apply(tx(7, before_revision, insert(item("mod:ingot", 0))), limits).committed,
          "reject zero-count insertion");

    std::vector<uint8_t> saved;
    check(chest.serialize(saved), "serialize container");
    std::vector<uint8_t> saved_again;
    check(chest.serialize(saved_again) && saved == saved_again, "serialization is deterministic");
    Container restored;
    check(restored.deserialize(saved.data(), saved.size()), "deserialize valid save");
    std::array<ItemStack, slot_count> original_slots{}, restored_slots{};
    uint64_t original_revision = 0, restored_revision = 0;
    check(chest.snapshot(original_slots, original_revision) &&
          restored.snapshot(restored_slots, restored_revision), "snapshot round trip");
    check(original_revision == restored_revision && original_slots.size() == 54, "save retains revision and 54 slots");
    for (size_t i = 0; i < slot_count; ++i)
        check(original_slots[i].item_id == restored_slots[i].item_id &&
              original_slots[i].variant == restored_slots[i].variant &&
              original_slots[i].count == restored_slots[i].count &&
              original_slots[i].metadata == restored_slots[i].metadata, "save retains each stack");

    auto corrupt = saved;
    corrupt[10] ^= 0x40;
    check(!restored.deserialize(corrupt.data(), corrupt.size()), "reject corrupt checksum");
    auto unsupported = saved;
    unsupported[4] = 2;
    check(!restored.deserialize(unsupported.data(), unsupported.size()), "reject unsupported data version");
    check(!restored.deserialize(saved.data(), saved.size() - 1), "reject truncated save");
    check(restored.snapshot(restored_slots, restored_revision) && restored_revision == original_revision,
          "failed loads preserve prior contents");
    check(!restored.apply(tx(5, restored_revision, insert(item("minecraft:stone", 1))), limits).committed,
          "save round trip preserves duplicate transaction protection");

    Container full;
    check(full.apply(tx(1, 0, insert(item("mod:ingot", 54 * 16))), limits).committed,
          "fill all 54 slots");
    check(!full.apply(tx(2, 1, insert(item("mod:ingot", 1))), limits).committed,
          "reject insertion when no capacity remains");
    check(slot(full, 53).count == 16, "full-container failure is atomic");

    Container max_count;
    for (uint64_t id = 1; id <= slot_count; ++id)
        check(max_count.apply(tx(id, id - 1,
              insert(item("mod:overflow", std::numeric_limits<uint32_t>::max()))), limits).committed,
              "accept configured maximum count without integer overflow");
    check(!max_count.apply(tx(slot_count + 1, slot_count, insert(item("mod:overflow", 1))), limits).committed,
          "reject insertion after all uint32-capacity slots fill");
    check(slot(max_count, slot_count - 1).count == std::numeric_limits<uint32_t>::max(),
          "overflow rejection preserves maximum stack");

    chest.close();
    check(!chest.is_open() && !chest.get_slot(0, original_slots[0]), "closed container rejects reads");
    check(!chest.get_slot(slot_count, original_slots[0]), "reject out-of-range slot read");
    check(!chest.apply(tx(6, before_revision, insert(item("minecraft:stone", 1))), limits).committed,
          "closed container rejects transactions");
    check(chest.serialize(saved), "closed container remains serializable for cleanup");

    std::puts("PASS: 54-slot container transactions, rollback, limits, serialization, corruption and lifecycle");
}
