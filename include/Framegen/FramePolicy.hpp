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
// A submission shows a new real frame, repeats the one already displayed, or (FSR3) shows the frame
// interpolated before a new real one. A midpoint is always followed by its real endpoint: never
// replace the pending endpoint with a newer capture, or motion reverses/skips time.
class Cadence {
public:
    enum class Next { Repeat, Real, Midpoint, PendingReal };
    Next Select(uint64_t serial,bool midpointReady) const {
        if(pending_)return Next::PendingReal;
        if(!serial || serial==displayed_)return Next::Repeat;
        return midpointReady ? Next::Midpoint : Next::Real;
    }
    void Accepted(Next kind,uint64_t serial) {
        if(kind==Next::Midpoint)pending_=serial;
        else if(kind==Next::Real || kind==Next::PendingReal) {displayed_=serial;pending_=0;}
    }
    // The pending endpoint is given up for the next capture's midpoint (Framegen/Presenter.cpp).
    void DropPending() {pending_=0;}
    void Reset() {pending_=displayed_=0;}
    uint64_t Pending() const {return pending_;}
private:
    uint64_t pending_{},displayed_{};
};
}
