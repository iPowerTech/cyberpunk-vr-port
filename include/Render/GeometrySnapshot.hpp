#pragma once
#include "Render/NativeGeometryLeases.hpp"
#include "Render/StereoGeometryCommands.hpp"

namespace cvr::stereo {
// Diagnostic acquisition at the original draw position. Every used range has
// its own copy; no mutable source is deduplicated across recording positions.
class GeometrySnapshot {
public:
    GeometrySnapshot();
    ~GeometrySnapshot();
    HRESULT Capture(ID3D12GraphicsCommandList*,const geometry::DrawResources&,
                    const std::array<geometry::ReadState,17>&,bool instanceRebaseSafe);
    // Caller supplies the fence of this exact source submission. The source
    // recording must have been reset/destroyed, so it cannot rewrite the copy.
    bool Seal(ID3D12Fence*,UINT64 value);
    bool OnRecording() const;
    HRESULT Read(); // S_FALSE until the sealed submission completes; never waits.
    UINT64 Bytes() const;
    bool CopyDraw(geometry::DrawResources&) const;
    static bool Pair(const GeometrySnapshot& vrcam,const GeometrySnapshot& main,GeometryCommandPair&);
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
