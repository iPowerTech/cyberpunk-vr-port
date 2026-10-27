#pragma once
#include "Render/NativeStereoProbe.hpp"
#include "Render/SinglePassTrace.hpp"

namespace cvr::stereo::paired_geometry {
bool Active();
bool NeedsCamera();
void FrameBoundary();
void Camera(const trace::Record&);
void BoundCamera(int side,uint32_t binding,uint32_t handle,uint32_t stages);
// Reference only: handled means the original native draw already ran once.
bool Draw(ID3D12GraphicsCommandList*,const native_probe::DrawRecord&);
bool BeforeSubmit(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
void Submitted(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PairedGeometryRequest,CyberpunkVR_PairedGeometryState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PairedGeometryOrdinal;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PairedGeometryFamily; // 0: exact quad pair, 70: audited radial blend pair
__declspec(dllexport) extern uint64_t CyberpunkVR_PairedGeometryStats[16];
__declspec(dllexport) extern uint64_t CyberpunkVR_PairedGeometryDifferent[12],CyberpunkVR_PairedGeometryBytes[2],CyberpunkVR_PairedGeometryCameraDiagnostics[16];
__declspec(dllexport) extern uint64_t CyberpunkVR_PairedGeometryChanged[12];
}
