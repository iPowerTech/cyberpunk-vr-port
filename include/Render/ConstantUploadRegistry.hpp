#pragma once
#include <d3d12.h>
#include <atomic>

namespace cvr::stereo::constants {
// Explicit descriptor-observation launch only. Own CPU-accessible buffers from
// the native CBV uploader and their mappings. Generic streaming allocations are
// not observed: they do not resolve these CBVs and would churn the bounded cache.
void UploadCreated(ID3D12Resource*);
// Resource and base address must come from the same live GetGPUVirtualAddress call.
void UploadAddressObserved(ID3D12Resource*,uint64_t);
using CreationAddressObserver=void(*)(ID3D12Resource*,uint64_t,uintptr_t caller);
void SetCreationAddressObserver(CreationAddressObserver);
void ObserveResourceApi(ID3D12Resource*);
void TouchUpload(const D3D12_CONSTANT_BUFFER_VIEW_DESC*);
ID3D12Resource* RegisteredSource(uint32_t handle,uint32_t bytes); // opaque identity ONLY, match against a fresh API argument before use
bool ReadUpload(uint64_t address,void*,size_t);
// Only UPLOAD/GPU_UPLOAD heaps: CPU writes can change these bytes, GPU writes cannot.
bool ReadFixedUpload(uint64_t address,void* output,size_t bytes);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadCount,CyberpunkVR_ConstantUploadFailures;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_ConstantUploadBytes,CyberpunkVR_ConstantUploadReads,CyberpunkVR_ConstantUploadMisses;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadEvictions;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadTypes[6];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadNativeImports,CyberpunkVR_ConstantUploadNativeRejects;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantRegisteredTypes[6],CyberpunkVR_ConstantRegisteredRejects;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadApiHits;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ConstantUploadFastPath;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_ConstantUploadFastStats[6];
__declspec(dllexport) extern uint32_t CyberpunkVR_ConstantUploadFastStatsCount;
}
