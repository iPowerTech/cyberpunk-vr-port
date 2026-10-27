#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// Lock-free read side of the vtable original-method registry.
//
// GetOriginalMethod used to take one process-wide std::mutex and search an unordered_map on EVERY
// hooked call. Several hooks sit on the hottest paths of the render jobs (ID3D12Device::
// CopyDescriptors, ResourceBarrier, CreateShaderResourceView...), so fifteen dispatcher threads
// serialized on that mutex: a live stack sample of the dense-city scene (single-pass-ceiling doc,
// run city-805) found worker threads asleep in RtlSleepConditionVariableSRW under it.
//
// The registry is append-only: PatchVtableMethod is its only writer, inserts an original only if
// the key is absent, never erases or replaces one, and publishes BEFORE exposing the detour in the
// vtable. So readers can probe without a lock: the value is stored before the key is released,
// and a reader that acquires the key sees the value. Writers stay serialized by the caller's mutex.
// A full table or a long probe falls back to the locked map, so behavior never depends on capacity.
namespace cvr::hooks {

template <size_t Capacity = 4096>
class OriginalMethodTable {
    static_assert(Capacity >= 16 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static constexpr unsigned Log2() {
        unsigned bits = 0;
        for (size_t value = Capacity; value > 1; value >>= 1) ++bits;
        return bits;
    }
    struct Entry {
        std::atomic<uintptr_t> key{0};
        std::atomic<void*> value{nullptr};
    };
    Entry entries_[Capacity];

    static size_t Home(uintptr_t key) {
        return static_cast<size_t>((static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) >> (64 - Log2()));
    }

public:
    static constexpr size_t kMaxProbe = 64;

    // Caller serializes writers. Returns false only when the key cannot be placed (zero key/value
    // or probe overflow); the locked map then remains the authority for that key.
    bool Publish(uintptr_t key, void* value) {
        if (!key || !value) return false;
        size_t slot = Home(key);
        for (size_t i = 0; i < kMaxProbe; ++i, slot = (slot + 1) & (Capacity - 1)) {
            const uintptr_t existing = entries_[slot].key.load(std::memory_order_relaxed);
            if (existing == key) return true;  // first original wins, exactly like the map
            if (!existing) {
                entries_[slot].value.store(value, std::memory_order_relaxed);
                entries_[slot].key.store(key, std::memory_order_release);
                return true;
            }
        }
        return false;
    }

    void* Find(uintptr_t key) const {
        if (!key) return nullptr;
        size_t slot = Home(key);
        for (size_t i = 0; i < kMaxProbe; ++i, slot = (slot + 1) & (Capacity - 1)) {
            const uintptr_t existing = entries_[slot].key.load(std::memory_order_acquire);
            if (existing == key) return entries_[slot].value.load(std::memory_order_relaxed);
            if (!existing) return nullptr;
        }
        return nullptr;
    }
};

}  // namespace cvr::hooks
