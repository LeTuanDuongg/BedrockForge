#include "iron_chest_container.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace hybrid::iron_chest {
namespace {
constexpr uint8_t magic[] = {'H', 'I', 'C', '1'};
constexpr uint16_t data_version = 1;
constexpr size_t max_item_id = 256;
constexpr size_t max_metadata = 1024 * 1024;
constexpr size_t max_save_size = 8 * 1024 * 1024;

bool empty(const ItemStack& stack) {
    return stack.item_id.empty() && stack.count == 0 && stack.variant == 0 && stack.metadata.empty();
}

bool valid_stack(const ItemStack& stack) {
    if (stack.item_id.size() > max_item_id || stack.metadata.size() > max_metadata) return false;
    return stack.count == 0 ? empty(stack) : !stack.item_id.empty();
}

bool same_item(const ItemStack& left, const ItemStack& right) {
    return left.item_id == right.item_id && left.variant == right.variant && left.metadata == right.metadata;
}

void write_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void write_u32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

void write_u64(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

bool read_bytes(const uint8_t*& cursor, const uint8_t* end, void* output, size_t size) {
    if (size > static_cast<size_t>(end - cursor)) return false;
    std::memcpy(output, cursor, size);
    cursor += size;
    return true;
}

bool read_u16(const uint8_t*& cursor, const uint8_t* end, uint16_t& value) {
    uint8_t bytes[2];
    if (!read_bytes(cursor, end, bytes, sizeof(bytes))) return false;
    value = static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
    return true;
}

bool read_u32(const uint8_t*& cursor, const uint8_t* end, uint32_t& value) {
    uint8_t bytes[4];
    if (!read_bytes(cursor, end, bytes, sizeof(bytes))) return false;
    value = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

bool read_u64(const uint8_t*& cursor, const uint8_t* end, uint64_t& value) {
    uint8_t bytes[8];
    if (!read_bytes(cursor, end, bytes, sizeof(bytes))) return false;
    value = 0;
    for (unsigned i = 0; i < sizeof(bytes); ++i) value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    return true;
}

uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool read_stack(const uint8_t*& cursor, const uint8_t* end, ItemStack& stack) {
    uint16_t id_size;
    uint32_t variant, metadata_size;
    if (!read_u16(cursor, end, id_size) || id_size > max_item_id ||
        static_cast<size_t>(end - cursor) < id_size) return false;
    stack.item_id.assign(reinterpret_cast<const char*>(cursor), id_size);
    cursor += id_size;
    if (!read_u32(cursor, end, variant) || !read_u32(cursor, end, stack.count) ||
        !read_u32(cursor, end, metadata_size) || metadata_size > max_metadata ||
        static_cast<size_t>(end - cursor) < metadata_size) return false;
    stack.variant = static_cast<int32_t>(variant);
    stack.metadata.assign(cursor, cursor + metadata_size);
    cursor += metadata_size;
    return valid_stack(stack);
}

bool add_count(ItemStack& destination, uint32_t count, uint32_t limit) {
    if (count == 0 || destination.count > limit || count > limit - destination.count) return false;
    destination.count += count;
    return true;
}
}  // namespace

bool ItemKey::operator<(const ItemKey& other) const {
    return item_id < other.item_id || (item_id == other.item_id && variant < other.variant);
}

bool StackLimits::set(const std::string& item_id, int32_t variant, uint32_t limit) {
    if (item_id.empty() || item_id.size() > max_item_id || limit == 0) return false;
    limits_[ItemKey{item_id, variant}] = limit;
    return true;
}

bool StackLimits::get(const ItemStack& stack, uint32_t& limit) const {
    const auto found = limits_.find(ItemKey{stack.item_id, stack.variant});
    if (found == limits_.end()) return false;
    limit = found->second;
    return true;
}

TransactionResult Container::apply(const Transaction& transaction, const StackLimits& limits) {
    std::lock_guard<std::mutex> guard(mutex_);
    TransactionResult result{false, revision_, {}};
    if (!open_ || transaction.id == 0 || transaction.id <= last_transaction_id_ ||
        transaction.expected_revision != revision_ || transaction.operations.empty() ||
        revision_ == std::numeric_limits<uint64_t>::max()) return result;

    auto next = slots_;
    std::vector<ItemStack> extracted_items;
    for (const auto& operation : transaction.operations) {
        if (operation.type == OperationType::insert) {
            if (!valid_stack(operation.stack) || operation.stack.count == 0) return result;
            uint32_t item_limit;
            if (!limits.get(operation.stack, item_limit)) return result;
            uint32_t remaining = operation.stack.count;
            for (auto& slot : next) {
                if (!empty(slot) && same_item(slot, operation.stack)) {
                    const uint32_t moved = std::min(remaining, item_limit - std::min(item_limit, slot.count));
                    slot.count += moved;
                    remaining -= moved;
                    if (remaining == 0) break;
                }
            }
            for (auto& slot : next) {
                if (remaining == 0) break;
                if (empty(slot)) {
                    slot = operation.stack;
                    slot.count = std::min(remaining, item_limit);
                    remaining -= slot.count;
                }
            }
            if (remaining != 0) return result;
        } else if (operation.type == OperationType::extract) {
            if (operation.from >= slot_count || operation.count == 0 || empty(next[operation.from]) ||
                operation.count > next[operation.from].count) return result;
            ItemStack extracted = next[operation.from];
            extracted.count = operation.count;
            next[operation.from].count -= operation.count;
            if (next[operation.from].count == 0) next[operation.from] = {};
            extracted_items.push_back(std::move(extracted));
        } else if (operation.type == OperationType::split || operation.type == OperationType::merge) {
            if (operation.from >= slot_count || operation.to >= slot_count || operation.from == operation.to ||
                operation.count == 0 || empty(next[operation.from]) || operation.count > next[operation.from].count)
                return result;
            auto& source = next[operation.from];
            auto& destination = next[operation.to];
            if (operation.type == OperationType::split && !empty(destination)) return result;
            if (!empty(destination) && !same_item(source, destination)) return result;
            uint32_t limit;
            if (!limits.get(source, limit)) return result;
            if (empty(destination)) {
                destination = source;
                destination.count = 0;
            } else if (!limits.get(destination, limit)) return result;
            if (!add_count(destination, operation.count, limit)) return result;
            source.count -= operation.count;
            if (source.count == 0) source = {};
        } else if (operation.type == OperationType::swap) {
            if (operation.from >= slot_count || operation.to >= slot_count || operation.from == operation.to)
                return result;
            std::swap(next[operation.from], next[operation.to]);
        } else {
            return result;
        }
    }

    slots_ = std::move(next);
    ++revision_;
    last_transaction_id_ = transaction.id;
    result.committed = true;
    result.revision = revision_;
    result.extracted = std::move(extracted_items);
    return result;
}

bool Container::get_slot(size_t index, ItemStack& output) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!open_ || index >= slot_count) return false;
    output = slots_[index];
    return true;
}

bool Container::snapshot(std::array<ItemStack, slot_count>& output, uint64_t& revision) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!open_) return false;
    output = slots_;
    revision = revision_;
    return true;
}

bool Container::is_open() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return open_;
}

void Container::close() {
    std::lock_guard<std::mutex> guard(mutex_);
    open_ = false;
}

bool Container::serialize(std::vector<uint8_t>& output) const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<uint8_t> encoded;
    encoded.insert(encoded.end(), std::begin(magic), std::end(magic));
    write_u16(encoded, data_version);
    write_u16(encoded, static_cast<uint16_t>(slot_count));
    write_u64(encoded, revision_);
    write_u64(encoded, last_transaction_id_);
    for (const auto& stack : slots_) {
        if (!valid_stack(stack)) return false;
        write_u16(encoded, static_cast<uint16_t>(stack.item_id.size()));
        encoded.insert(encoded.end(), stack.item_id.begin(), stack.item_id.end());
        write_u32(encoded, static_cast<uint32_t>(stack.variant));
        write_u32(encoded, stack.count);
        write_u32(encoded, static_cast<uint32_t>(stack.metadata.size()));
        encoded.insert(encoded.end(), stack.metadata.begin(), stack.metadata.end());
        if (encoded.size() > max_save_size - sizeof(uint32_t)) return false;
    }
    write_u32(encoded, crc32(encoded.data(), encoded.size()));
    output = std::move(encoded);
    return true;
}

bool Container::deserialize(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(magic) + 2 + 2 + 8 + 8 + slot_count * (2 + 4 + 4 + 4) + 4 ||
        size > max_save_size) return false;
    const auto expected_crc = crc32(data, size - sizeof(uint32_t));
    const uint8_t* cursor = data;
    const uint8_t* end = data + size - sizeof(uint32_t);
    uint8_t stored_magic[sizeof(magic)];
    uint16_t version, count;
    uint64_t revision, transaction_id;
    if (!read_bytes(cursor, end, stored_magic, sizeof(stored_magic)) ||
        std::memcmp(stored_magic, magic, sizeof(magic)) != 0 ||
        !read_u16(cursor, end, version) || version != data_version || !read_u16(cursor, end, count) ||
        count != slot_count || !read_u64(cursor, end, revision) || !read_u64(cursor, end, transaction_id) ||
        ((revision == 0) != (transaction_id == 0)) || transaction_id < revision)
        return false;

    std::array<ItemStack, slot_count> decoded{};
    for (auto& stack : decoded) if (!read_stack(cursor, end, stack)) return false;
    if (cursor != end) return false;
    const uint8_t* crc_cursor = data + size - sizeof(uint32_t);
    uint32_t stored_crc;
    if (!read_u32(crc_cursor, data + size, stored_crc) || stored_crc != expected_crc) return false;

    std::lock_guard<std::mutex> guard(mutex_);
    if (!open_) return false;
    slots_ = std::move(decoded);
    revision_ = revision;
    last_transaction_id_ = transaction_id;
    return true;
}

}  // namespace hybrid::iron_chest
