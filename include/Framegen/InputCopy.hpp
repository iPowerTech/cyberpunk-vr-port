#pragma once
#include "Framegen/Inputs.hpp"

namespace cvr::framegen {
struct InputTag {ID3D12Resource* resource{};uint32_t state{},type{},x{},y{},width{},height{};};
bool CopyInput(ID3D12GraphicsCommandList* list,const InputTag& tag,Inputs& out);
// Widens inputs.motionSource into inputs.motion on `queue`, in a command list of its own, after the copy that filled
// it on that queue. Called with cvr::camera::ImageSubmissionMutex() held.
bool WidenMotion(ID3D12CommandQueue* queue,Inputs& inputs);
}
