#pragma once
#include <cstdint>
#include <limits>

namespace cvr::detail {
enum class AutoGrassStatus : unsigned { Ready, MissingContext, MissingState, MissingBuffer };
struct AutoGrassReadiness {
    AutoGrassStatus status=AutoGrassStatus::MissingContext;
    uintptr_t state{};
    uint32_t buffer{};
    explicit operator bool() const {return status==AutoGrassStatus::Ready;}
};

// CP2077 2.31 PrepareAutoSpawnOnTerrain (77B638) reads the shared terrain
// state through workContext+20 -> +98. Its AutoGrass branch passes state+7B0
// to774384/1F51C4 without a null-handle check. During save replacement the
// count at+79C can already be nonzero while this buffer is still zero.
template<class ReadPointer,class ReadHandle>
AutoGrassReadiness InspectAutoGrassInputs(uintptr_t workContext,ReadPointer readPointer,ReadHandle readHandle) {
    AutoGrassReadiness out;
    uintptr_t owner{};
    if(!workContext || !readPointer(workContext+0x20,&owner) || !owner)return out;
    out.status=AutoGrassStatus::MissingState;
    if(!readPointer(owner+0x98,&out.state) || !out.state)return out;
    out.status=AutoGrassStatus::MissingBuffer;
    if(!readHandle(out.state+0x7B0,&out.buffer) || !out.buffer ||
       out.buffer==std::numeric_limits<uint32_t>::max())return out;
    out.status=AutoGrassStatus::Ready;
    return out;
}
}
