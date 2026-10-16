#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace hybrid::iron_chest {

constexpr size_t slot_count = 54;

struct ItemStack {
    std::string item_id;
    int32_t variant = 0;
    uint32_t count = 0;
    std::vector<uint8_t> metadata;
};

struct ItemKey {
    std::string item_id;
    int32_t variant = 0;
    bool operator<(const ItemKey& other) const;
};

class StackLimits {
public:
    bool set(const std::string& item_id, int32_t variant, uint32_t limit);
    bool get(const ItemStack& stack, uint32_t& limit) const;

private:
    std::map<ItemKey, uint32_t> limits_;
};

enum class OperationType : uint8_t { insert, extract, split, merge, swap };

struct Operation {
    OperationType type = OperationType::insert;
    size_t from = 0;
    size_t to = 0;
    uint32_t count = 0;
    ItemStack stack;
};

struct Transaction {
    uint64_t id = 0;
    uint64_t expected_revision = 0;
    std::vector<Operation> operations;
};

struct TransactionResult {
    bool committed = false;
    uint64_t revision = 0;
    std::vector<ItemStack> extracted;
};

class Container {
public:
    Container() = default;
    Container(const Container&) = delete;
    Container& operator=(const Container&) = delete;

    TransactionResult apply(const Transaction& transaction, const StackLimits& limits);
    bool get_slot(size_t index, ItemStack& output) const;
    bool snapshot(std::array<ItemStack, slot_count>& output, uint64_t& revision) const;
    bool is_open() const;
    void close();

    bool serialize(std::vector<uint8_t>& output) const;
    bool deserialize(const uint8_t* data, size_t size);

private:
    mutable std::mutex mutex_;
    std::array<ItemStack, slot_count> slots_{};
    uint64_t revision_ = 0;
    uint64_t last_transaction_id_ = 0;
    bool open_ = true;
};

}  // namespace hybrid::iron_chest
