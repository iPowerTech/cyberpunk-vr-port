#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace cvr::sharpen {
inline constexpr float DefaultAmount = 0.0f;

class UpscalerGate {
    static constexpr unsigned Selected=1, Known=2, Dlaa=4;
    std::atomic<unsigned> state{};
public:
    void SelectDlss(bool selected) {
        if(selected)state.fetch_or(Selected,std::memory_order_relaxed);
        else state.store(0,std::memory_order_relaxed);
    }
    // Called only for the original game's NGX evaluation, never private
    // foveation passes (whose periphery intentionally uses DLAA).
    void ObserveGameQuality(unsigned view,unsigned quality) {
        if(view!=1)return;
        state.store(Selected|(quality<=5?Known:0)|(quality==5?Dlaa:0),std::memory_order_relaxed);
    }
    bool BlocksCas() const {
        const auto value=state.load(std::memory_order_relaxed);
        return (value&Selected) && (!(value&Known) || (value&Dlaa));
    }
    float Amount(float requested) const {
        return BlocksCas() || !std::isfinite(requested) ? 0.0f : std::clamp(requested,0.0f,1.0f);
    }
};
inline UpscalerGate gameUpscaler;
}
