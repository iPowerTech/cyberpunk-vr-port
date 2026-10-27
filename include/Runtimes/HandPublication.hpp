#pragma once
#include <cstdint>

namespace cvr::tracking {
// Access under the hand-publication mutex. The native tick owns on-foot hands;
// Present resumes publication if native movement stops (menus, seats, loading).
class HandPublicationGate {
public:
    static constexpr uint64_t MaxAgeUs=250000;
    bool NativeOwns(uint64_t now, uint64_t origin, bool eligible = true) const {
        return eligible && m_stamp && m_origin==origin && now>=m_stamp && now-m_stamp<=MaxAgeUs;
    }
    void PublishedNative(uint64_t now, uint64_t origin) { m_stamp=now; m_origin=origin; }
private:
    uint64_t m_stamp{},m_origin{};
};
}
