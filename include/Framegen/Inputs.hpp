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
    // NDC of the frustum's centre axis: non-zero under the asymmetric projection, where each eye renders
    // its own off-centre window (the engine's P20/P21, Runtimes/AsymmetricProjection.hpp).
    float shift[2]{};
    bool inverted{},infinite{},jittered{},reset{},cameraMotion{},motion3d{};
};
struct Inputs {
    Microsoft::WRL::ComPtr<ID3D12Resource> depth,motion;
    // Motion smaller than the depth (TAA's velocity is half the render size), as copied. `widen` until it has been
    // widened into `motion`, at the depth's size, before a consumer is given the inputs (AcquireInputs).
    Microsoft::WRL::ComPtr<ID3D12Resource> motionSource;
    Microsoft::WRL::ComPtr<ID3D12Fence> readyFence;
    ID3D12CommandQueue* queue{};
    CameraData camera{};
    uint64_t poseId{},origin{},fenceValue{},bytes{};
    uint32_t frameId{},view{},width{},height{},motionWidth{},motionHeight{};
    bool hasDepth{},hasMotion{},submitted{},evaluated{},native{},widen{};
};
bool ReadRenderFrameIndex(uint32_t* index);
void RecordNativeCamera(void* state,unsigned view);
// The camera of the engine view a native consumer node renders (eye 1 = MAIN, 2 = second eye).
void RecordViewCamera(const void* view,unsigned eye);
// Called by the existing Streamline tag hook while the supplied list owns the resources.
void RecordTags(const void* viewport,const void* tags,uint32_t count,void* commandList);
struct InputTag;
bool RecordNativeInputs(const InputTag* tags,ID3D12GraphicsCommandList*);
bool InstallInputHooks();
void Submitted(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists);
void ResetList(ID3D12GraphicsCommandList* list);
std::shared_ptr<const Inputs> AcquireInputs(unsigned view,uint32_t frameId,
    uint64_t poseId,uint64_t origin,ID3D12CommandQueue* queue);
uint64_t InputVram();
void ReleaseInputs();
}
