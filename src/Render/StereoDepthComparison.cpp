#include "Render/StereoDepthComparison.hpp"
#include "Render/CommandResources.hpp"
#include "Render/RenderProbeScope.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace cvr::stereo {
using Microsoft::WRL::ComPtr;
struct StereoDepthComparison::Storage final: IUnknown {
    struct Plane {D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows{};UINT64 rowBytes{};UINT stride{},component{};};
    std::atomic<ULONG> refs{1};ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;ComPtr<ID3D12Fence> fence;
    StereoBindingBank referenceBank;StereoGeometryCommands geometry;StereoGeometryPass paired;
    std::vector<ComPtr<ID3D12PipelineState>> originals;std::array<ComPtr<ID3D12Resource>,3> depth;
    ComPtr<ID3D12DescriptorHeap> dsvs;ComPtr<ID3D12Resource> readback;std::array<Plane,8> planes;
    DepthComparisonResult result;UINT64 readbackBytes{};float clear{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
StereoDepthComparison::StereoDepthComparison()=default;
StereoDepthComparison::~StereoDepthComparison()=default;
namespace {
bool ClearFor(const families::PassContract& pass,float& value) {
    if(!pass.depthStateKnown || !pass.depthState.DepthEnable || pass.depthState.DepthWriteMask!=D3D12_DEPTH_WRITE_MASK_ALL)return false;
    switch(pass.depthState.DepthFunc){
    case D3D12_COMPARISON_FUNC_LESS:case D3D12_COMPARISON_FUNC_LESS_EQUAL:case D3D12_COMPARISON_FUNC_ALWAYS:value=1;return true;
    case D3D12_COMPARISON_FUNC_GREATER:case D3D12_COMPARISON_FUNC_GREATER_EQUAL:value=0;return true;
    default:return false;
    }
}
// Private lists are never replayed. Detach recording ownership on every exit;
// an already-submitted immutable batch keeps all objects through its GPU fence.
struct RecordingGuard {ID3D12GraphicsCommandList* list;~RecordingGuard(){gpu::ResetCommandResources(list);}};
}
HRESULT StereoDepthComparison::Begin(ID3D12Device* device,std::span<const GeometryPassDraw> inputs,const D3D12_RESOURCE_DESC& target) try {
    stage_=1;
    if(storage_ || !device || inputs.empty() || inputs.size()>2048 || target.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        !target.Width || !target.Height || target.Width>4096 || target.Height>4096 || target.DepthOrArraySize!=1 || target.MipLevels!=1 ||
        target.SampleDesc.Count!=1 || target.SampleDesc.Quality || !(target.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) ||
        (target.Format!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT && target.Format!=DXGI_FORMAT_R32G8X24_TYPELESS))return E_INVALIDARG;
    ComPtr<Storage> pending;pending.Attach(new Storage);pending->device=device;
    pending->result.width=UINT(target.Width);pending->result.height=target.Height;pending->result.draws=UINT(inputs.size());
    std::vector<BankDraw> banks;std::vector<GeometryCommandPair> geometry;
    stage_=2;
    for(const auto& input:inputs) {
        if(!input.family || !input.family->pairedRoot || input.family->pass.pixelShader || input.family->pass.targets.NumRenderTargets ||
            input.family->pass.depth!=DXGI_FORMAT_D32_FLOAT_S8X24_UINT || !input.family->sourceRoot)return E_INVALIDARG;
        float clear{};if(!ClearFor(input.family->pass,clear) || (!banks.empty() && clear!=pending->clear))return E_INVALIDARG;
        pending->clear=clear;
        banks.push_back({input.family->pairedRoot->root.Get(),&input.family->pairedRoot->plan,input.constants,{},input.family->sourceRoot.Get()});
        geometry.push_back(input.geometry);pending->originals.push_back(input.geometry.eyes[0].state.original);
    }
    stage_=3;probe::Scope internal;auto hr=pending->referenceBank.CreatePass(device,banks);if(FAILED(hr))return hr;
    stage_=4;
    hr=pending->geometry.Create(device,geometry);if(FAILED(hr))return hr;
    stage_=5;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;clear.DepthStencil.Depth=pending->clear;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV,3,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    hr=device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&pending->dsvs));if(FAILED(hr))return hr;
    const auto stride=device->GetDescriptorHandleIncrementSize(hd.Type);const auto start=pending->dsvs->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<3;++i) {
        auto desc=target;desc.Alignment=0;desc.DepthOrArraySize=i==2?2:1;
        hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&pending->depth[i]));if(FAILED(hr))return hr;
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=clear.Format;view.ViewDimension=i==2?D3D12_DSV_DIMENSION_TEXTURE2DARRAY:D3D12_DSV_DIMENSION_TEXTURE2D;
        if(i==2)view.Texture2DArray.ArraySize=2;device->CreateDepthStencilView(pending->depth[i].Get(),&view,{start.ptr+i*stride});
    }
    stage_=6;hr=pending->paired.Create(device,inputs,{{},pending->depth[2]});if(FAILED(hr))return hr;
    stage_=7;
    for(UINT plane=0;plane<2;++plane)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<2;++mode) {
        auto& copy=pending->planes[plane*4+eye*2+mode];auto desc=pending->depth[mode?2:eye]->GetDesc();
        const UINT sub=mode?eye+plane*2:plane;UINT64 bytes{};
        device->GetCopyableFootprints(&desc,sub,1,pending->readbackBytes,&copy.layout,&copy.rows,&copy.rowBytes,&bytes);
        if(bytes==UINT64_MAX || copy.rowBytes%target.Width || copy.rows!=target.Height)return E_INVALIDARG;
        copy.stride=UINT(copy.rowBytes/target.Width);
        if((plane==0 && copy.stride!=4 && copy.stride!=8) || (plane==1 && copy.stride!=1 && copy.stride!=8))return E_NOTIMPL;
        copy.component=plane && copy.stride==8?4:0;
        pending->readbackBytes=(copy.layout.Offset+UINT64(copy.layout.Footprint.RowPitch)*copy.rows+511)&~UINT64(511);
        if(pending->readbackBytes>512*1024*1024)return E_OUTOFMEMORY;
    }
    stage_=8;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=pending->readbackBytes;
    buffer.Height=buffer.SampleDesc.Count=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_READBACK;hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&pending->readback));if(FAILED(hr))return hr;
    stage_=9;hr=device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&pending->allocator));if(FAILED(hr))return hr;
    hr=device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,pending->allocator.Get(),nullptr,IID_PPV_ARGS(&pending->commands));if(FAILED(hr))return hr;
    auto* list=pending->commands.Get();RecordingGuard recording{list};ComPtr<ID3D12GraphicsCommandList5> rates;
    hr=list->QueryInterface(IID_PPV_ARGS(&rates));if(FAILED(hr))return hr;
    if(!gpu::KeepCommandResources(list,{pending.Get()}))return E_OUTOFMEMORY;
    stage_=10;if(!pending->referenceBank.CaptureEye(list,0) || !pending->referenceBank.CaptureEye(list,1))return E_FAIL;
    rates->RSSetShadingRateImage(nullptr);rates->RSSetShadingRate(D3D12_SHADING_RATE_1X1,nullptr);
    for(UINT i=0;i<3;++i)list->ClearDepthStencilView({start.ptr+i*stride},D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,pending->clear,0,0,nullptr);
    for(UINT eye=0;eye<2;++eye) {
        const D3D12_CPU_DESCRIPTOR_HANDLE view{start.ptr+eye*stride};list->OMSetRenderTargets(0,nullptr,FALSE,&view);
        for(UINT i=0;i<inputs.size();++i)if(inputs[i].viewMask&(1u<<eye)) {
            const auto& source=inputs[i].geometry.eyes[eye];const auto& draw=source.draw;
            list->SetPipelineState(source.state.original.Get());if(!pending->referenceBank.BindSourceEye(list,eye,i))return E_FAIL;
            list->IASetPrimitiveTopology(source.state.topology);list->OMSetStencilRef(source.state.stencil);list->OMSetBlendFactor(source.state.blendFactor.data());
            list->RSSetViewports(1,&draw.viewport);list->RSSetScissorRects(1,&draw.scissor);
            if(!pending->geometry.RecordGeometry(list,i))return E_FAIL;
        }
    }
    stage_=11;if(!pending->paired.Record(list))return E_FAIL;
    stage_=12;
    for(const auto& depth:pending->depth){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={depth.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);}
    for(UINT plane=0;plane<2;++plane)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<2;++mode) {
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=pending->depth[mode?2:eye].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=mode?eye+plane*2:plane;
        to.pResource=pending->readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=pending->planes[plane*4+eye*2+mode].layout;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    }
    hr=list->Close();if(FAILED(hr))return hr;
    stage_=13;D3D12_COMMAND_QUEUE_DESC q{};hr=device->CreateCommandQueue(&q,IID_PPV_ARGS(&pending->queue));if(FAILED(hr))return hr;
    hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&pending->fence));if(FAILED(hr))return hr;
    ID3D12CommandList* raw=list;auto flight=gpu::PrepareCommandResources(1,&raw);
    stage_=14;pending->queue->ExecuteCommandLists(1,&raw);gpu::SubmitCommandResources(pending->queue.Get(),std::move(flight));
    hr=pending->queue->Signal(pending->fence.Get(),1);if(FAILED(hr))return hr;
    storage_=std::move(pending);stage_=0;return S_OK;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}
HRESULT StereoDepthComparison::Read(DepthComparisonResult& result) {
    if(!storage_)return E_UNEXPECTED;auto& s=*storage_.Get();
    if(s.result.complete){result=s.result;return S_OK;}
    const auto completed=s.fence->GetCompletedValue();if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;if(completed<1)return S_FALSE;
    void* mapped{};const D3D12_RANGE range{0,SIZE_T(s.readbackBytes)};auto hr=s.readback->Map(0,&range,&mapped);if(FAILED(hr))return hr;
    if(!mapped){s.readback->Unmap(0,nullptr);return E_FAIL;}
    const auto* bytes=static_cast<const uint8_t*>(mapped);const auto clear=std::bit_cast<uint32_t>(s.clear);
    for(UINT eye=0;eye<2;++eye)for(UINT plane=0;plane<2;++plane) {
        const auto& a=s.planes[plane*4+eye*2];const auto& b=s.planes[plane*4+eye*2+1];
        for(UINT y=0;y<s.result.height;++y)for(UINT x=0;x<s.result.width;++x) {
            const auto* left=bytes+a.layout.Offset+UINT64(y)*a.layout.Footprint.RowPitch+UINT64(x)*a.stride+a.component;
            const auto* right=bytes+b.layout.Offset+UINT64(y)*b.layout.Footprint.RowPitch+UINT64(x)*b.stride+b.component;
            if(plane){s.result.stencilDifferent[eye]+=*left!=*right;continue;}
            uint32_t av{},bv{};std::memcpy(&av,left,4);std::memcpy(&bv,right,4);s.result.written[eye]+=av!=clear;s.result.depthDifferent[eye]+=av!=bv;
            const auto af=std::bit_cast<float>(av),bf=std::bit_cast<float>(bv);
            if(!std::isfinite(af) || !std::isfinite(bf)){++s.result.invalidDepth[eye];continue;}
            s.result.maxDepthError[eye]=std::max(s.result.maxDepthError[eye],std::abs(af-bf));
        }
    }
    const D3D12_RANGE none{};s.readback->Unmap(0,&none);s.result.complete=1;result=s.result;return S_OK;
}
}
