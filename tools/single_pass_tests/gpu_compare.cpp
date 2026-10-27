#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/StereoTargetArray.hpp"
#include "Render/StereoMeshPlan.hpp"
#include "Render/StereoPairedRoot.hpp"
#include "Render/StereoBindingBank.hpp"
#include "Render/CommandResources.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
static void Check(HRESULT hr,const char* message) {
    if(FAILED(hr)){std::fprintf(stderr,"%s: %08x\n",message,unsigned(hr));throw std::runtime_error(message);}
}
static void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
static void DumpErrors(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> info;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&info))))return;
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};info->GetMessage(i,nullptr,&size);
        std::vector<char> data(size);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&size);
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::fprintf(stderr,"D3D12: %s\n",m->pDescription);}
}
static std::vector<char> Read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);Require(bool(f),"input file missing");
    return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 size,D3D12_HEAP_TYPE type,const void* data=nullptr,
    D3D12_RESOURCE_STATES initial=D3D12_RESOURCE_STATE_GENERIC_READ) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width=size;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,
        type==D3D12_HEAP_TYPE_READBACK?D3D12_RESOURCE_STATE_COPY_DEST:initial,nullptr,IID_PPV_ARGS(&r)),"CreateBuffer");
    if(data){void* p{};D3D12_RANGE empty{};Check(r->Map(0,&empty,&p),"Map upload");std::memcpy(p,data,size);r->Unmap(0,nullptr);}
    return r;
}
int wmain(int argc,wchar_t** argv) try {
    Require(argc>=2,"gpu_compare <fixture directory> [scale [clip-x-offset [roll-degrees [instances [clip-eye [view-mask]]]]]]");fs::path dir=argv[1];
    const float fixtureScale=argc>2?float(_wtof(argv[2])):1.0f;
    const float shift=argc>3?float(_wtof(argv[3])):0.0f;
    const float roll=argc>4?float(_wtof(argv[4]))*0.01745329252f:0.0f;
    const UINT fixtureInstances=argc>5?UINT(_wtoi(argv[5])):0;
    const int clipEye=argc>6?_wtoi(argv[6]):-1;
    const UINT viewMask=argc>7?UINT(_wtoi(argv[7])):3;
    const bool pairedResources=argc>8 && std::wstring(argv[8])==L"resource-pairs";
    const bool pairedPixel=pairedResources && argc>=12;
    bool swapPairedSrvs=false,reverseWinding=false,ownedBank=false,aliasInputs=false;
    for(int i=12;i<argc;++i) {
        const std::wstring option=argv[i];
        if(option==L"swap-srvs" && !swapPairedSrvs)swapPairedSrvs=true;
        else if(option==L"reverse-winding" && !reverseWinding)reverseWinding=true;
        else if(option==L"owned-bank" && !ownedBank)ownedBank=true;
        else if(option==L"alias-inputs" && !aliasInputs)aliasInputs=true;
        else Require(false,"unexpected pixel fixture option");
    }
    const bool meshPass=pairedResources || (argc>8 && std::wstring(argv[8])==L"mesh-pass");
    Require((pairedResources && (argc==10 || pairedPixel)) || (!pairedResources && argc<=9 && (argc<9 || meshPass)),"unexpected argument");
    Require(!meshPass || (fixtureInstances==5 && viewMask==3),"mesh pass requires five instances and both eyes");
    Require(!aliasInputs || ownedBank,"aliased inputs require the owned bank");
    // Native order differs and contains duplicates. The shared plan must keep
    // each eye's exact subsequence without recording reference/fallback draws
    // into the optimized command stream or waiting for the other eye on GPU.
    const std::array<std::vector<uint64_t>,2> meshIds{{{1,2,3,2,4},{1,3,2,4,5}}};
    cvr::stereo::MeshPlan meshPlan;
    Require(!meshPass || cvr::stereo::BuildStereoMeshPlan(meshIds[0],meshIds[1],meshPlan),"mesh plan failed");
    Require(viewMask<=3,"invalid eye mask");
    Require(fixtureScale>0 && fixtureScale<=64 && std::abs(shift)<1 && std::abs(roll)<1 && fixtureInstances<=8,"invalid fixture parameters");
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");
    ComPtr<IDXGIAdapter1> adapter;Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"Device2");
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&options,sizeof(options)),"Options3");
    Require(options.ViewInstancingTier!=D3D12_VIEW_INSTANCING_TIER_NOT_SUPPORTED,"View Instancing unsupported");
    std::ifstream scene(dir/L"scene.txt");Require(bool(scene),"scene manifest missing");
    UINT indices{},instances{},firstInstance{},topology{},indexStride{};INT baseVertex{};
    scene>>indices>>instances>>baseVertex>>firstInstance>>topology>>indexStride;
    const UINT originalInstances=instances;
    if(fixtureInstances){Require(instances==1,"instance expansion requires a single captured instance");instances=fixtureInstances;}
    UINT count{};scene>>count;Require(count>0 && count<32,"invalid input layout");
    std::vector<std::string> names(count);std::vector<D3D12_INPUT_ELEMENT_DESC> layout(count);
    for(UINT i=0;i<count;++i){UINT fmt{},rate{};scene>>names[i]>>layout[i].SemanticIndex>>fmt>>layout[i].InputSlot>>layout[i].AlignedByteOffset>>rate>>layout[i].InstanceDataStepRate;
        layout[i].SemanticName=names[i].c_str();layout[i].Format=DXGI_FORMAT(fmt);layout[i].InputSlotClass=rate?D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA:D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;}
    std::vector<ComPtr<ID3D12Resource>> keep;
    std::array<D3D12_VERTEX_BUFFER_VIEW,32> vertexViews{};UINT vbCount{};
    scene>>count;Require(count>0 && count<32,"invalid vertex buffers");
    for(UINT i=0;i<count;++i){UINT slot{},stride{};std::string name;scene>>slot>>stride>>name;Require(slot<32,"invalid vertex slot");
        auto raw=Read(dir/name);
        const bool transform=std::any_of(layout.begin(),layout.end(),[&](const auto& e){return e.InputSlot==slot && std::string(e.SemanticName)=="INSTANCE_TRANSFORM";});
        if(transform){Require(stride==48 && raw.size()>=(firstInstance+originalInstances)*stride,"unexpected transform stream");
            const auto begin=size_t(firstInstance)*stride;std::array<char,48> initial{};std::memcpy(initial.data(),raw.data()+begin,48);
            raw.resize(std::max(raw.size(),size_t(firstInstance+instances)*stride));
            for(UINT n=0;n<instances;++n){auto* dst=raw.data()+(firstInstance+n)*stride;if(n>=originalInstances)std::memcpy(dst,initial.data(),48);
                auto* matrix=reinterpret_cast<float*>(dst);for(UINT row=0;row<3;++row)for(UINT col=0;col<3;++col)matrix[row*4+col]*=fixtureScale;
                if(n>=originalInstances){INT x{};std::memcpy(&x,dst+12,4);x+=INT(n*65536);std::memcpy(dst+12,&x,4);}}}
        auto r=Buffer(device.Get(),raw.size(),D3D12_HEAP_TYPE_UPLOAD,raw.data());
        vertexViews[slot]={r->GetGPUVirtualAddress(),UINT(raw.size()),stride};vbCount=std::max(vbCount,slot+1);keep.push_back(r);}
    Require(bool(scene),"invalid scene manifest");
    auto ib=Read(dir/L"scene-index.bin");auto indexBuffer=Buffer(device.Get(),ib.size(),D3D12_HEAP_TYPE_UPLOAD,ib.data());
    D3D12_INDEX_BUFFER_VIEW indexView{indexBuffer->GetGPUVirtualAddress(),UINT(ib.size()),indexStride==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT};
    std::array<char,2048> cameras{};auto camera0=Read(dir/L"14824-Vertex-cb0.bin"),camera1=Read(dir/L"38430-Vertex-cb0.bin");
    Require(camera0.size()>=848 && camera1.size()>=848,"camera data missing");
    std::memcpy(cameras.data(),camera0.data(),848);std::memcpy(cameras.data()+1024,camera1.data(),848);
    // Deliberately asymmetric clip-space transforms stress independent camera
    // selection. Both reference draws use exactly these same camera blocks.
    for(UINT eye=0;eye<2;++eye){auto* values=reinterpret_cast<float*>(cameras.data()+eye*1024);const float angle=eye?roll:-roll;
        for(UINT row=28;row<32;++row){auto* v=values+row*4;const float x=v[0],y=v[1];
            v[0]=std::cos(angle)*x-std::sin(angle)*y+(eye?shift:-shift)*v[3];v[1]=std::sin(angle)*x+std::cos(angle)*y;}
        if(clipEye==int(eye)){values[200]=values[201]=values[202]=0;values[203]=-1;}}
    auto cameraBuffer=Buffer(device.Get(),cameras.size(),D3D12_HEAP_TYPE_UPLOAD,cameras.data());
    auto frequent=Read(dir/L"38430-Vertex-cb1.bin");frequent.resize(256);
    if(pairedResources){frequent.resize(512);std::memcpy(frequent.data()+256,frequent.data(),256);
        reinterpret_cast<float*>(frequent.data())[16]*=.985f;reinterpret_cast<float*>(frequent.data()+256)[16]*=1.015f;}
    auto frequentBuffer=Buffer(device.Get(),frequent.size(),D3D12_HEAP_TYPE_UPLOAD,frequent.data());
    std::array<ComPtr<ID3D12Resource>,2> inputBuffers,inputTextures,inputUploads;
    if(pairedPixel)for(UINT eye=0;eye<2;++eye) {
        const float data[]{eye?.65f:.15f,0,0,1};inputUploads[eye]=Buffer(device.Get(),sizeof(data),D3D12_HEAP_TYPE_UPLOAD,data);
        if(aliasInputs && eye){inputBuffers[eye]=inputBuffers[0];inputTextures[eye]=inputTextures[0];continue;}
        inputBuffers[eye]=Buffer(device.Get(),sizeof(data),D3D12_HEAP_TYPE_DEFAULT,nullptr,D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=texture.Height=2;
        texture.DepthOrArraySize=texture.MipLevels=1;texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&inputTextures[eye])),"Per-eye texture");
    }
    D3D12_ROOT_PARAMETER parameters[4]{};
    D3D12_DESCRIPTOR_RANGE pixelRange{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0};
    for(UINT i=0;i<2;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;parameters[i].Descriptor.ShaderRegister=i?5:1;parameters[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;}
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=2;rootDesc.pParameters=parameters;rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if(pairedPixel){parameters[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[2].DescriptorTable={1,&pixelRange};parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;rootDesc.NumParameters=3;}
    ComPtr<ID3DBlob> signature,error;Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"SerializeRootSignature");
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)),"RootSignature");
    ComPtr<ID3D12RootSignature> pairedRoot;ComPtr<ID3D12DescriptorHeap> pairedHeap;
    cvr::stereo::PairedRootPlan pairedPlan;
    if(pairedResources) {
        std::vector<cvr::stereo::PairedBinding> bindings{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100},
            {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,5,100}};
        if(pairedPixel)for(UINT reg=0;reg<2;++reg)bindings.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,reg,101});
        Check(cvr::stereo::BuildPairedRoot(signature->GetBufferPointer(),signature->GetBufferSize(),bindings,pairedPlan),"Build paired root");
        Check(device->CreateRootSignature(0,pairedPlan.serialized.data(),pairedPlan.serialized.size(),IID_PPV_ARGS(&pairedRoot)),"Paired root");
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,pairedPixel?12u:4u,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        Check(device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(&pairedHeap)),"Paired CBV heap");
        auto cpu=pairedHeap->GetCPUDescriptorHandleForHeapStart();const auto stride=device->GetDescriptorHandleIncrementSize(heapDesc.Type);
        for(UINT i=0;i<4;++i){D3D12_CONSTANT_BUFFER_VIEW_DESC cb{};
            cb.BufferLocation=i<2?cameraBuffer->GetGPUVirtualAddress()+i*1024:frequentBuffer->GetGPUVirtualAddress()+(i-2)*256;
            cb.SizeInBytes=i<2?1024:256;device->CreateConstantBufferView(&cb,cpu);cpu.ptr+=stride;}
        if(pairedPixel)for(UINT slot=4;slot<12;++slot) {
            const bool texture=slot<8?slot>=6:(slot&1)!=0;
            UINT eye=slot<8?(slot&1):(slot-8)/2;if(slot<8 && swapPairedSrvs)eye^=1;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            if(texture){view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;}
            else {view.Format=DXGI_FORMAT_UNKNOWN;view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=1;view.Buffer.StructureByteStride=16;}
            device->CreateShaderResourceView(texture?inputTextures[eye].Get():inputBuffers[eye].Get(),&view,cpu);cpu.ptr+=stride;
        }
    }
    auto bank=std::make_unique<cvr::stereo::StereoBindingBank>();
    if(ownedBank) {
        std::vector<cvr::stereo::BankPair> pairs;
        for(const auto& binding:pairedPlan.bindings) {
            cvr::stereo::BankPair pair;pair.binding=binding.binding;
            for(UINT eye=0;eye<2;++eye) {
                auto& value=pair.eyes[eye];
                if(pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV) {
                    value.constants=pair.binding.sourceRegister==1?
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(cameras.data()+eye*1024),848):
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(frequent.data()+eye*256),112);
                } else {
                    const bool texture=pair.binding.sourceRegister==1;
                    value.source=texture?inputTextures[eye]:inputBuffers[eye];value.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                    value.srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    if(texture){value.srv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;value.srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;value.srv.Texture2D.MipLevels=1;}
                    else{value.srv.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;value.srv.Buffer.NumElements=1;value.srv.Buffer.StructureByteStride=16;}
                }
            }
            if(swapPairedSrvs && pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV)std::swap(pair.eyes[0],pair.eyes[1]);
            pairs.push_back(std::move(pair));
        }
        Check(bank->Create(device.Get(),pairedRoot.Get(),pairedPlan,pairs),"Own paired binding bank");
        Require(bank->DescriptorCount()==8 && bank->SnapshotCount()==4 && bank->ConstantBytes()==2560,"Unexpected owned bank shape");
        // Bank CBVs no longer borrow these CPU arrays. Native reference buffers
        // already contain their original contents and are never changed.
        std::fill(cameras.begin(),cameras.end(),char(0xCD));std::fill(frequent.begin(),frequent.end(),char(0xCD));
    }
    auto original=Read(dir/L"38430-Vertex.dxil"),patched=Read(dir/L"simple-viewid.dxil"),pixel=Read(dir/L"probe-ps.dxil");
    if(pairedResources)patched=Read(argv[9]);
    std::vector<char> pairedPixelShader;if(pairedPixel){pixel=Read(argv[10]);pairedPixelShader=Read(argv[11]);}
    std::array<ComPtr<ID3D12PipelineState>,2> pipelines;
    ComPtr<ID3DBlob> monoCache;
    for(UINT mode=0;mode<2;++mode){D3D12_GRAPHICS_PIPELINE_STATE_DESC s{};s.pRootSignature=mode && pairedResources?pairedRoot.Get():root.Get();auto& vs=mode?patched:original;
        auto& ps=mode && pairedPixel?pairedPixelShader:pixel;
        s.VS={vs.data(),vs.size()};s.PS={ps.data(),ps.size()};s.InputLayout={layout.data(),UINT(layout.size())};
        s.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;s.NumRenderTargets=1;s.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;
        s.DSVFormat=DXGI_FORMAT_D32_FLOAT;s.SampleDesc={1,0};s.SampleMask=UINT_MAX;
        auto& blend=s.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        s.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;s.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;s.RasterizerState.DepthClipEnable=TRUE;
        s.RasterizerState.FrontCounterClockwise=reverseWinding;
        s.DepthStencilState.DepthEnable=TRUE;s.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;s.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        s.DepthStencilState.FrontFace=s.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        if(mode && monoCache)s.CachedPSO={monoCache->GetBufferPointer(),monoCache->GetBufferSize()};
        cvr::stereo::ViewInstancedLayout viewLayout;viewLayout.allowMasking=true;
        const auto hr=mode?cvr::stereo::CreateViewInstancedPipeline(device.Get(),s,viewLayout,&pipelines[mode]):device->CreateGraphicsPipelineState(&s,IID_PPV_ARGS(&pipelines[mode]));
        if(FAILED(hr)){std::fprintf(stderr,"PSO mode %u\n",mode);DumpErrors(device.Get());}Check(hr,"Create view PSO");
        if(!mode)Check(pipelines[0]->GetCachedBlob(&monoCache),"Get monoscopic PSO cache");}
    constexpr UINT resolution=512;
    D3D12_RESOURCE_DESC imageDesc{};imageDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    imageDesc.Width=imageDesc.Height=resolution;imageDesc.DepthOrArraySize=2;imageDesc.MipLevels=1;imageDesc.SampleDesc.Count=1;
    imageDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;imageDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    std::array<ComPtr<ID3D12Resource>,2> targets,depths,readbacks;
    cvr::stereo::StereoTargetArray sharedColor;
    ComPtr<ID3D12Resource> nativeColor;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]{},depthFootprints[2]{};UINT64 total{},depthBytes{};
    device->GetCopyableFootprints(&imageDesc,0,2,0,footprints,nullptr,nullptr,&total);
    auto depthLayout=imageDesc;depthLayout.Format=DXGI_FORMAT_D32_FLOAT;depthLayout.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const auto depthOffset=(total+511)&~UINT64(511);
    device->GetCopyableFootprints(&depthLayout,0,2,depthOffset,depthFootprints,nullptr,nullptr,&depthBytes);
    total=depthOffset+depthBytes;
    ComPtr<ID3D12DescriptorHeap> rtv,dsv;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=pairedPixel?8:6;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtv)),"RTV heap");hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)),"DSV heap");
    auto rh=[&](UINT i){auto h=rtv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);return h;};
    auto dh=[&](UINT i){auto h=dsv->GetCPUDescriptorHandleForHeapStart();h.ptr+=i*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);return h;};
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    for(UINT mode=0;mode<2;++mode){
        if(mode){auto native=imageDesc;native.DepthOrArraySize=1;
            Check(sharedColor.Initialize(device.Get(),native,D3D12_RESOURCE_STATE_RENDER_TARGET),"Common geometry output");
            targets[mode]=sharedColor.Resource();
            Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&native,D3D12_RESOURCE_STATE_COPY_SOURCE,nullptr,IID_PPV_ARGS(&nativeColor)),"Native 2D pass input");
        }else Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&imageDesc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[mode])),"Color array");
        auto dd=imageDesc;dd.Format=DXGI_FORMAT_D32_FLOAT;dd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&dd,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depths[mode])),"Depth array");
        readbacks[mode]=Buffer(device.Get(),total,D3D12_HEAP_TYPE_READBACK);
        for(UINT v=0;v<3;++v){D3D12_RENDER_TARGET_VIEW_DESC rd{};rd.Format=imageDesc.Format;rd.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rd.Texture2DArray.FirstArraySlice=v<2?v:0;rd.Texture2DArray.ArraySize=v<2?1:2;device->CreateRenderTargetView(targets[mode].Get(),&rd,rh(mode*3+v));
            D3D12_DEPTH_STENCIL_VIEW_DESC sd{};sd.Format=dd.Format;sd.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            sd.Texture2DArray.FirstArraySlice=rd.Texture2DArray.FirstArraySlice;sd.Texture2DArray.ArraySize=rd.Texture2DArray.ArraySize;device->CreateDepthStencilView(depths[mode].Get(),&sd,dh(mode*3+v));}}
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC qd{};Check(device->CreateCommandQueue(&qd,IID_PPV_ARGS(&queue)),"Queue");
    ComPtr<ID3D12CommandAllocator> allocator;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
    ComPtr<ID3D12GraphicsCommandList> commands;Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"CommandList");
    ComPtr<ID3D12GraphicsCommandList1> viewCommands;Check(commands.As(&viewCommands),"View masking commands");
    commands->SetGraphicsRootSignature(root.Get());commands->IASetVertexBuffers(0,vbCount,vertexViews.data());commands->IASetIndexBuffer(&indexView);commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY(topology));
    commands->SetGraphicsRootConstantBufferView(1,frequentBuffer->GetGPUVirtualAddress());
    D3D12_VIEWPORT vp{0,0,float(resolution),float(resolution),0,1};D3D12_RECT sc{0,0,resolution,resolution};commands->RSSetViewports(1,&vp);commands->RSSetScissorRects(1,&sc);
    const float clear[4]{};
    if(pairedPixel)for(UINT eye=0;eye<2;++eye){D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(inputTextures[eye].Get(),&view,rh(6+eye));}
    const float poisonData[]{9,0,0,1};auto poisonUpload=Buffer(device.Get(),sizeof(poisonData),D3D12_HEAP_TYPE_UPLOAD,poisonData);
    std::array<bool,2> inputInitialized{};
    auto writeInputs=[&](UINT eye,bool poison) {
        const UINT storage=aliasInputs?0:eye;D3D12_RESOURCE_BARRIER barriers[2]{};
        for(auto& b:barriers)b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        if(inputInitialized[storage]) {
            barriers[0].Transition={inputTextures[eye].Get(),0,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET};
            barriers[1].Transition={inputBuffers[eye].Get(),0,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};commands->ResourceBarrier(2,barriers);
        }
        const float color[]{0,poison?9.f:eye?.2f:.1f,0,1};commands->ClearRenderTargetView(rh(6+eye),color,0,nullptr);
        commands->CopyBufferRegion(inputBuffers[eye].Get(),0,poison?poisonUpload.Get():inputUploads[eye].Get(),0,16);
        barriers[0].Transition={inputTextures[eye].Get(),0,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
        barriers[1].Transition={inputBuffers[eye].Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};commands->ResourceBarrier(2,barriers);
        inputInitialized[storage]=true;
    };
    if(pairedPixel && !aliasInputs)for(UINT eye=0;eye<2;++eye)writeInputs(eye,false);
    for(UINT mode=0;mode<2;++mode){commands->SetPipelineState(pipelines[mode].Get());
        commands->SetGraphicsRootSignature(mode && pairedResources?pairedRoot.Get():root.Get());
        if(mode && ownedBank) {
            for(UINT eye=0;eye<(aliasInputs?1u:2u);++eye)writeInputs(eye,true);
            Require(bank->Bind(commands.Get()),"Bind captured bank");bank.reset(); // Recording/flight must own the private heap and snapshots now.
        } else if((mode && pairedResources)||pairedPixel){auto* heap=pairedHeap.Get();commands->SetDescriptorHeaps(1,&heap);
            if(mode && pairedResources)commands->SetGraphicsRootDescriptorTable(pairedPlan.parameters[0],heap->GetGPUDescriptorHandleForHeapStart());}
        commands->ClearRenderTargetView(rh(mode*3+2),clear,0,nullptr);commands->ClearDepthStencilView(dh(mode*3+2),D3D12_CLEAR_FLAG_DEPTH,0,0,0,nullptr);
        if(mode)viewCommands->SetViewInstanceMask(viewMask);
        for(UINT eye=0;eye<(mode?1u:2u);++eye){auto color=rh(mode*3+(mode?2:eye)),depth=dh(mode*3+(mode?2:eye));
            if(!mode && !(viewMask&(1u<<eye)))continue;
            if(!mode && pairedPixel) {
                if(aliasInputs)writeInputs(eye,false);
                if(ownedBank)Require(bank->CaptureEye(commands.Get(),aliasInputs && swapPairedSrvs?eye^1:eye),"Capture producer eye before reuse");
            }
            commands->OMSetRenderTargets(1,&color,FALSE,&depth);commands->SetGraphicsRootConstantBufferView(0,cameraBuffer->GetGPUVirtualAddress()+eye*1024);
            commands->SetGraphicsRootConstantBufferView(1,frequentBuffer->GetGPUVirtualAddress()+(pairedResources?eye*256:0));
            if(pairedPixel && !(mode && ownedBank)){auto table=pairedHeap->GetGPUDescriptorHandleForHeapStart();table.ptr+=uint64_t(mode?4:8+eye*2)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);commands->SetGraphicsRootDescriptorTable(mode?pairedPlan.parameters[1]:2,table);}
            if(!meshPass)commands->DrawIndexedInstanced(indices,instances,0,baseVertex,firstInstance);
            else if(!mode) {
                for(auto id:meshIds[eye])commands->DrawIndexedInstanced(indices,1,0,baseVertex,firstInstance+UINT(id-1));
            } else {
                UINT currentMask=UINT_MAX;
                for(const auto& draw:meshPlan.draws) {
                    const UINT from=draw.mask&1?0:1;const auto id=meshIds[from][draw.source[from]];
                    if(currentMask!=draw.mask){viewCommands->SetViewInstanceMask(draw.mask);currentMask=draw.mask;}
                    commands->DrawIndexedInstanced(indices,1,0,baseVertex,firstInstance+UINT(id-1));
                }
            }}
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={targets[mode].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        for(UINT eye=0;eye<2;++eye){D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readbacks[mode].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[eye];
            D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=targets[mode].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=eye;
            if(mode){Check(sharedColor.CopyEye(commands.Get(),eye,D3D12_RESOURCE_STATE_COPY_SOURCE,nativeColor.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE),"Route common geometry to native eye input");
                src.pResource=nativeColor.Get();src.SubresourceIndex=0;}
            commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
        barrier.Transition={depths[mode].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
        for(UINT eye=0;eye<2;++eye){D3D12_TEXTURE_COPY_LOCATION dst{},src{};
            dst.pResource=readbacks[mode].Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=depthFootprints[eye];
            src.pResource=depths[mode].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=eye;
            commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}}
    Check(commands->Close(),"Close");ID3D12CommandList* lists[]={commands.Get()};
    ComPtr<ID3D12Fence> gate;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"Lifetime gate");
    if(ownedBank)Check(queue->Wait(gate.Get(),1),"Block GPU before releasing bank owner");
    auto submission=cvr::gpu::PrepareCommandResources(1,lists);queue->ExecuteCommandLists(1,lists);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(submission));
    ComPtr<ID3D12CommandAllocator> nextAllocator;
    if(ownedBank) {
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&nextAllocator)),"Next recording allocator");
        Check(commands->Reset(nextAllocator.Get(),nullptr),"Reset list while old submission is blocked");cvr::gpu::ResetCommandResources(commands.Get());
        Check(commands->Close(),"Close unused next recording");Check(gate->Signal(1),"Release GPU lifetime gate");
    }
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");Check(queue->Signal(fence.Get(),1),"Signal");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(fence->SetEventOnCompletion(1,event),"Fence completion");auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU timeout");
    cvr::gpu::CollectCommandResources();
    std::array<char*,2> mapped{};D3D12_RANGE range{0,SIZE_T(total)};
    for(UINT i=0;i<2;++i)Check(readbacks[i]->Map(0,&range,reinterpret_cast<void**>(&mapped[i])),"Readback map");
    UINT64 covered[2]{},different{},differentDepth{},referenceHash=14695981039346656037ull;double maxError{};
    for(UINT eye=0;eye<2;++eye)for(UINT y=0;y<resolution;++y)for(UINT x=0;x<resolution;++x){const auto at=footprints[eye].Offset+y*footprints[eye].Footprint.RowPitch+x*16;
        auto* a=reinterpret_cast<float*>(mapped[0]+at);auto* b=reinterpret_cast<float*>(mapped[1]+at);covered[eye]+=a[3]!=0;
        uint32_t blue{};std::memcpy(&blue,a+2,4);referenceHash=(referenceHash^blue)*1099511628211ull;
        bool mismatch=false;for(unsigned c=0;c<4;++c){Require(std::isfinite(a[c])&&std::isfinite(b[c]),"nonfinite output");double error=std::abs(double(a[c])-b[c]);maxError=std::max(maxError,error);mismatch|=error>1e-6;}different+=mismatch;}
    for(UINT eye=0;eye<2;++eye)for(UINT y=0;y<resolution;++y)for(UINT x=0;x<resolution;++x){
        const auto at=depthFootprints[eye].Offset+y*depthFootprints[eye].Footprint.RowPitch+x*4;
        differentDepth+=std::memcmp(mapped[0]+at,mapped[1]+at,4)!=0;}
    D3D12_RANGE none{};for(UINT i=0;i<2;++i)readbacks[i]->Unmap(0,&none);
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info)))for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes{};info->GetMessage(i,nullptr,&bytes);std::vector<char> data(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&bytes);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);throw std::runtime_error("D3D12 validation error");}}
    std::printf("{\"covered\":[%llu,%llu],\"differentPixels\":%llu,\"differentDepthPixels\":%llu,\"maxChannelError\":%.12g,\"instances\":%u,\"scale\":%.2f,\"shift\":%.3f,\"baselineDraws\":%u,\"instancedDraws\":%zu,\"viewMask\":%u,\"meshPass\":%s,\"native2dRouting\":true,\"referenceColorHash\":%llu}\n",covered[0],covered[1],different,differentDepth,maxError,instances,fixtureScale,shift,meshPass?10:(viewMask&1)+((viewMask>>1)&1),meshPass?meshPlan.draws.size():size_t(1),viewMask,meshPass?"true":"false",referenceHash);
    for(UINT eye=0;eye<2;++eye)if(!(viewMask&(1u<<eye)) || clipEye==int(eye))Require(covered[eye]==0,"masked eye contains geometry");
    if(viewMask && (clipEye<0 || (viewMask&(1u<<(clipEye^1)))))Require(covered[0]+covered[1]>0,"both eligible fixture views are empty");
    Require(different==0 && differentDepth==0,"view instancing differs from two ordinary draws");return 0;
} catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
