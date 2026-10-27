#pragma once
#include <cstdint>
#include <mutex>

namespace cvr::stereo {
inline constexpr uint64_t ShadingRateImageFlagF1=1ull<<20; // graph feature 84
constexpr uint64_t SupportedShadingRateFlags(uint64_t flags,uint32_t nativeTileSize) {
    return nativeTileSize ? flags : flags&~ShadingRateImageFlagF1;
}

struct FrameGraphFlags {
    uint64_t f0{},f1{};
    bool operator==(const FrameGraphFlags&) const = default;
};

class MainViewFeatureCache {
public:
    bool Observe(uintptr_t context,uintptr_t knownMain,uint64_t viewName,FrameGraphFlags flags) {
        if(!context || context!=knownMain || viewName || !(flags.f0|flags.f1))return false;
        std::lock_guard lock(mutex_);
        context_=context;flags_=flags;return true;
    }
    bool Read(uintptr_t knownMain,FrameGraphFlags& out) const {
        std::lock_guard lock(mutex_);
        if(!knownMain || context_!=knownMain)return false;
        out=flags_;return true;
    }
private:
    mutable std::mutex mutex_;
    uintptr_t context_{};
    FrameGraphFlags flags_{};
};
}
