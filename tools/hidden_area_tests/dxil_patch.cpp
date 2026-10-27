// In-place DXIL constant patches on the real adapter (src/Render/DxilPatch.cpp).
//
// A compute shader with two placeholder constants is compiled by the Windows SDK's dxcompiler, its
// container hash is checked against our implementation, both placeholders are found exactly once and
// rewritten, the container is re-signed, and the device runs it: out = in * A + B must use the new
// values. That proves the driver accepts the rewritten bitcode and that the constants took effect.
//   hidden_area_dxil_patch [DXC directory]
#include "Render/DxilPatch.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace cvr::dxil_patch;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}}while(0)

static const char* Source=R"hlsl(
#define RS "RootFlags(0), UAV(u0)"
RWStructuredBuffer<float> data : register(u0);
[RootSignature(RS)] [numthreads(1,1,1)] void main() { data[1] = data[0] * 1.2345678f + 1.3456789f; }
)hlsl";

static std::vector<uint8_t> Compile(const std::wstring& dxcDirectory) {
    const auto path=dxcDirectory+L"\\dxcompiler.dll";
    HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto create=module?reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(module,"DxcCreateInstance")):nullptr;
    if(!create){std::printf("dxcompiler not found in %ls\n",dxcDirectory.c_str());return {};}
    ComPtr<IDxcCompiler3> compiler;ComPtr<IDxcResult> result;ComPtr<IDxcBlob> object;
    if(FAILED(create(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler))))return {};
    const DxcBuffer buffer{Source,std::strlen(Source),DXC_CP_UTF8};
    LPCWSTR args[]{L"-T",L"cs_6_0",L"-E",L"main"};
    if(FAILED(compiler->Compile(&buffer,args,4,nullptr,IID_PPV_ARGS(&result))))return {};
    HRESULT status{};result->GetStatus(&status);
    if(FAILED(status) || FAILED(result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr)) || !object)return {};
    const auto* bytes=static_cast<const uint8_t*>(object->GetBufferPointer());
    return {bytes,bytes+object->GetBufferSize()};
}

static bool Run(ID3D12Device* device,const std::vector<uint8_t>& shader,float input,float& output) {
    ComPtr<ID3D12RootSignature> root;ComPtr<ID3D12PipelineState> pso;
    if(FAILED(device->CreateRootSignature(0,shader.data(),shader.size(),IID_PPV_ARGS(&root))))return false;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root.Get();desc.CS={shader.data(),shader.size()};
    if(FAILED(device->CreateComputePipelineState(&desc,IID_PPV_ARGS(&pso))))return false;
    D3D12_HEAP_PROPERTIES upload{D3D12_HEAP_TYPE_UPLOAD},readback{D3D12_HEAP_TYPE_READBACK},local{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC buffer{D3D12_RESOURCE_DIMENSION_BUFFER,0,256,1,1,1,DXGI_FORMAT_UNKNOWN,{1,0},D3D12_TEXTURE_LAYOUT_ROW_MAJOR};
    ComPtr<ID3D12Resource> source,data,result;
    if(FAILED(device->CreateCommittedResource(&upload,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&source))) ||
       FAILED(device->CreateCommittedResource(&readback,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result))))return false;
    buffer.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if(FAILED(device->CreateCommittedResource(&local,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&data))))return false;
    void* mapped{};source->Map(0,nullptr,&mapped);float values[2]{input,0};std::memcpy(mapped,values,8);source->Unmap(0,nullptr);
    D3D12_COMMAND_QUEUE_DESC qd{};ComPtr<ID3D12CommandQueue> queue;ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    if(FAILED(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue))) ||
       FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))) ||
       FAILED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),pso.Get(),IID_PPV_ARGS(&list))))return false;
    list->CopyBufferRegion(data.Get(),0,source.Get(),0,256);
    D3D12_RESOURCE_BARRIER b{};b.Transition={data.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};list->ResourceBarrier(1,&b);
    list->SetComputeRootSignature(root.Get());list->SetComputeRootUnorderedAccessView(0,data->GetGPUVirtualAddress());list->Dispatch(1,1,1);
    b.Transition={data.Get(),0,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);
    list->CopyBufferRegion(result.Get(),0,data.Get(),0,256);
    if(FAILED(list->Close()))return false;
    ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence));
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);queue->Signal(fence.Get(),1);fence->SetEventOnCompletion(1,event);
    if(WaitForSingleObject(event,5000)!=WAIT_OBJECT_0)return false;
    result->Map(0,nullptr,&mapped);std::memcpy(values,mapped,8);result->Unmap(0,nullptr);
    output=values[1];return true;
}

int wmain(int argc,wchar_t** argv) {
    const std::wstring dxc=argc>1?argv[1]:L"C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64";
    auto shader=Compile(dxc);
    CHECK(!shader.empty());if(shader.empty())return 1;
    CHECK(ContainerHash(shader.data(),shader.size())==*reinterpret_cast<const Hash*>(shader.data()+4));
    CHECK(PlaceholderValue(1.0f) && PlaceholderValue(1.9999f) && !PlaceholderValue(2.0f) && !PlaceholderValue(0.99f));
    const auto a=FindFloat(shader.data(),shader.size(),1.2345678f), b=FindFloat(shader.data(),shader.size(),1.3456789f);
    CHECK(a.size()==1 && b.size()==1);
    if(a.size()!=1 || b.size()!=1)return 1;
    auto patched=shader;
    CHECK(WriteFloat(patched,a[0],1.5f) && WriteFloat(patched,b[0],1.75f) && !WriteFloat(patched,a[0],2.5f));
    CHECK(FindFloat(patched.data(),patched.size(),1.2345678f).empty() && FindFloat(patched.data(),patched.size(),1.5f).size()==1);
    CHECK(Sign(patched) && ContainerHash(patched.data(),patched.size())==*reinterpret_cast<const Hash*>(patched.data()+4));
    CHECK(patched.size()==shader.size());

    ComPtr<IDXGIFactory6> factory;ComPtr<IDXGIAdapter1> adapter;ComPtr<ID3D12Device> device;
    if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory))) ||
       FAILED(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))) ||
       FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)))){std::printf("no D3D12 device\n");return 1;}
    float original{},rewritten{},unsigned_{};
    CHECK(Run(device.Get(),shader,2.0f,original) && std::fabs(original-(2.0f*1.2345678f+1.3456789f))<1e-5f);
    CHECK(Run(device.Get(),patched,2.0f,rewritten) && rewritten==4.75f);
    auto stale=patched;std::memcpy(stale.data()+4,shader.data()+4,16);   // rewritten but not re-signed
    CHECK(!Run(device.Get(),stale,2.0f,unsigned_));
    std::printf("original %.7f, rewritten %.7f (expected 4.75); stale hash refused: %s\n",original,rewritten,failures?"?":"yes");
    std::printf(failures?"%d failures\n":"all passed\n",failures);
    return failures?1:0;
}
