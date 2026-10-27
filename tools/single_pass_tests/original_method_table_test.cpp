#include "Hooks/OriginalMethodTable.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using cvr::hooks::OriginalMethodTable;

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static uintptr_t Key(uintptr_t vtable, size_t slot) {  // same mix as MakeVtableSlotKey
    return vtable ^ (static_cast<uintptr_t>(slot) * 0x9E3779B97F4A7C15ull);
}
static void* Value(uintptr_t n) { return reinterpret_cast<void*>(0x10000 + n * 16); }

static void Functional() {
    static OriginalMethodTable<64> table;
    CHECK(table.Find(Key(0x1000, 1)) == nullptr);
    CHECK(!table.Publish(0, Value(1)));
    CHECK(!table.Publish(Key(0x1000, 1), nullptr));
    CHECK(table.Find(0) == nullptr);
    CHECK(table.Publish(Key(0x1000, 1), Value(1)));
    CHECK(table.Find(Key(0x1000, 1)) == Value(1));
    // First original wins, like the map: a second publish of the same key keeps the first value.
    CHECK(table.Publish(Key(0x1000, 1), Value(2)));
    CHECK(table.Find(Key(0x1000, 1)) == Value(1));
    // Fill to capacity; every published key stays retrievable, then publishing more fails closed.
    size_t placed = 1;
    for (uintptr_t i = 2; placed < 64; ++i)
        if (table.Publish(Key(0x1000 + i * 0x40, i % 80), Value(i))) ++placed;
    CHECK(!table.Publish(Key(0xDEAD0000, 3), Value(999)));
    CHECK(table.Find(Key(0xDEAD0000, 3)) == nullptr);
    CHECK(table.Find(Key(0x1000, 1)) == Value(1));
}

// Writer publishes while readers probe: a reader that finds a key must see exactly its value.
static void Concurrent() {
    static OriginalMethodTable<4096> table;
    constexpr uintptr_t kKeys = 2000;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> wrong{0}, seen{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 8; ++t)
        readers.emplace_back([&, t] {
            uint64_t local = 0;
            for (int pass = 0; !done.load(std::memory_order_acquire) || pass < 3; ++pass)
                for (uintptr_t i = 1 + t; i <= kKeys; i += 8) {
                    void* found = table.Find(Key(0x7FF600000000 + i * 0x100, i % 100));
                    if (!found) continue;
                    ++local;
                    if (found != Value(i)) wrong.fetch_add(1);
                }
            seen.fetch_add(local);
        });
    std::mutex writer;  // the caller-side serialization PatchVtableMethod provides
    for (uintptr_t i = 1; i <= kKeys; ++i) {
        std::lock_guard lock(writer);
        CHECK(table.Publish(Key(0x7FF600000000 + i * 0x100, i % 100), Value(i)));
        if (i % 50 == 0) std::this_thread::yield();  // let readers overlap the publication
    }
    done.store(true, std::memory_order_release);
    for (auto& r : readers) r.join();
    CHECK(wrong.load() == 0);
    CHECK(seen.load() > 0);
    for (uintptr_t i = 1; i <= kKeys; ++i) CHECK(table.Find(Key(0x7FF600000000 + i * 0x100, i % 100)) == Value(i));
}

// Contended lookups: the old mutex+map against the published table, same keys, 8 threads.
static void Benchmark() {
    static OriginalMethodTable<4096> table;
    std::unordered_map<uintptr_t, void*> map;
    std::mutex mutex;
    std::vector<uintptr_t> keys;
    for (uintptr_t i = 1; i <= 300; ++i) {
        keys.push_back(Key(0x7FF700000000 + (i / 50) * 0x1000, i % 50));
        map[keys.back()] = Value(i);
        table.Publish(keys.back(), Value(i));
    }
    auto run = [&](bool locked) {
        std::atomic<uint64_t> sum{0};
        const auto begin = std::chrono::steady_clock::now();
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&, t] {
                uintptr_t local = 0;
                for (int n = 0; n < 400000; ++n) {
                    const uintptr_t key = keys[(n * 7 + t) % keys.size()];
                    if (locked) {
                        std::lock_guard lock(mutex);
                        local += reinterpret_cast<uintptr_t>(map.find(key)->second);
                    } else {
                        local += reinterpret_cast<uintptr_t>(table.Find(key));
                    }
                }
                sum.fetch_add(local);
            });
        for (auto& thread : threads) thread.join();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        return std::make_pair(ms, sum.load());
    };
    const auto [lockedMs, lockedSum] = run(true);
    const auto [freeMs, freeSum] = run(false);
    CHECK(lockedSum == freeSum);
    std::printf("contended 8x400000 lookups: mutex+map %.1f ms, published table %.1f ms (%.1fx)\n",
                lockedMs, freeMs, lockedMs / (freeMs > 0 ? freeMs : 1));
}

int main() {
    Functional();
    Concurrent();
    Benchmark();
    if (failures) return EXIT_FAILURE;
    std::printf("original_method_table: all checks passed\n");
    return EXIT_SUCCESS;
}
