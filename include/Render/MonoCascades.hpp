#pragma once
#include <d3d12.h>
#include <atomic>
#include <cstdint>

extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_MonoCascades;
__declspec(dllexport) extern uint32_t CyberpunkVR_MonoCascadeReportSeq;
__declspec(dllexport) extern char CyberpunkVR_MonoCascadeReport[4096];
}
namespace cvr::cascades {
void ResourceCreated(ID3D12Resource*,ID3D12Heap*,uint64_t offset,uint64_t bytes);
void DsvCreated(ID3D12Resource*,const D3D12_DEPTH_STENCIL_VIEW_DESC*,D3D12_CPU_DESCRIPTOR_HANDLE);
void CopyDescriptors(ID3D12Device*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,const UINT*,D3D12_DESCRIPTOR_HEAP_TYPE);
void SourceSampling(const float*,unsigned);
void MainSampling(const float*,unsigned);
void Camera(unsigned,const void*,size_t,bool source);
void BeginNode(int side,int index);
bool SkipNode(int side,int index);
void EndNode();
void Targets(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*);
bool SkipDraw(ID3D12GraphicsCommandList*);
void SignatureCreated(ID3D12CommandSignature*,const D3D12_COMMAND_SIGNATURE_DESC&);
bool SkipIndirect(ID3D12GraphicsCommandList*,ID3D12CommandSignature*);
bool Clear(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,D3D12_CLEAR_FLAGS,FLOAT,UINT8,UINT,int side,bool clearNode);
bool Discard(ID3D12Resource*,int side,bool clearNode);
void Write(ID3D12Resource*);
void Barriers(UINT,const D3D12_RESOURCE_BARRIER*);
void BeforeBarriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
void Close(ID3D12GraphicsCommandList*);
void Reset(ID3D12GraphicsCommandList*);
void Poll();
}
