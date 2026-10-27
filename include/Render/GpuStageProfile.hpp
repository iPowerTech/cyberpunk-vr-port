#pragma once
#include <d3d12.h>
#include <memory>

namespace cvr::gpu::profile {
enum class Stage : unsigned { CaptureMain, CaptureSecond, CaptureDepth, SubmitColor, SubmitDepth,
    SecondHud, SecondBlit, HudSprites, HudBlur, HudStyle, HudSubmit, Count };
struct Batch;
using BatchPtr = std::shared_ptr<Batch>;
// Internal, bounded diagnostic request, independent of the broad runtime gate.
// No D3D calls or allocations while unarmed. Lists remain owned by the caller.
BatchPtr Begin(ID3D12GraphicsCommandList*);
class Scope {
public:
    Scope(const BatchPtr&, Stage);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Batch* batch{};
    unsigned index = ~0u;
};
// Call only after successful submission and Signal on the supplied queue/fence.
void Submitted(BatchPtr, ID3D12CommandQueue*, ID3D12Fence*, UINT64 value);
// Nonblocking: only maps completed readbacks. Called from Present.
void Poll();
}
