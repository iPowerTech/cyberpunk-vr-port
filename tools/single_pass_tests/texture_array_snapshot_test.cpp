#include "Render/NativeTextureRead.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
namespace texture=cvr::stereo::texture_read;
static void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("HRESULT "+std::to_string(UINT(result)));}
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static ComPtr<ID3D12Resource> Resource(ID3D12Device* device,const D3D12_RESOURCE_DESC& desc,
    D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&resource)));return resource;
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,uint64_t bytes,D3D12_HEAP_TYPE type,
    D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
    return Resource(device,desc,type,state);
}
static void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
static void TextureCopy(ID3D12GraphicsCommandList* list,ID3D12Resource* image,ID3D12Resource* buffer,
    const texture::MipFootprint& footprint,bool toImage){
    D3D12_TEXTURE_COPY_LOCATION imageLocation{},bufferLocation{};imageLocation.pResource=image;
    imageLocation.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;imageLocation.SubresourceIndex=footprint.mip;
    bufferLocation.pResource=buffer;bufferLocation.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;bufferLocation.PlacedFootprint=footprint.layout;
    list->CopyTextureRegion(toImage?&imageLocation:&bufferLocation,0,0,0,toImage?&bufferLocation:&imageLocation,nullptr);
}
static uint32_t Pixel(UINT subresource,UINT x,UINT y){return subresource*100000+y*100+x+1;}
int main(int argc,char** argv)try{
    const bool hardware=argc>1 && std::string(argv[1])=="hardware";
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    if(hardware){
        for(UINT index=0;SUCCEEDED(factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));++index){
            DXGI_ADAPTER_DESC1 desc{};Check(adapter->GetDesc1(&desc));if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))break;adapter.Reset();
        }
        if(!adapter)return 77;
    }else Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    ComPtr<ID3D12InfoQueue> messages;device.As(&messages);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=64;desc.Height=32;
    desc.DepthOrArraySize=4;desc.MipLevels=4;desc.Format=DXGI_FORMAT_R32_UINT;desc.SampleDesc.Count=1;
    D3D12_SHADER_RESOURCE_VIEW_DESC full{};full.Format=desc.Format;full.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    full.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;full.Texture2DArray={0,UINT_MAX,0,4,0,0};
    auto selected=full;selected.Texture2DArray={1,2,1,2,0,0};
    std::vector<texture::MipFootprint> all,snapshot;
    Require(texture::detail::ViewLayouts(device.Get(),desc,full,0,all) && all.size()==16,"Full array layout failed");
    Require(texture::detail::ViewLayouts(device.Get(),desc,selected,512,snapshot) && snapshot.size()==4,"Selected array layout failed");
    const std::array<UINT,4> expectedIndices{5,6,9,10};
    for(UINT index=0;index<snapshot.size();++index)Require(snapshot[index].mip==expectedIndices[index],"Original mip/layer index was rebased");
    const auto allBytes=all.back().layout.Offset+all.back().bytes,snapshotBytes=snapshot.back().layout.Offset+snapshot.back().bytes;
    auto upload=Buffer(device.Get(),allBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* mapped{};Check(upload->Map(0,nullptr,&mapped));std::memset(mapped,0xcd,size_t(allBytes));
    for(const auto& footprint:all)for(UINT y=0;y<footprint.rows;++y)for(UINT x=0;x<footprint.layout.Footprint.Width;++x){
        const auto value=Pixel(footprint.mip,x,y);std::memcpy(static_cast<uint8_t*>(mapped)+footprint.layout.Offset+
            uint64_t(y)*footprint.layout.Footprint.RowPitch+x*4,&value,4);
    }
    upload->Unmap(0,nullptr);
    auto mutation=Buffer(device.Get(),allBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    Check(mutation->Map(0,nullptr,&mapped));std::memset(mapped,0,size_t(allBytes));const auto& changed=all[5];
    for(UINT y=0;y<changed.rows;++y)for(UINT x=0;x<changed.layout.Footprint.Width;++x){const auto value=Pixel(5,x,y)^0x80000000u;
        std::memcpy(static_cast<uint8_t*>(mapped)+changed.layout.Offset+uint64_t(y)*changed.layout.Footprint.RowPitch+x*4,&value,4);}
    mutation->Unmap(0,nullptr);
    auto original=Resource(device.Get(),desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto owned=Resource(device.Get(),desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto packed=Buffer(device.Get(),snapshotBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto packedReadback=Buffer(device.Get(),snapshotBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    auto output=Buffer(device.Get(),36*4,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback=Buffer(device.Get(),36*4,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&heap)));
    const auto stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    device->CreateShaderResourceView(original.Get(),&selected,heap->GetCPUDescriptorHandleForHeapStart());
    device->CreateShaderResourceView(owned.Get(),&selected,{heap->GetCPUDescriptorHandleForHeapStart().ptr+stride});
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[0].DescriptorTable={1,&range};
    parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[1].Descriptor={0,0};
    parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants={0,0,1};
    const D3D12_ROOT_SIGNATURE_DESC rootDesc{3,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> rootBytes,error;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    constexpr char shader[]=R"(
Texture2DArray<uint> image : register(t0);
RWStructuredBuffer<uint> result : register(u0);
cbuffer Parameters : register(b0) { uint outputBase; };
[numthreads(12,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint sample = id.x % 3;
    result[outputBase+id.x] = image.Load(uint4(sample+1,sample+2,id.x/6,(id.x/3)%2));
})";
    ComPtr<ID3DBlob> bytecode;const auto compiled=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS,0,&bytecode,&error);
    if(FAILED(compiled) && error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));Check(compiled);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};pipelineDesc.pRootSignature=root.Get();pipelineDesc.CS={bytecode->GetBufferPointer(),bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateComputePipelineState(&pipelineDesc,IID_PPV_ARGS(&pipeline)));
    for(const auto& footprint:all)TextureCopy(list.Get(),original.Get(),upload.Get(),footprint,true);
    constexpr auto readState=D3D12_RESOURCE_STATES(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_COPY_SOURCE);
    Barrier(list.Get(),original.Get(),D3D12_RESOURCE_STATE_COPY_DEST,readState);
    ID3D12DescriptorHeap* heaps[]={heap.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipeline.Get());
    list->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());
    auto dispatch=[&](UINT slot,UINT base){list->SetComputeRootDescriptorTable(0,{heap->GetGPUDescriptorHandleForHeapStart().ptr+uint64_t(slot)*stride});
        list->SetComputeRoot32BitConstant(2,base,0);list->Dispatch(1,1,1);D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=output.Get();list->ResourceBarrier(1,&barrier);};
    dispatch(0,0);
    for(const auto& footprint:snapshot)TextureCopy(list.Get(),original.Get(),packed.Get(),footprint,false);
    Barrier(list.Get(),packed.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(packedReadback.Get(),0,packed.Get(),0,snapshotBytes);
    for(const auto& footprint:snapshot)TextureCopy(list.Get(),owned.Get(),packed.Get(),footprint,true);
    Barrier(list.Get(),owned.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Barrier(list.Get(),original.Get(),readState,D3D12_RESOURCE_STATE_COPY_DEST);TextureCopy(list.Get(),original.Get(),mutation.Get(),changed,true);
    Barrier(list.Get(),original.Get(),D3D12_RESOURCE_STATE_COPY_DEST,readState);
    dispatch(0,12);dispatch(1,24);
    Barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.Get(),0,output.Get(),0,36*4);Check(list->Close());ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event unavailable");
    const auto signal=fence->SetEventOnCompletion(1,event);if(FAILED(signal)){CloseHandle(event);Check(signal);}
    const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Array GPU test timed out");
    Check(packedReadback->Map(0,nullptr,&mapped));
    for(const auto& footprint:snapshot)for(UINT y=0;y<footprint.rows;++y)for(UINT x=0;x<footprint.layout.Footprint.Width;++x){uint32_t value{};
        std::memcpy(&value,static_cast<uint8_t*>(mapped)+footprint.layout.Offset+uint64_t(y)*footprint.layout.Footprint.RowPitch+x*4,4);
        Require(value==Pixel(footprint.mip,x,y),"Snapshot GPU bytes differ from their original subresource");}
    packedReadback->Unmap(0,nullptr);Check(readback->Map(0,nullptr,&mapped));const auto* results=static_cast<const uint32_t*>(mapped);
    for(UINT index=0;index<12;++index){const UINT layer=index/6,mip=(index/3)%2,sample=index%3,subresource=1+mip+(1+layer)*4;
        const auto expected=Pixel(subresource,sample+1,sample+2);
        Require(results[index]==expected && results[24+index]==expected,"Private Texture2DArray shader indexing differs");
        Require(results[12+index]==(subresource==5?(expected^0x80000000u):expected),"Source mutation did not remain isolated from the owned array");}
    readback->Unmap(0,nullptr);
    if(messages)for(UINT64 index=0;index<messages->GetNumStoredMessagesAllowedByRetrievalFilter();++index){SIZE_T bytes{};
        Check(messages->GetMessage(index,nullptr,&bytes));std::vector<uint8_t> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(messages->GetMessage(index,message,&bytes));if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){
            std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 debug layer reported an array error");}}
    std::printf("Texture2DArray:4 original-index subresources, exact GPU bytes,12 shader samples and source mutation isolation (%s)\n",hardware?"hardware":"WARP");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
