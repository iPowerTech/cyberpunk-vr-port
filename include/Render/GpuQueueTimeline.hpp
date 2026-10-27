#pragma once
// GpuQueueTimeline -- diagnostic: when each queue of the game actually runs its batches.
// See src/Render/GpuQueueTimeline.cpp.
#include <d3d12.h>

namespace cvr::gpu::queue_timeline {
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
// Runs the submission when a capture is on and returns true; false leaves it to the caller.
bool Execute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists, ExecuteFn original);
// Once per game Present: marks the frame boundary and moves the capture along.
void Present();
}
