#pragma once
#include <cstdint>

namespace cvr::framegen {
struct Identity {
    uint64_t serial{}, origin{};
    uint32_t nativeFrame{};
    uint64_t pose[2]{};
    double capturedMs{};
    bool stereo{};
};
// A submission shows a new real frame, or repeats the one already displayed.
class Cadence {
public:
    enum class Next { Repeat, Real };
    Next Select(uint64_t serial) const {
        return !serial || serial==displayed_ ? Next::Repeat : Next::Real;
    }
    void Accepted(Next kind,uint64_t serial) {if(kind==Next::Real)displayed_=serial;}
    void Reset() {displayed_=0;}
private:
    uint64_t displayed_{};
};
}
