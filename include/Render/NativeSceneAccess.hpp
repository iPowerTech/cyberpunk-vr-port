#pragma once
#include "Render/NativeSceneChild.hpp"
#include <d3d12.h>

namespace cvr::stereo::scene_access_trace {
enum class Kind:uint32_t {Read=1,Write,Transition,Alias,UavBarrier,Shader,Unknown,Targets};
inline constexpr uint32_t NoResource=UINT32_MAX;
inline constexpr uint32_t RecordingLimit=256,ResourceLimit=4096,EventLimit=65536,ChildLimit=4096;
struct Event {
    uint64_t sequence{},offset{},bytes=UINT64_MAX,auxiliary{};
    uint32_t recording{},child=UINT32_MAX,kind{},resource=NoResource,other=NoResource;
    uint32_t subresource=UINT32_MAX,before{},after{},flags{},tag{},internal{},reserved{};
};
static_assert(sizeof(Event)==80);
void Arm()noexcept;
void Finish(bool aborted=false)noexcept;
bool Active()noexcept;
void Reset(ID3D12GraphicsCommandList*)noexcept;
void Close(ID3D12GraphicsCommandList*,HRESULT)noexcept;
void Operand(ID3D12GraphicsCommandList*,ID3D12Resource*,bool write,uint32_t tag,UINT subresource,
             uint64_t offset=0,uint64_t bytes=UINT64_MAX)noexcept;
void ImmediateWrite(ID3D12GraphicsCommandList2*,UINT,const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER*,
                    const D3D12_WRITEBUFFERIMMEDIATE_MODE*,uintptr_t caller)noexcept;
void Barriers(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*)noexcept;
void Targets(ID3D12GraphicsCommandList*,UINT,const D3D12_CPU_DESCRIPTOR_HANDLE*,BOOL,const D3D12_CPU_DESCRIPTOR_HANDLE*)noexcept;
void ClearView(ID3D12GraphicsCommandList*,D3D12_CPU_DESCRIPTOR_HANDLE,bool depth)noexcept;
void Shader(ID3D12GraphicsCommandList*,bool graphics)noexcept;
void Unknown(ID3D12GraphicsCommandList*,uint32_t tag)noexcept;
void ObserverUnavailable()noexcept;
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SceneAccessEnable;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_SceneAccessSeq;
// schema,armed,finished,epoch,recordings,resources,events,children,selected,
// unknownShaderOperands,unattributedEvents,missingMetadata,resets,overflow,failure,permission(0).
__declspec(dllexport) extern uint64_t CyberpunkVR_SceneAccessStats[16];
__declspec(dllexport) extern uint64_t CyberpunkVR_SceneAccessRecordings,CyberpunkVR_SceneAccessResources;
__declspec(dllexport) extern uint64_t CyberpunkVR_SceneAccessEvents,CyberpunkVR_SceneAccessChildren;
}
