#pragma once
#include <d3d12.h>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace cvr::stereo {
// Matches patch_resource_pairs.py: space-0 register N -> 2*N + SV_ViewID.
struct PairedBinding {
    D3D12_DESCRIPTOR_RANGE_TYPE type{};
    D3D12_SHADER_VISIBILITY visibility{};
    UINT sourceRegister{},privateSpace{};
    bool operator==(const PairedBinding&) const = default;
};
struct PairedBindingLocation {
    PairedBinding binding{};
    UINT sourceParameter{},sourceOffset{},destinationParameter{},destinationOffset{};
    D3D12_ROOT_PARAMETER_TYPE sourceType{};
    bool operator==(const PairedBindingLocation&) const = default;
};
struct PairedRootPlan {
    std::vector<uint8_t> serialized;
    std::vector<PairedBindingLocation> bindings;
    std::array<UINT,2> parameters{UINT_MAX,UINT_MAX},descriptors{}; // VS, PS
    UINT originalParameters{},rootDwords{};
    bool operator==(const PairedRootPlan&) const = default;
};
// Preserves every original parameter index, range, flag and static sampler.
// Adds at most one mixed CBV/SRV table per stage. No resource is retained or
// copied here; source locations describe where a future owned bank gets data.
// Failure leaves output unchanged. Serialized input may be root version 1.0/1.1.
HRESULT BuildPairedRoot(const void*,size_t,std::span<const PairedBinding>,PairedRootPlan&);
}
