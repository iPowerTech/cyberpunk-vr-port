#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <memory>

namespace cvr::framegen {
struct CameraData {
    float jitter[2]{},motionScale[2]{};
    float position[3]{},up[3]{},right[3]{},forward[3]{};
    float nearPlane{},farPlane{},verticalFov{},aspect{};
    bool inverted{},infinite{},jittered{},reset{},cameraMotion{},motion3d{};
};
struct Inputs {
    Microsoft::WRL::ComPtr<ID3D12Resource> depth,motion;
    Microsoft::WRL::ComPtr<ID3D12Fence> readyFence;
    ID3D12CommandQueue* queue{};
    CameraData camera{};
    uint64_t poseId{},origin{},fenceValue{},bytes{};
    uint32_t frameId{},view{},width{},height{},motionWidth{},motionHeight{};
    bool hasDepth{},hasMotion{},submitted{},evaluated{};
};
bool ReadRenderFrameIndex(uint32_t* index);
void RecordNativeCamera(void* state,unsigned view);
// Called by the existing Streamline tag hook while the supplied list owns the resources.
void RecordTags(const void* viewport,const void* tags,uint32_t count,void* commandList);
bool InstallInputHooks();
void Submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists);
void ResetList(ID3D12GraphicsCommandList* list);
std::shared_ptr<const Inputs> AcquireInputs(unsigned view,uint32_t frameId,
    uint64_t poseId,uint64_t origin,ID3D12CommandQueue* queue);
uint64_t InputVram();
void ReleaseInputs();
}
