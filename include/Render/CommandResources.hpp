#pragma once
#include <d3d12.h>
#include <initializer_list>
#include <memory>
#include <vector>

namespace cvr::gpu {
struct ResourceBatch;
struct ResourceSubmission { std::vector<std::shared_ptr<const ResourceBatch>> batches; };
// Keep resources through recording, possible replay, and GPU completion.
bool KeepCommandResources(ID3D12GraphicsCommandList*, std::initializer_list<IUnknown*>);
bool HasCommandResource(ID3D12GraphicsCommandList*,IUnknown*); // current recording only, not a prior submitted list
ResourceSubmission PrepareCommandResources(UINT count, ID3D12CommandList* const*);
void SubmitCommandResources(ID3D12CommandQueue*, ResourceSubmission);
void ResetCommandResources(ID3D12GraphicsCommandList*); // after successful Reset
void CollectCommandResources();
}
