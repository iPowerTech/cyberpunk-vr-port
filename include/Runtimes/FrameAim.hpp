#pragma once
#include <cstdint>
#include <mutex>

namespace cvr::tracking {
struct FrameAimSnapshot {
    int64_t time{};
    uint64_t stampUs{}, epoch{};
};

// The target time, publication time and identity describe one update. Separate
// atomics can return a new target tagged with the previous update's identity.
class FrameAim {
public:
    void Publish(int64_t time, uint64_t stampUs) {
        std::lock_guard lock(m_mutex);
        m_value = {time, stampUs, m_value.epoch + 1};
    }
    FrameAimSnapshot Read() const {
        std::lock_guard lock(m_mutex);
        return m_value;
    }
private:
    mutable std::mutex m_mutex;
    FrameAimSnapshot m_value{};
};
}
