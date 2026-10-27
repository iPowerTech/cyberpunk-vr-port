#pragma once
#include <cmath>
#include <cstdint>

namespace cvr::stereo {
struct JitterSample { float pixelX{},pixelY{},clipX{},clipY{};uint32_t phase{}; };
// REDengine's R2 branch, verified against the current EXE. This predicts the
// sample without advancing its native counter. Other AA branches must select
// their own implementation; they must not silently use this one.
inline JitterSample PeekR2Jitter(uint32_t counter,uint32_t width,uint32_t height) {
    if(!width || !height)return {};
    auto component=[counter](float multiplier) {
        // Native code rounds after MULSS and ADDSS; contraction into an FMA
        // changes the sample at large counters.
        volatile float product=static_cast<float>(counter)*multiplier;
        volatile float shifted=product+0.5f;
        float integer{};
        return std::modf(static_cast<float>(shifted),&integer)-0.5f;
    };
    JitterSample sample;
    sample.pixelX=component(0.7548776865005493f);
    sample.pixelY=component(0.5698403120040894f);
    sample.clipX=(sample.pixelX*2.0f)/static_cast<float>(width);
    sample.clipY=(sample.pixelY*2.0f)/static_cast<float>(height);
    sample.phase=counter&15u;
    return sample;
}
}
