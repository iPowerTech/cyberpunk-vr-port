#pragma once
#include "Render/NativeStereoFamilies.hpp"
#include "Render/NativeStereoProbe.hpp"
#include "Render/StereoBindingBank.hpp"
#include "Render/ConstantWriteTimeline.hpp"
#include "Render/StereoDepthComparison.hpp"

namespace cvr::stereo::constants {
constexpr UINT BankPairCapacity=2048;
using ReadBytes=bool(*)(ID3D12CommandList*,uint64_t address,void* output,size_t bytes,uint64_t* receipt);
using ValidateBytes=bool(*)(uint64_t receipt,ID3D12CommandList*,uint64_t address,const void* expected,size_t bytes);
using MarkPosition=uint64_t(*)(ID3D12CommandList*);
using ResolvePosition=ConstantWriteTimeline::Result(*)(uint64_t,ID3D12CommandList*,uint64_t,void*,size_t);
struct Binding {
    uint64_t descriptor{},heap{},address{},hash{};
    uint32_t draw{},parameter{},offset{},visibility{},shaderRegister{},bytes{},expectedBytes{},payload{},status{},reserved{}; // reserved=1: logical shader bytes, not CBV padding
};
struct Draw {uint64_t family{};uint32_t sequence{},first{},count{},flags{},side{},frame{};};
struct Payload {uint32_t offset{},bytes{};uint64_t hash{};};
static_assert(sizeof(Binding)==72 && sizeof(Draw)==32 && sizeof(Payload)==16);
// Status: 1=copied, 2=descriptor unknown, 4=small, 8=unreadable,
// 16=descriptor changed, 32=source bytes/order unverified, 64=abandoned.
// Draw flags: 1=all declared CBVs owned, 2=submitted, 4=source validated at submit.
// These flags do not prove final GPU contents or authorize native draw removal.
void SetReader(ReadBytes,ValidateBytes=nullptr,MarkPosition=nullptr,ResolvePosition=nullptr);
void Begin();
void Snapshot(const native_probe::DrawRecord&,std::shared_ptr<const families::Family>);
void End();
void BeforeSubmit(UINT,ID3D12CommandList* const*);
void Reset(ID3D12GraphicsCommandList*);
void Poll();
struct PairInput {
    PairInput()=default;
    PairInput(const PairInput&)=delete;
    PairInput& operator=(const PairInput&)=delete;
    PairInput(PairInput&&)=default;
    PairInput& operator=(PairInput&&)=default;
    std::shared_ptr<const families::Family> family;
    std::vector<std::array<std::vector<uint8_t>,2>> bytes;
    std::vector<BankPair> pairs;
};
// Input packaging only. This does not authorize merging geometry/visibility or
// scheduling draws. Families with SRV/UAV/sampler dependencies are not yet ready.
bool CopyConstantPair(UINT vrcamSequence,UINT mainSequence,PairInput&);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantCaptureEnable,CyberpunkVR_ConstantCaptureSeq,CyberpunkVR_ConstantCaptureFailure;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantDrawCount,CyberpunkVR_ConstantBindingCount,CyberpunkVR_ConstantPayloadCount,CyberpunkVR_ConstantBlobBytes,CyberpunkVR_ConstantPendingLists;
__declspec(dllexport) extern uint64_t CyberpunkVR_ConstantDraws,CyberpunkVR_ConstantBindings,CyberpunkVR_ConstantPayloads,CyberpunkVR_ConstantBlob;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantBankRequest,CyberpunkVR_ConstantBankDone;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantBankSequences[2*cvr::stereo::constants::BankPairCapacity],CyberpunkVR_ConstantBankDescriptors,CyberpunkVR_ConstantBankDraws;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantBankPairCount;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantBankPrepareGeometry;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantBankPreparedDraws;
__declspec(dllexport) extern uint64_t CyberpunkVR_ConstantBankInstanceBytes;
__declspec(dllexport) extern int32_t CyberpunkVR_ConstantBankGeometryResult;
__declspec(dllexport) extern int32_t CyberpunkVR_ConstantBankPassResult;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantBankPassDraws;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantBankPassStage;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantBankPassValidation[3];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantBankCompareDepth;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantCompareState,CyberpunkVR_ConstantCompareToken,CyberpunkVR_ConstantCompareSeq;
__declspec(dllexport) extern int32_t CyberpunkVR_ConstantCompareResult;
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantCompareStage;
__declspec(dllexport) extern cvr::stereo::DepthComparisonResult CyberpunkVR_ConstantCompareData;
__declspec(dllexport) extern uint64_t CyberpunkVR_ConstantBankBytes;
__declspec(dllexport) extern int32_t CyberpunkVR_ConstantBankResult;
}
