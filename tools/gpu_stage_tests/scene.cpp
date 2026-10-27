#include "Render/SceneGpuProfile.hpp"
#include "Render/CommandResources.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <chrono>

using Microsoft::WRL::ComPtr;
using namespace cvr::gpu;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void Hr(HRESULT value){Check(SUCCEEDED(value),"D3D12 failure");}
int main() try {
    scene_profile::Mark(reinterpret_cast<ID3D12GraphicsCommandList*>(1),1,0,nullptr);
    scene_profile::Close(reinterpret_cast<ID3D12GraphicsCommandList*>(1));
    scene_profile::Reset(reinterpret_cast<ID3D12GraphicsCommandList*>(1));
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC desc{};Hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> alloc[2];for(auto& a:alloc)Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)));
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc[0].Get(),nullptr,IID_PPV_ARGS(&list)));
    ComPtr<ID3D12Fence> gate,done;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    auto wait=[&](uint64_t value) {
        Hr(queue->Signal(done.Get(),value));auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Check(event!=nullptr,"event missing");
        Hr(done->SetEventOnCompletion(value,event));const auto status=WaitForSingleObject(event,5000);CloseHandle(event);Check(status==WAIT_OBJECT_0,"GPU timeout");
    };
    CyberpunkVR_SceneGpuRequest=2;scene_profile::Poll();
    scene_profile::Mark(list.Get(),123,0,nullptr);scene_profile::Mark(list.Get(),123,0,nullptr);
    scene_profile::Mark(list.Get(),456,1,nullptr);scene_profile::EndRange(list.Get());scene_profile::Close(list.Get());Hr(list->Close());
    ID3D12CommandList* commands[]{list.Get()};auto resources=PrepareCommandResources(1,commands);
    auto captured=scene_profile::BeforeSubmit(1,commands);Check(captured.size()==1,"missing submission");
    Hr(queue->Wait(gate.Get(),1));queue->ExecuteCommandLists(1,commands);
    SubmitCommandResources(queue.Get(),std::move(resources));scene_profile::Submitted(queue.Get(),std::move(captured));
    auto start=std::chrono::steady_clock::now();scene_profile::Poll();
    Check(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(50),"poll blocked");
    Check(std::strstr(CyberpunkVR_SceneGpuReport,"\"completed\":0"),"mapped incomplete GPU result");
    Hr(list->Reset(alloc[1].Get(),nullptr));ResetCommandResources(list.Get());scene_profile::Reset(list.Get());
    scene_profile::Mark(list.Get(),789,0,nullptr);scene_profile::Reset(list.Get());Hr(list->Close());
    Hr(gate->Signal(1));wait(1);scene_profile::Poll();CollectCommandResources();
    Check(!scene_profile::active && std::strstr(CyberpunkVR_SceneGpuReport,"\"completed\":1"),"profile did not finish");
    Check(std::strstr(CyberpunkVR_SceneGpuReport,"\"discarded\":1"),"discard not counted");
    Check(std::strstr(CyberpunkVR_SceneGpuReport,"\"node\":123,\"side\":0,\"pso\":0,\"ranges\":1,\"commands\":2"),"same-range draws received per-draw timestamps");
    Check(std::strstr(CyberpunkVR_SceneGpuReport,"\"node\":456,\"side\":1"),"second range missing");
    Check(!std::strstr(CyberpunkVR_SceneGpuReport,"\"node\":789"),"discarded range counted");
    Hr(alloc[0]->Reset());Hr(list->Reset(alloc[0].Get(),nullptr));ResetCommandResources(list.Get());
    CyberpunkVR_SceneGpuRequest=1;scene_profile::Poll();
    scene_profile::Mark(list.Get(),123,0,nullptr);scene_profile::Close(list.Get());Hr(list->Close());
    resources=PrepareCommandResources(1,commands);captured=scene_profile::BeforeSubmit(1,commands);
    queue->ExecuteCommandLists(1,commands);SubmitCommandResources(queue.Get(),std::move(resources));scene_profile::Submitted(queue.Get(),std::move(captured));
    Check(scene_profile::BeforeSubmit(1,commands).empty(),"same recording counted twice");
    wait(2);scene_profile::Poll();Check(std::strstr(CyberpunkVR_SceneGpuReport,"\"discarded\":1"),"replayed query buffer accepted");
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info)))for(uint64_t i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());info->GetMessage(i,message,&size);
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::cerr<<message->pDescription<<'\n';Check(false,"GPU validation error");}
    }
    std::cout<<"PASS whole-range timing, disabled path, delayed GPU/reset, cancellation, replay rejection, D3D12 validation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
