#include "Render/GpuStageProfile.hpp"
#include "Render/CommandResources.hpp"
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace cvr::gpu;
extern "C" std::atomic<uint32_t> CyberpunkVR_GpuStageRequest;
extern "C" char CyberpunkVR_GpuStageReport[2048];
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void Hr(HRESULT value) { Check(SUCCEEDED(value), "D3D12 call failed"); }
int main() try {
    Check(!profile::Begin(reinterpret_cast<ID3D12GraphicsCommandList*>(1)), "disabled path touched D3D");
    ComPtr<IDXGIFactory4> factory; Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter; Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device; Hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC desc{}; desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue; Hr(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)));
    std::vector<ComPtr<ID3D12CommandAllocator>> allocators;
    auto allocator=[&] { ComPtr<ID3D12CommandAllocator> a;Hr(device->CreateCommandAllocator(desc.Type,IID_PPV_ARGS(&a)));allocators.push_back(a);return a.Get(); };
    ComPtr<ID3D12GraphicsCommandList> list; Hr(device->CreateCommandList(0,desc.Type,allocator(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> completion,gate;
    Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&completion)));
    Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    CyberpunkVR_GpuStageRequest=2;profile::Poll();
    auto batch=profile::Begin(list.Get());Check(bool(batch),"request did not arm");
    {profile::Scope first(batch,profile::Stage::CaptureMain);}
    {profile::Scope second(batch,profile::Stage::CaptureSecond);}
    Hr(list->Close());ID3D12CommandList* commands[]={list.Get()};
    Hr(queue->Wait(gate.Get(),1));
    auto resources=PrepareCommandResources(1,commands);
    queue->ExecuteCommandLists(1,commands);SubmitCommandResources(queue.Get(),std::move(resources));
    Hr(queue->Signal(completion.Get(),1));profile::Submitted(batch,queue.Get(),completion.Get(),1);batch.reset();
    auto start=std::chrono::steady_clock::now();profile::Poll();
    Check(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(50),"poll waited for GPU");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"batches\":0"),"mapped incomplete readback");

    // Reset with a different allocator while the first submission is held on
    // the GPU. Cancel the new recording; it must not erase the pending sample.
    Hr(list->Reset(allocator(),nullptr));ResetCommandResources(list.Get());
    batch=profile::Begin(list.Get());Check(bool(batch),"second sample missing");
    {profile::Scope discarded(batch,profile::Stage::SubmitColor);}
    batch.reset();Hr(list->Close());list.Reset();
    Check(!profile::Begin(reinterpret_cast<ID3D12GraphicsCommandList*>(1)),"bounded budget exceeded");
    profile::Poll();Hr(gate->Signal(1));
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event creation");
    Hr(completion->SetEventOnCompletion(1,event));
    const auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Check(waited==WAIT_OBJECT_0,"GPU timeout");
    CollectCommandResources();profile::Poll();
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"pending\":false"),"profile did not finish");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"batches\":1"),"submitted sample missing");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"cancelled\":1"),"cancelled recording not retired");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"invalid\":0"),"invalid timestamp result");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"name\":\"CaptureMain\",\"count\":1"),"main timing missing");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"name\":\"CaptureSecond\",\"count\":1"),"second timing missing");
    Check(std::strstr(CyberpunkVR_GpuStageReport,"\"name\":\"SubmitColor\",\"count\":0"),"aborted query was counted");
    std::cout<<"PASS disabled path, bounded capture, nonblocking poll, GPU-delayed reset, cancellation and destruction\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
