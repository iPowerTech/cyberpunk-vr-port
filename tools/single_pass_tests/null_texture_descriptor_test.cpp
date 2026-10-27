#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT result,const char* message){
    if(FAILED(result)){std::fprintf(stderr,"%s HRESULT=%08X\n",message,unsigned(result));throw std::runtime_error(message);}
}
// The null descriptor spec still requires plausible numeric view parameters:
// https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html#null-descriptors
// A passing probe describes this adapter/runtime, not a portable zero-mip ABI.
static constexpr char Shader[]=R"(
#ifdef ARRAY_NULL
Texture2DArray<uint> tested : register(t0);
#else
Texture2D<uint> tested : register(t0);
#endif
Texture2D<uint> control : register(t1);
RWStructuredBuffer<uint> output : register(u0);
cbuffer Constants : register(b0) {uint caseTag;}
[numthreads(8,1,1)]
void Main(uint lane : SV_GroupIndex) {
    int2 coordinates[8]={int2(0,0),int2(1,1),int2(123,456),int2(-1,-2),
                         int2(0,0),int2(1,1),int2(0,0),int2(4095,4095)};
    int mips[8]={0,0,0,0,1,5,15,0};
#ifdef ARRAY_NULL
    output[lane]=tested.Load(int4(coordinates[lane],lane%3,mips[lane]));
#else
    output[lane]=tested.Load(int3(coordinates[lane],mips[lane]));
#endif
    output[8+lane]=control.Load(int3(lane&1,(lane>>1)&1,0));
    output[16+lane]=0xC51F0000u^(caseTag<<8)^lane;
}
)";
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,
                                    D3D12_RESOURCE_STATES state,D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> result;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&result)),"buffer creation");return result;
}
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
struct Messages {UINT errors{},warnings{};};
static Messages Diagnostics(ID3D12InfoQueue* queue,const char* phase){
    Messages result;
    for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T bytes{};Check(queue->GetMessage(i,nullptr,&bytes),"message length");
        std::vector<uint8_t> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(queue->GetMessage(i,message,&bytes),"message retrieval");
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)++result.errors;
        else if(message->Severity==D3D12_MESSAGE_SEVERITY_WARNING)++result.warnings;
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)
            std::printf("DEBUG phase=%s id=%u severity=%u %s\n",phase,unsigned(message->ID),unsigned(message->Severity),message->pDescription);
    }
    queue->ClearStoredMessages();return result;
}
struct Gpu {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12InfoQueue> messages;ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> texturePipeline,arrayPipeline;
    UINT64 serial{};
    explicit Gpu(bool hardware){
        ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug interface");debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");ComPtr<IDXGIAdapter1> adapter;
        Check(hardware?factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)):
            factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"adapter");
        Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12 device");
        Check(device.As(&messages),"info queue");Check(messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,FALSE),"debug no break");
        Check(messages->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE),"debug no break");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)),"command queue");
        Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
        D3D12_DESCRIPTOR_RANGE ranges[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,0}};
        std::array<D3D12_ROOT_PARAMETER,3> parameters{};
        for(UINT i=0;i<2;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};}
        parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[2].Constants={0,0,1};
        const D3D12_ROOT_SIGNATURE_DESC desc{UINT(parameters.size()),parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ComPtr<ID3DBlob> binary,errors;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&binary,&errors),"root serialization");
        Check(device->CreateRootSignature(0,binary->GetBufferPointer(),binary->GetBufferSize(),IID_PPV_ARGS(&root)),"root creation");
        for(bool array:{false,true}){const D3D_SHADER_MACRO macros[]{{"ARRAY_NULL","1"},{nullptr,nullptr}};
            const auto result=D3DCompile(Shader,sizeof(Shader)-1,nullptr,array?macros:nullptr,nullptr,"Main","cs_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
            if(FAILED(result) && errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
            Check(result,"compute compilation");D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root.Get();
            pipeline.CS={binary->GetBufferPointer(),binary->GetBufferSize()};
            ComPtr<ID3D12PipelineState> created;Check(device->CreateComputePipelineState(&pipeline,IID_PPV_ARGS(&created)),"compute pipeline");
            if(array)arrayPipeline=std::move(created);else texturePipeline=std::move(created);
        }
        Require(!Diagnostics(messages.Get(),"setup").errors,"setup debug errors");
    }
    bool Execute(ID3D12GraphicsCommandList* list,bool required,const char* name,Messages& recording){
        const auto closed=list->Close();recording=Diagnostics(messages.Get(),name);
        if(FAILED(closed) || recording.errors){
            std::printf("RESULT case=%s recording=REJECTED closeHRESULT=%08X errors=%u gpu=NOT_EXECUTED nullQualified=0\n",
                name,unsigned(closed),recording.errors);
            Require(!required,"normalized null command list rejected");return false;
        }
        ID3D12CommandList* lists[]{list};queue->ExecuteCommandLists(1,lists);
        Check(queue->Signal(fence.Get(),++serial),"queue signal");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"fence event");
        const auto result=fence->SetEventOnCompletion(serial,event);
        if(FAILED(result)){CloseHandle(event);Check(result,"fence completion");}
        const auto wait=WaitForSingleObject(event,15000);CloseHandle(event);
        Require(wait==WAIT_OBJECT_0,"GPU completion timeout");Check(device->GetDeviceRemovedReason(),"device removed");return true;
    }
};
struct Case {const char* name;UINT tag,mips,arraySize;bool array,required;};
static bool Probe(Gpu& gpu,const Case& test){
    auto* device=gpu.device.Get();D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,3,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    ComPtr<ID3D12DescriptorHeap> descriptors;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&descriptors)),"descriptor heap");
    const auto cpu=descriptors->GetCPUDescriptorHandleForHeapStart();const auto visible=descriptors->GetGPUDescriptorHandleForHeapStart();
    const UINT stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
    D3D12_SHADER_RESOURCE_VIEW_DESC null{};null.Format=DXGI_FORMAT_R32_UINT;
    null.ViewDimension=test.array?D3D12_SRV_DIMENSION_TEXTURE2DARRAY:D3D12_SRV_DIMENSION_TEXTURE2D;
    null.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if(test.array){null.Texture2DArray.MipLevels=test.mips;null.Texture2DArray.ArraySize=test.arraySize;}
    else null.Texture2D.MipLevels=test.mips;
    Require(!Diagnostics(gpu.messages.Get(),"precreate").errors,"precreate debug errors");
    device->CreateShaderResourceView(nullptr,&null,cpu);
    const auto creation=Diagnostics(gpu.messages.Get(),test.name);
    if(creation.errors){
        std::printf("RESULT case=%s creation=REJECTED errors=%u warnings=%u gpu=NOT_EXECUTED nullQualified=0\n",test.name,creation.errors,creation.warnings);
        Require(!test.required,"normalized null descriptor rejected by debug layer");return false;
    }
    D3D12_RESOURCE_DESC textureDesc{};textureDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;textureDesc.Width=textureDesc.Height=2;
    textureDesc.DepthOrArraySize=textureDesc.MipLevels=textureDesc.SampleDesc.Count=1;textureDesc.Format=DXGI_FORMAT_R32_UINT;
    D3D12_HEAP_PROPERTIES defaultHeap{};defaultHeap.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> control;
    Check(device->CreateCommittedResource(&defaultHeap,D3D12_HEAP_FLAG_NONE,&textureDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,
        IID_PPV_ARGS(&control)),"control texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows{};UINT64 rowBytes{},uploadBytes{};
    device->GetCopyableFootprints(&textureDesc,0,1,0,&footprint,&rows,&rowBytes,&uploadBytes);
    Require(rows==2 && rowBytes==8 && uploadBytes>=footprint.Footprint.RowPitch+8,"control footprint");
    auto upload=Buffer(device,uploadBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    const std::array<UINT,4> expected{101,223,347,509};void* mapped{};const D3D12_RANGE noRead{};
    Check(upload->Map(0,&noRead,&mapped),"control map");std::memset(mapped,0,SIZE_T(uploadBytes));
    std::memcpy(static_cast<uint8_t*>(mapped)+footprint.Offset,expected.data(),8);
    std::memcpy(static_cast<uint8_t*>(mapped)+footprint.Offset+footprint.Footprint.RowPitch,expected.data()+2,8);upload->Unmap(0,nullptr);
    constexpr UINT resultBytes=24*sizeof(UINT);auto result=Buffer(device,resultBytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto sentinel=Buffer(device,resultBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback=Buffer(device,resultBytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    Check(sentinel->Map(0,&noRead,&mapped),"sentinel map");std::memset(mapped,0xCD,resultBytes);sentinel->Unmap(0,nullptr);
    auto controlView=null;controlView.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;controlView.Texture2D={0,1,0,0};
    device->CreateShaderResourceView(control.Get(),&controlView,{cpu.ptr+stride});
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=24;uav.Buffer.StructureByteStride=4;
    device->CreateUnorderedAccessView(result.Get(),nullptr,&uav,{cpu.ptr+2*stride});
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");
    D3D12_TEXTURE_COPY_LOCATION source{},destination{};source.pResource=upload.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint=footprint;
    destination.pResource=control.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);list->CopyBufferRegion(result.Get(),0,sentinel.Get(),0,resultBytes);
    Transition(list.Get(),control.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(list.Get(),result.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* heaps[]{descriptors.Get()};list->SetDescriptorHeaps(1,heaps);list->SetComputeRootSignature(gpu.root.Get());
    list->SetPipelineState(test.array?gpu.arrayPipeline.Get():gpu.texturePipeline.Get());list->SetComputeRootDescriptorTable(0,visible);
    list->SetComputeRootDescriptorTable(1,{visible.ptr+UINT64(2)*stride});list->SetComputeRoot32BitConstant(2,test.tag,0);list->Dispatch(1,1,1);
    Transition(list.Get(),result.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(readback.Get(),0,result.Get(),0,resultBytes);Messages recording;
    if(!gpu.Execute(list.Get(),test.required,test.name,recording))return false;
    const auto execution=Diagnostics(gpu.messages.Get(),test.name);Require(!execution.errors,"GPU null-descriptor debug validation failed");
    const D3D12_RANGE outputRange{0,resultBytes};Check(readback->Map(0,&outputRange,&mapped),"readback map");
    std::array<UINT,24> values{};std::memcpy(values.data(),mapped,resultBytes);readback->Unmap(0,&noRead);
    for(UINT i=0;i<8;++i){
        Require(values[i]==0,"null texture returned a nonzero value");
        Require(values[8+i]==expected[i%4],"real texture control did not execute correctly");
        Require(values[16+i]==(0xC51F0000u^(test.tag<<8)^i),"compute execution sentinel was not overwritten");
    }
    std::printf("RESULT case=%s creation=ACCEPTED gpu=ZERO_READS_8 control=NONZERO_8 markers=WRITTEN_8 warnings=%u portableZeroMipContract=0\n",
        test.name,creation.warnings+recording.warnings+execution.warnings);return true;
}
int main(int argc,char** argv)try{
    bool hardware=false;
    for(int i=1;i<argc;++i){if(!std::strcmp(argv[i],"--hardware"))hardware=true;else throw std::runtime_error("unknown argument");}
    Gpu gpu(hardware);UINT normalized{},observed{},rejected{};
    const std::array cases{Case{"texture2d_mips1",1,1,0,false,true},Case{"texture2d_native_mips0",2,0,0,false,false},
        Case{"texture2darray_mips1_array1",3,1,1,true,true},Case{"texture2darray_mips0_array0",4,0,0,true,false}};
    for(const auto& test:cases){const bool ran=Probe(gpu,test);if(test.required)normalized+=ran;else{observed+=ran;rejected+=!ran;}}
    Require(normalized==2,"normalized controls did not complete");
    std::printf("PASS %s null-descriptor diagnostic: normalized=%u zeroGeometryObserved=%u zeroGeometryRejected=%u; rejected cases confer no qualification\n",
        hardware?"hardware":"WARP",normalized,observed,rejected);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL null-descriptor diagnostic: %s\n",error.what());return 1;}
