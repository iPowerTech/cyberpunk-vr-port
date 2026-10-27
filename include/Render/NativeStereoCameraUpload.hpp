#pragma once
#include <d3d12.h>
#include <atomic>
#include <cstdint>
#include <wrl/client.h>
#include "Render/StereoShaderCamera.hpp"
#include "Render/NativeCameraBindingState.hpp"

namespace cvr::stereo::camera_upload {
struct Result {uint64_t cpuDescriptor{},gpuAddress{};uint32_t bytes{},reserved{};};
Context CurrentContext(uintptr_t gameBase);
// One-shot comparison path. Uses the engine's current upload allocator and an
// owned CPU-only descriptor. Ordinary shaders still read the unchanged prefix.
Result Prepare(uintptr_t gameBase,uint32_t nativeHandle,const ShaderCamera& first,const ShaderCamera& second);
void CreatedCbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
Result CurrentBinding();
void AfterNativeBind(uintptr_t gameBase,uint32_t binding,uint32_t nativeHandle,uint32_t stages,bool sceneVrcam);
// Scoped depth path. The game's upload allocator owns the GPU bytes; the caller
// retains the CPU descriptor until the native recording has consumed it.
bool BeginNative(uintptr_t gameBase,const ShaderCamera& main,const ShaderCamera& peer,
                 Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>&,Result&);
void EndNative(uintptr_t gameBase,uint32_t originalHandle);
void ObserveNativeBind(uintptr_t gameBase,uint32_t binding,uint32_t nativeHandle,uint32_t stages);
// nativeCameraHandle must be the completed native upload/bind identified by
// the caller's current MAIN camera record, not an arbitrary historical handle.
bool BeginNativeStages(uintptr_t gameBase,uint32_t nativeCameraHandle,const ShaderCamera& main,const ShaderCamera& peer,
                       Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>&,Result&,uint32_t stages,BindingScope&);
bool EndNativeStages(uintptr_t gameBase,BindingScope&);
// Delayed paired reference only: both native draws have already executed.
// Checks the current camera independently from the [MAIN,PEER] bank prefix,
// materializes a fresh GPU table and verifies its actual bound 2048-byte CBV.
bool BeginNativePairStages(uintptr_t gameBase,uint32_t currentHandle,const ShaderCamera& expectedCurrent,
                          const ShaderCamera& main,const ShaderCamera& peer,
                          Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>&,Result&,uint32_t stages,BindingScope&);
bool EndNativePairStages(uintptr_t gameBase,BindingScope&);
std::array<uint64_t,16> BindingDiagnostics();
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SinglePassTwinUpload;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SinglePassTwinAllocations,CyberpunkVR_SinglePassTwinBindings,CyberpunkVR_SinglePassTwinFailures;
}
