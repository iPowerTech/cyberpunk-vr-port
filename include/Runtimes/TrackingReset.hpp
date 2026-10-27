#pragma once
#include <atomic>
#include <cstdint>

namespace cvr::roomscale {
class TrackingResetGate {
public:
    void Begin() { m_serial.fetch_add(1, std::memory_order_acq_rel); }
    void End() { m_serial.fetch_add(1, std::memory_order_release); }
    uint64_t Serial() const { return m_serial.load(std::memory_order_acquire); }
    bool Pending() const { return Serial() != m_applied.load(std::memory_order_acquire); }
    bool Ready(uint64_t locatedAt) const { return !(locatedAt & 1u) && locatedAt == Serial(); }
    void Applied(uint64_t locatedAt) { m_applied.store(locatedAt, std::memory_order_release); }
private:
    std::atomic<uint64_t> m_serial{}, m_applied{};
};
}
