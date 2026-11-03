#pragma once
#include <d3d12.h>
namespace cvr::framegen {
// Production callers hold ImageSubmissionMutex before either entry point.
// Timer markers submit recursively, so reversing this lock order deadlocks
// against the Present capture's already-held image-submission lock.
void BeforeGameCommands(ID3D12CommandQueue*);
void FinishGpuFrame();
// The GPU spans of the frames timed since the last call (first game list after a Present to the Present, ms): their
// sum, the largest and how many. For the [present] line (Hooks/SwapChain.cpp); takes only the timer's own lock.
bool TakeGpuSpans(double* sum, double* max, unsigned* count);
}
