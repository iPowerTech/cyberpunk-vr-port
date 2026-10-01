#pragma once
#include <d3d12.h>
namespace cvr::framegen {
// Production callers hold ImageSubmissionMutex before either entry point.
// Timer markers submit recursively, so reversing this lock order deadlocks
// against the Present capture's already-held image-submission lock.
void BeforeGameCommands(ID3D12CommandQueue*);
void FinishGpuFrame();
}
