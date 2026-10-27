#include "Render/InstanceGpuProbe.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("D3D12 instance probe call failed");}
static void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:
        D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result)));return result;
}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::string(argv[1])=="--hardware";
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){for(UINT index=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++index){
            DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();}
        if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    auto source=Buffer(device.Get(),4096,D3D12_HEAP_TYPE_UPLOAD),zero=Buffer(device.Get(),192,D3D12_HEAP_TYPE_UPLOAD);
    auto predicate=Buffer(device.Get(),16,D3D12_HEAP_TYPE_UPLOAD),readback=Buffer(device.Get(),192,D3D12_HEAP_TYPE_READBACK);
    void* mapped{};const D3D12_RANGE none{};
    Check(source->Map(0,&none,&mapped));std::memset(mapped,0,4096);
    std::array<uint8_t,64> expected{};for(UINT index=0;index<expected.size();++index)expected[index]=uint8_t(index*3+1);
    std::memcpy(static_cast<uint8_t*>(mapped)+960,expected.data(),expected.size());source->Unmap(0,nullptr);
    Check(zero->Map(0,&none,&mapped));std::memset(mapped,0,192);zero->Unmap(0,nullptr);
    Check(predicate->Map(0,&none,&mapped));std::memset(mapped,0,16);predicate->Unmap(0,nullptr);
    list->CopyBufferRegion(readback.Get(),0,zero.Get(),0,192);
    cvr::stereo::instance_gpu_probe::Record(list.Get(),source.Get(),960,readback.Get(),0,64,predicate.Get(),0);
    cvr::stereo::instance_gpu_probe::Record(list.Get(),source.Get(),960,readback.Get(),64,64,predicate.Get(),8);
    list->CopyBufferRegion(readback.Get(),128,source.Get(),960,64);Check(list->Close());
    // Final source validation happens after recording but before submission.
    Check(predicate->Map(0,&none,&mapped));static_cast<uint64_t*>(mapped)[1]=1;predicate->Unmap(0,nullptr);
    ID3D12CommandList* raw=list.Get();queue->ExecuteCommandLists(1,&raw);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");
    const auto signal=fence->SetEventOnCompletion(1,event);if(FAILED(signal)){CloseHandle(event);Check(signal);}
    const auto wait=WaitForSingleObject(event,10000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"Instance probe timeout");
    const D3D12_RANGE range{0,192};Check(readback->Map(0,&range,&mapped));const auto* result=static_cast<const uint8_t*>(mapped);
    const std::array<uint8_t,64> empty{};Require(!std::memcmp(result,empty.data(),64),"Rejected source was copied despite zero predicate");
    Require(!std::memcmp(result+64,expected.data(),64),"Accepted original instance bytes differ");
    Require(!std::memcmp(result+128,expected.data(),64),"Diagnostic predication leaked into the next native command");readback->Unmap(0,&none);
    if(messages)for(UINT64 index=0;index<messages->GetNumStoredMessages();++index){SIZE_T size{};Check(messages->GetMessage(index,nullptr,&size));
        std::vector<uint8_t> storage(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(messages->GetMessage(index,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("Instance probe debug layer error");}}
    std::puts("PASS selected UPLOAD instance GPU bytes, per-source predicate finalized before submit, rejected source skipped and native predication restored");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
