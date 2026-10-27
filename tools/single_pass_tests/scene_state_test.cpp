#include "Render/StereoSceneState.hpp"
#include "Render/RenderProbeScope.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <stdexcept>
#include <limits>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo::scene_state;
namespace cvr::stereo::native_probe {bool Enabled(){return false;}bool ObserveBindings(){return true;}}
extern "C" {__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};}
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D12 failed");}
static void Require(bool v){if(!v)throw std::runtime_error("Scene binding/descriptor validation failed");}
static void BoundTargetResources(ID3D12Device* device,const cvr::stereo::native_probe::DrawRecord& nativeDraw,
                                 ID3D12Resource* nativeColor,ID3D12Resource* nativeDepth){
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heapDesc.NumDescriptors=5;
    ComPtr<ID3D12DescriptorHeap> colors,depths;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&colors)));
    heapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heapDesc.NumDescriptors=2;Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&depths)));
    const auto stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,5> handles{};
    for(UINT i=0;i<handles.size();++i)handles[i]={colors->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(i)*stride};
    const auto depthHandle=depths->GetCPUDescriptorHandleForHeapStart();
    D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=64;desc.Height=32;
    desc.DepthOrArraySize=2;desc.MipLevels=1;desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    std::array<ComPtr<ID3D12Resource>,4> owned;
    for(UINT i=0;i<owned.size();++i){
        if(i==2){desc.DepthOrArraySize=1;desc.MipLevels=2;}
        if(i==3){desc.DepthOrArraySize=2;desc.MipLevels=1;desc.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;}
        Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&desc,
            i==3?D3D12_RESOURCE_STATE_DEPTH_READ:D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&owned[i])));
    }
    D3D12_RENDER_TARGET_VIEW_DESC arrayView{};arrayView.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    arrayView.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;arrayView.Texture2DArray.ArraySize=2;
    D3D12_RENDER_TARGET_VIEW_DESC mipView{};mipView.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    mipView.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;mipView.Texture2D.MipSlice=1;
    D3D12_DEPTH_STENCIL_VIEW_DESC depthView{};depthView.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    depthView.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;depthView.Texture2DArray.ArraySize=2;
    depthView.Flags=D3D12_DSV_FLAG_READ_ONLY_DEPTH|D3D12_DSV_FLAG_READ_ONLY_STENCIL;
    {
        cvr::stereo::probe::Scope internal;
        for(UINT i=0;i<3;++i){const auto* view=i==2?&mipView:&arrayView;
            device->CreateRenderTargetView(owned[i].Get(),view,handles[i]);RtvCreated(owned[i].Get(),view,handles[i]);}
        device->CreateDepthStencilView(owned[3].Get(),&depthView,depthHandle);DsvCreated(owned[3].Get(),&depthView,depthHandle);
    }
    std::array<ComPtr<ID3D12Resource>,9> bound;
    const auto empty=[&](){for(const auto& resource:bound)Require(!resource);};
    const auto reject=[&](UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* rt,BOOL contiguous,const D3D12_CPU_DESCRIPTOR_HANDLE* ds){
        bound[7]=owned[0];Require(!CaptureBoundResources(count,rt,contiguous,ds,bound));empty();
    };
    Require(CaptureBoundResources(3,handles.data(),FALSE,&depthHandle,bound));
    for(UINT i=0;i<3;++i)Require(bound[i]==owned[i]);Require(bound[8]==owned[3]);
    for(UINT i=3;i<8;++i)Require(!bound[i]);
    // Release the caller's reference only after capture; all later lookups have
    // a restored caller owner, and only the retained COM object is used here.
    auto* retained=owned[0].Get();owned[0].Reset();
    Require(bound[0].Get()==retained && bound[0]->GetDesc().DepthOrArraySize==2);owned[0]=bound[0];
    {
        cvr::stereo::probe::Scope internal;
        Require(CaptureBoundResources(3,handles.data(),TRUE,&depthHandle,bound));
    }
    for(UINT i=0;i<3;++i)Require(bound[i]==owned[i]);Require(bound[8]==owned[3]);
    auto ignoredHandles=handles;ignoredHandles[1]={};ignoredHandles[2]={};
    Require(CaptureBoundResources(3,ignoredHandles.data(),TRUE,nullptr,bound) && bound[2]==owned[2]);
    reject(3,ignoredHandles.data(),FALSE,nullptr);
    reject(4,handles.data(),TRUE,nullptr);reject(9,handles.data(),FALSE,nullptr);reject(1,nullptr,FALSE,nullptr);
    reject(1,&depthHandle,FALSE,nullptr);reject(0,nullptr,FALSE,&handles[0]);
    const D3D12_CPU_DESCRIPTOR_HANDLE nullHandle{};reject(0,nullptr,FALSE,&nullHandle);reject(1,&nullHandle,FALSE,nullptr);
    RtvCreated(owned[0].Get(),&arrayView,nullHandle);reject(1,&nullHandle,FALSE,nullptr);RtvCreated(nullptr,nullptr,nullHandle);
    RtvCreated(nullptr,&arrayView,handles[1]);reject(3,handles.data(),TRUE,&depthHandle);
    RtvCreated(owned[1].Get(),&arrayView,handles[1]);
    DsvCreated(owned[3].Get(),&depthView,handles[1]);reject(3,handles.data(),TRUE,&depthHandle);
    RtvCreated(owned[1].Get(),&arrayView,handles[1]);
    DsvCreated(nullptr,&depthView,depthHandle);reject(0,nullptr,FALSE,&depthHandle);DsvCreated(owned[3].Get(),&depthView,depthHandle);
    device->CreateRenderTargetView(owned[1].Get(),&arrayView,handles[0]);RtvCreated(owned[1].Get(),&arrayView,handles[0]);
    Require(CaptureBoundResources(1,handles.data(),FALSE,nullptr,bound) && bound[0]==owned[1]);
    device->CreateRenderTargetView(owned[0].Get(),&arrayView,handles[0]);RtvCreated(owned[0].Get(),&arrayView,handles[0]);
    constexpr GUID identityKey{0x52fbc840,0x27d7,0x46ce,{0x82,0xe5,0x58,0xc5,0x1c,0xe5,0x8f,0x39}};
    uint64_t identity{};UINT bytes=sizeof(identity);Check(owned[0]->GetPrivateData(identityKey,&bytes,&identity));
    const auto changed=identity+100;Check(owned[0]->SetPrivateData(identityKey,sizeof(changed),&changed));
    reject(1,handles.data(),FALSE,nullptr);Check(owned[0]->SetPrivateData(identityKey,sizeof(identity),&identity));
    const D3D12_CPU_DESCRIPTOR_HANDLE overflowing{SIZE_MAX-stride};RtvCreated(owned[0].Get(),&arrayView,overflowing);
    const D3D12_CPU_DESCRIPTOR_HANDLE last{SIZE_MAX};RtvCreated(owned[1].Get(),&arrayView,last);
    reject(3,&overflowing,TRUE,nullptr);RtvCreated(nullptr,nullptr,overflowing);RtvCreated(nullptr,nullptr,last);
    Require(CaptureBoundResources(0,nullptr,FALSE,&depthHandle,bound) && bound[8]==owned[3]);
    Require(CaptureBoundResources(0,nullptr,TRUE,nullptr,bound));empty();
    auto draw=nativeDraw;draw.rtCount=3;draw.rtContiguous=FALSE;
    Targets strict;GeometryTargets geometry;std::array<TargetInfo,9> described;
    Require(CaptureTargets(draw,strict) && CaptureGeometryTargets(draw,geometry) && DescribeTargets(draw,described));
    const auto strictRejected=[&](const auto& candidate){
        Require(!CaptureTargets(candidate,strict) && !CaptureGeometryTargets(candidate,geometry) && !DescribeTargets(candidate,described));
    };
    for(UINT i=0;i<3;++i){auto candidate=draw;candidate.targets[0]=handles[i].ptr;strictRejected(candidate);
        Require(CaptureBoundResources(1,&handles[i],FALSE,nullptr,bound) && bound[0]==owned[i]);}
    auto arrayDepth=draw;arrayDepth.depth=depthHandle.ptr;strictRejected(arrayDepth);
    // A legal array view over one native layer still lacks the whole-view
    // contract required by the original native target capture APIs.
    auto singleLayer=arrayView;singleLayer.Format=nativeColor->GetDesc().Format;singleLayer.Texture2DArray.ArraySize=1;
    device->CreateRenderTargetView(nativeColor,&singleLayer,handles[3]);RtvCreated(nativeColor,&singleLayer,handles[3]);
    auto partialView=draw;partialView.targets[0]=handles[3].ptr;strictRejected(partialView);
    Require(CaptureBoundResources(1,&handles[3],FALSE,nullptr,bound) && bound[0].Get()==nativeColor);
    auto singleDepth=depthView;singleDepth.Flags=D3D12_DSV_FLAG_NONE;singleDepth.Texture2DArray.ArraySize=1;
    const D3D12_CPU_DESCRIPTOR_HANDLE singleDepthHandle{depthHandle.ptr+device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)};
    device->CreateDepthStencilView(nativeDepth,&singleDepth,singleDepthHandle);DsvCreated(nativeDepth,&singleDepth,singleDepthHandle);
    partialView=draw;partialView.depth=singleDepthHandle.ptr;strictRejected(partialView);
    Require(CaptureBoundResources(0,nullptr,FALSE,&singleDepthHandle,bound) && bound[8].Get()==nativeDepth);
    for(const auto handle:handles)RtvCreated(nullptr,nullptr,handle);
    DsvCreated(nullptr,nullptr,depthHandle);DsvCreated(nullptr,nullptr,singleDepthHandle);
}
int main() try {
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(0x1000);auto* root=reinterpret_cast<ID3D12RootSignature*>(0x2000);
    auto* heap=reinterpret_cast<ID3D12DescriptorHeap*>(0x3000);Bindings snapshot;
    D3D12_DESCRIPTOR_RANGE ranges[36]{};D3D12_ROOT_PARAMETER params[36]{};
    for(UINT i=0;i<36;++i){ranges[i]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,i,0,0};params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
    D3D12_ROOT_SIGNATURE_DESC desc{36,params,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));RootCreated(root,blob->GetBufferPointer(),blob->GetBufferSize());
    Reset(list,true);Root(list,root);Topology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);Heaps(list,1,&heap);
    for(UINT i=0;i<32;++i)Table(list,i,{0x4000+i*32});Require(Snapshot(list,snapshot) && snapshot.tableMask==0xffffffff);
    Table(list,36,{0xBAD});Require(!Snapshot(list,snapshot));
    Reset(list,true);Root(list,root);Topology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);Heaps(list,1,&heap);
    for(UINT i=0;i<32;++i)Table(list,i,{0x4000+i*32});
    for(UINT i=32;i<36;++i)Table(list,i,{0x4000+i*32});Require(Snapshot(list,snapshot));Require(snapshot.tables[35].ptr==0x4000+35*32 && snapshot.tableMask==0xfffffffff);
    ComputeRoot(list,root);ComputeTable(list,35,{0xABCD});Require(Snapshot(list,snapshot) && snapshot.computeTables[35].ptr==0xABCD);
    const float blend[]{.2f,.3f,.4f,.5f};BlendFactor(list,blend);Require(Snapshot(list,snapshot) && snapshot.blendFactor==std::array<float,4>{.2f,.3f,.4f,.5f});
    {cvr::stereo::probe::Scope internal;BlendFactor(list,nullptr);}Require(Snapshot(list,snapshot) && snapshot.blendFactor[0]==.2f);
    BlendFactor(list,nullptr);Require(Snapshot(list,snapshot) && snapshot.blendFactor==std::array<float,4>{1,1,1,1});
    const D3D12_SHADING_RATE_COMBINER combiners[]{D3D12_SHADING_RATE_COMBINER_MIN,D3D12_SHADING_RATE_COMBINER_OVERRIDE};
    ShadingRate(list,D3D12_SHADING_RATE_2X2,combiners);ShadingRateImage(list,reinterpret_cast<ID3D12Resource*>(0x7770));
    {cvr::stereo::probe::Scope internal;ShadingRate(list,D3D12_SHADING_RATE_1X1,nullptr);ShadingRateImage(list,nullptr);}
    Require(Snapshot(list,snapshot) && snapshot.rate.known && snapshot.rate.value==D3D12_SHADING_RATE_2X2 && snapshot.rate.combiners[1]==D3D12_SHADING_RATE_COMBINER_OVERRIDE && snapshot.rate.image==reinterpret_cast<ID3D12Resource*>(0x7770));
    {cvr::stereo::probe::Scope internal;Table(list,35,{0xBAD});Stencil(list,255);}
    Require(Snapshot(list,snapshot) && snapshot.tables[35].ptr!=0xBAD && snapshot.stencil==0);
    Heaps(list,1,&heap);Require(Snapshot(list,snapshot) && snapshot.computeTableMask==(uint64_t(1)<<35));heap=reinterpret_cast<ID3D12DescriptorHeap*>(0x5000);Heaps(list,1,&heap);Require(Snapshot(list,snapshot) && snapshot.tableMask==0 && snapshot.computeTableMask==0);
    for(UINT i=0;i<36;++i)Table(list,i,{0x6000+i*32});Stencil(list,255);Require(!Snapshot(list,snapshot));
    Require(CaptureSourceBindings(list,snapshot) && snapshot.stencil==255 && snapshot.tables[35].ptr==0x6000+35*32);
    Stencil(list,7);Require(Snapshot(list,snapshot) && snapshot.stencil==7);
    Require(snapshot.computeTableMask==0);
    Reset(list);Require(!Snapshot(list,snapshot));ResetCapture();Require(!Snapshot(list,snapshot));
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    std::array<ComPtr<ID3D12Resource>,4> resources;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=129;r.Height=97;r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
    cvr::stereo::native_probe::DrawRecord draw;draw.rtCount=3;draw.viewport={0,0,129,97,0,1};draw.depth=0x9000;
    for(UINT i=0;i<4;++i){r.Format=DXGI_FORMAT(i<2?24:i==2?28:20);r.Flags=i==3?D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL:D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&r,i==3?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&resources[i])));
        if(i==3)DsvCreated(resources[i].Get(),nullptr,{draw.depth});else {draw.targets[i]=0x8000+i*32;RtvCreated(resources[i].Get(),nullptr,{draw.targets[i]});}}
    Targets captured;Require(CaptureTargets(draw,captured));for(UINT i=0;i<4;++i)Require(captured.resources[i].Get()==resources[i].Get());
    std::array<TargetInfo,9> described;Require(DescribeTargets(draw,described));
    const auto depthIdentity=described[8].identity;
    Require(depthIdentity && described[0].identity && described[0].identity!=depthIdentity && described[8].viewFormat==20);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;dsv.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;dsv.Flags=D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    DsvCreated(resources[3].Get(),&dsv,{draw.depth});Require(!CaptureTargets(draw,captured));
    Require(DescribeTargets(draw,described) && described[8].identity==depthIdentity && described[8].viewFlags==UINT(dsv.Flags));
    DsvCreated(resources[3].Get(),nullptr,{draw.depth});
    RtvCreated(nullptr,nullptr,{draw.targets[1]});Require(!CaptureTargets(draw,captured));RtvCreated(resources[1].Get(),nullptr,{draw.targets[1]});
    draw.viewport.Width=128;Require(!CaptureTargets(draw,captured));draw.viewport.Width=129;draw.rtContiguous=TRUE;Require(!CaptureTargets(draw,captured));draw.rtContiguous=FALSE;Require(CaptureTargets(draw,captured));
    GeometryTargets geometry;
    Require(CaptureGeometryTargets(draw,geometry) && geometry.colors==3 && geometry.resources[4]==resources[3] && !geometry.resources[3]);
    ComPtr<ID3D12Resource> motion;r.Format=DXGI_FORMAT_R16G16_FLOAT;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&motion)));
    const auto rtStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    draw.rtCount=4;
    for(UINT i=0;i<4;++i){draw.targets[i]=0xA000+UINT64(i)*rtStride;RtvCreated(i==3?motion.Get():resources[i].Get(),nullptr,{draw.targets[i]});}
    Require(CaptureGeometryTargets(draw,geometry) && geometry.colors==4 && geometry.resources[3]==motion);
    for(UINT i=0;i<5;++i)Require(geometry.resources[i] && geometry.views[i].identity && geometry.handles[i].ptr);
    draw.rtContiguous=TRUE;Require(CaptureGeometryTargets(draw,geometry) && geometry.handles[3].ptr==draw.targets[0]+3*rtStride);
    for(UINT flags=0;flags<4;++flags){dsv.Flags=D3D12_DSV_FLAGS(flags);DsvCreated(resources[3].Get(),&dsv,{draw.depth});
        Require(CaptureGeometryTargets(draw,geometry) && geometry.views[4].viewFlags==flags);}
    dsv.Flags=D3D12_DSV_FLAGS(4);DsvCreated(resources[3].Get(),&dsv,{draw.depth});Require(!CaptureGeometryTargets(draw,geometry));
    DsvCreated(resources[3].Get(),nullptr,{draw.depth});
    const auto valid=draw;
    for(float width:{0.f,4097.f,129.5f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        draw.viewport.Width=width;Require(!CaptureGeometryTargets(draw,geometry) && !geometry.resources[4]);}
    draw=valid;draw.viewport.MinDepth=.1f;Require(!CaptureGeometryTargets(draw,geometry));draw=valid;
    for(UINT count:{1u,2u,5u,8u}){draw.rtCount=count;Require(!CaptureGeometryTargets(draw,geometry));}
    draw=valid;draw.rtCount=0;Require(CaptureGeometryTargets(draw,geometry) && geometry.colors==0 && geometry.resources[4]);draw=valid;
    RtvCreated(resources[0].Get(),nullptr,{draw.targets[1]});Require(!CaptureGeometryTargets(draw,geometry));
    RtvCreated(resources[1].Get(),nullptr,{draw.targets[1]});
    RtvCreated(resources[2].Get(),nullptr,{draw.targets[3]});Require(!CaptureGeometryTargets(draw,geometry));
    RtvCreated(motion.Get(),nullptr,{draw.targets[3]});
    constexpr GUID identityKey{0x52fbc840,0x27d7,0x46ce,{0x82,0xe5,0x58,0xc5,0x1c,0xe5,0x8f,0x39}};
    uint64_t cookie{};UINT cookieBytes=sizeof(cookie);Check(motion->GetPrivateData(identityKey,&cookieBytes,&cookie));
    const auto changed=cookie+100;Check(motion->SetPrivateData(identityKey,sizeof(changed),&changed));Require(!CaptureGeometryTargets(draw,geometry));
    Check(motion->SetPrivateData(identityKey,sizeof(cookie),&cookie));Require(CaptureGeometryTargets(draw,geometry));
    draw.targets[0]=UINT64_MAX-rtStride;RtvCreated(resources[0].Get(),nullptr,{draw.targets[0]});Require(!CaptureGeometryTargets(draw,geometry));draw=valid;
    ComPtr<ID3D12Resource> differentSize;r.Width=130;
    Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&differentSize)));
    RtvCreated(differentSize.Get(),nullptr,{draw.targets[3]});Require(!CaptureGeometryTargets(draw,geometry));
    RtvCreated(motion.Get(),nullptr,{draw.targets[3]});Require(CaptureGeometryTargets(draw,geometry));
    BoundTargetResources(device.Get(),draw,resources[0].Get(),resources[3].Get());
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes{};messages->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(messages->GetMessage(i,m,&bytes));
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);Require(false);}}
    std::puts("PASS root bindings, strict MRT ownership, generic internal array/mip targets, contiguous handles, read-only DSV flags, target identity and shape rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
