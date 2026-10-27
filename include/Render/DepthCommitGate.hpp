#pragma once
#include "Render/NativeStereoProbe.hpp"
#include <wrl/client.h>
#include <span>

namespace cvr::stereo {
// Caller serializes recording/admission and only reads after its queue fence.
// The GPU also rejects admission when native initial depth/stencil differs.
class DepthCommitGate {
public:
    static constexpr UINT MaxBufferProofs=256;
    // Owned, immutable snapshots in NON_PIXEL_SHADER_RESOURCE state. The gate
    // compares every byte in each declared row before predicated work executes.
    // Zero row fields mean one contiguous DWORD-aligned range. Texture snapshots
    // may specify row layout to exclude uninitialized copy-footprint padding.
    struct BufferProof {
        ID3D12Resource* main{};ID3D12Resource* peer{};UINT bytes{};
        UINT rowBytes{},rowPitch{},rows{};
        UINT mainOffset{},peerOffset{};
    };
    DepthCommitGate();
    ~DepthCommitGate();
    HRESULT Create(ID3D12Device*,ID3D12Resource* initialDepthArray);
    bool Prepare(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,const BufferProof* instances=nullptr);
    bool Prepare(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&,std::span<const BufferProof>);
    bool Decide(bool allow);
    void Freeze();
    bool Bind(ID3D12GraphicsCommandList*,bool optimized)const;
    bool Readback(ID3D12GraphicsCommandList*);
    HRESULT Read(uint64_t& value)const;
    // The owning pass must prove that every previous GPU user has completed.
    bool Restart();
private:
    struct Storage;
    Microsoft::WRL::ComPtr<Storage> storage_;
};
}
