#pragma once
// XR GPU TIMELINE (src/Runtimes/XrGpuTimeline.cpp) -- report only, while the frame-path report is on.
//
// GPU timestamps around each frame's capture (Present thread) and each submit cycle's copy to the headset (XR
// thread), both on the game's queue, mapped onto the CPU clock with the queue's clock calibration. Once a second
// [xrgpu] says, for the copies of that second: how long a copy sat in the queue after it was submitted, how long
// after its own capture had finished it started, and how many of the game's frames finished on the GPU between
// its submit and its start.
#include <d3d12.h>
#include <cstdint>

namespace cvr::xrtimeline {

bool Enabled();
// Present thread, around the capture command list of frame `serial`.
void CaptureBegin(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list, uint64_t serial);
void CaptureEnd(ID3D12GraphicsCommandList* list, uint64_t serial);   // stamps and resolves; just before Close
// XR thread, around the submit cycle's copy list; `serial` is the capture it copies.
void CopyBegin(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list);
void CopyEnd(ID3D12GraphicsCommandList* list, uint64_t serial);      // just before Close
void CopySubmitted();                                                // just after ExecuteCommandLists
// XR thread, once a cycle: reads the copies recorded 8 cycles back.
void Poll();
// Present thread, once a second (ReportXrFrameRates): the [xrgpu] line.
void Report();

}  // namespace cvr::xrtimeline
