#pragma once
#include "Render/RegisteredConstantUpload.hpp"
#include <array>
#include <span>

namespace cvr::stereo {
// Physical copy inputs only. The caller separately authenticates native
// allocation lifetime, access ordering, residency and the final states.
// Retain this object until its submitted queue fence completes. No CPU waits,
// queue submission, mapping, native registration or export permission here.
class NativeOutputCopy {
public:
    struct Target {
        constants::ResourceCreationInfo creation;
        UINT planeCount{};
        std::array<D3D12_RESOURCE_STATES,2> finalStates{};
    };
    using Arrays=std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,4>;
    using Targets=std::array<std::array<Target,4>,2>;
    struct Plane {
        UINT side{},slot{},plane{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows{},stride{},component{},bytes{};
    };
    NativeOutputCopy()=default;
    NativeOutputCopy(const NativeOutputCopy&)=delete;
    NativeOutputCopy& operator=(const NativeOutputCopy&)=delete;
    // Compact input slots are 0/1/2/4; outer target index is native side.
    // Arrays must already be COPY_SOURCE; the predicate must be readable for
    // predication. Neither state can be queried from the D3D12 objects.
    HRESULT Create(ID3D12Device*,const Arrays&,const Targets&,ID3D12Resource* predicate,UINT64 offset);
    // One recording only, on a caller-owned DIRECT list. Leaves predication
    // disabled. Predicate zero skips only image writes, never barriers/readback.
    bool Record(ID3D12GraphicsCommandList*);
    ID3D12Resource* Readback()const{return readback_.Get();}
    std::span<const Plane> Planes()const{return readback_?std::span<const Plane>(planes_):std::span<const Plane>{};}
    UINT64 Bytes()const{return bytes_;}
    bool Recorded()const{return recorded_;}
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Arrays arrays_;
    Targets targets_;
    Microsoft::WRL::ComPtr<ID3D12Resource> predicate_,readback_;
    std::array<Plane,10> planes_{};
    UINT64 predicateOffset_{},bytes_{};
    bool recorded_{};
};
}
