#pragma once
#include <d3d12.h>
#include <cstdint>

namespace cvr::stereo::indirect_policy {
namespace detail {
inline bool ValidStride(UINT stride,UINT kind=1){
    static_assert(sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)==20);
    static_assert(sizeof(D3D12_DRAW_ARGUMENTS)==16);
    const UINT minimum=kind==1?sizeof(D3D12_DRAW_INDEXED_ARGUMENTS):kind==2?sizeof(D3D12_DRAW_ARGUMENTS):0;
    return minimum && stride>=minimum && stride<=1024 && !(stride%sizeof(UINT));
}
inline uint32_t Classify(const D3D12_COMMAND_SIGNATURE_DESC& desc){
    if(desc.NumArgumentDescs!=1 || !desc.pArgumentDescs || desc.NodeMask>1)return 0;
    const auto type=desc.pArgumentDescs[0].Type;
    const UINT kind=type==D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED?1u:type==D3D12_INDIRECT_ARGUMENT_TYPE_DRAW?2u:0u;
    return ValidStride(desc.ByteStride,kind)?kind:0;
}
struct Receipt {uint32_t version{},kind{},stride{},nodeMask{};};
static_assert(sizeof(Receipt)==16);
inline UINT DrawKind(const Receipt& receipt){
    return receipt.version==1 && receipt.nodeMask<=1 && ValidStride(receipt.stride,receipt.kind)?receipt.kind:0;
}
inline UINT PureDrawStride(const Receipt& receipt){return DrawKind(receipt)?receipt.stride:0;}
inline UINT DrawIndexedStride(const Receipt& receipt){
    return receipt.kind==1?PureDrawStride(receipt):0;
}
}
// Call only after successful native command-signature creation. The signature's
// private data owns this immutable classification; no raw-pointer registry exists.
void Created(ID3D12CommandSignature*,const D3D12_COMMAND_SIGNATURE_DESC&);
bool DrawIndexedOnly(ID3D12CommandSignature*);
// Zero means unknown or rejected. Runtime argument-buffer bounds must use this
// actual stride, which may include padding beyond the 20-byte indexed arguments.
UINT DrawIndexedStride(ID3D12CommandSignature*);
// Both pure draw kinds preserve root/IA state. Kind 1 is DRAW_INDEXED; kind 2 is
// DRAW. All other signatures, unobserved signatures, and malformed receipts are 0.
UINT PureDrawStride(ID3D12CommandSignature*);
UINT DrawKind(ID3D12CommandSignature*);
// Diagnostic metadata, including the original stride for rejected signatures.
// This raw receipt is not authorization; use the validated accessors above.
detail::Receipt Describe(ID3D12CommandSignature*);
}
