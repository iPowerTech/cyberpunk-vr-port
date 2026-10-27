#include "Render/StereoGeometryPass.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/CommandResources.hpp"
#include "Render/StereoDepthComparison.hpp"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <cstdio>
#include <stdexcept>
#include <source_location>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
static ID3D12InfoQueue* messages{};
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value,std::source_location at=std::source_location::current()){if(FAILED(value)){
    std::fprintf(stderr,"HRESULT %08X at %s:%u\n",UINT(value),at.file_name(),at.line());
    if(messages)for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T n{};messages->GetMessage(i,nullptr,&n);std::vector<uint8_t> data(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());if(SUCCEEDED(messages->GetMessage(i,message,&n)))std::fprintf(stderr,"%s\n",message->pDescription);}
    throw std::runtime_error("D3D12 setup failed");}}
static std::vector<uint8_t> Read(const char* path){std::ifstream file(path,std::ios::binary);Require(bool(file),"Shader missing");return {std::istreambuf_iterator<char>(file),{}};}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,D3D12_HEAP_TYPE type,UINT64 bytes,const void* data=nullptr) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=bytes;d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> r;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&r)));
    if(data){void* p{};Check(r->Map(0,nullptr,&p));std::memcpy(p,data,SIZE_T(bytes));r->Unmap(0,nullptr);}return r;
}
static geometry::BufferLease Lease(ID3D12Resource* r){const auto gpu=r->GetGPUVirtualAddress(),bytes=r->GetDesc().Width;return geometry::AcquireBuffer({reinterpret_cast<uintptr_t>(r),gpu,bytes},gpu,bytes);}
struct SourceTargets {
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> colors{},depths{};
    std::vector<UINT> eyes;
    bool depthOnly{};UINT fail=UINT_MAX;
};
static bool BindSourceTargets(void* user,ID3D12GraphicsCommandList* list,UINT source){
    auto& targets=*static_cast<SourceTargets*>(user);if(source==targets.fail || source>=targets.eyes.size())return false;
    const auto eye=targets.eyes[source];list->OMSetRenderTargets(targets.depthOnly?0:1,targets.depthOnly?nullptr:&targets.colors[eye],FALSE,&targets.depths[eye]);return true;
}
int main(int argc,char** argv) try {
    Require(argc==4 || argc==5,"Expected original VS, paired VS, PS [depth-only]");const bool depthOnly=argc==5;
    auto vs=Read(argv[1]),paired=Read(argv[2]),ps=Read(argv[3]);
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&options,sizeof(options)));
    if(!options.ViewInstancingTier){std::puts("SKIP hardware has no View Instancing");return 77;}
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));messages=info.Get();
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,3,0,0,0};D3D12_ROOT_PARAMETER param{};
    param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;param.DescriptorTable={1,&range};param.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC rd{1,&param,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> bytes,error;
    Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto variant=std::make_shared<families::RootVariant>();const PairedBinding binding{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100};
    Check(BuildPairedRoot(bytes->GetBufferPointer(),bytes->GetBufferSize(),{&binding,1},variant->plan));
    Check(device->CreateRootSignature(0,variant->plan.serialized.data(),variant->plan.serialized.size(),IID_PPV_ARGS(&variant->root)));
    D3D12_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs.data(),vs.size()};pd.PS={ps.data(),ps.size()};pd.InputLayout={elements,2};
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.SampleMask=UINT_MAX;pd.SampleDesc.Count=1;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    if(depthOnly){pd.PS={};pd.NumRenderTargets=0;pd.RTVFormats[0]=DXGI_FORMAT_UNKNOWN;}
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pd.BlendState.RenderTarget[0];blend.BlendEnable=TRUE;blend.RenderTargetWriteMask=15;blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_BLEND_FACTOR;
    blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_INV_BLEND_FACTOR;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;
    auto& ds=pd.DepthStencilState;ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    ds.StencilEnable=TRUE;ds.StencilReadMask=ds.StencilWriteMask=255;ds.FrontFace=ds.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> original;Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&original)));
    auto family=std::make_shared<families::Family>();family->sourceRoot=root;family->pairedRoot=variant;family->instanceRebaseSafe=true;
    family->uses.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,0,1,1,16});
    family->pass.targets.NumRenderTargets=pd.NumRenderTargets;family->pass.targets.RTFormats[0]=pd.RTVFormats[0];family->pass.depth=pd.DSVFormat;family->pass.samples=pd.SampleDesc;family->pass.pixelShader=!depthOnly;
    family->pass.topology=pd.PrimitiveTopologyType;family->pass.fixedRasterState=true;
    family->pass.depthState=pd.DepthStencilState;family->pass.depthStateKnown=true;
    pd.pRootSignature=variant->root.Get();pd.VS={paired.data(),paired.size()};ViewInstancedLayout layout;layout.allowMasking=true;
    Check(CreateViewInstancedPipeline(device.Get(),pd,layout,&family->pipeline));
    const float vertexBytes[]{-1,-1,0,1,1,-1};const uint16_t indexBytes[]{0,1,2};
    auto vertices=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,sizeof(vertexBytes),vertexBytes),indices=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,sizeof(indexBytes),indexBytes);
    std::array<float,24> instances{};instances[0]=-.4f;instances[2]=.25f;instances[12]=.4f;instances[14]=.8f;
    std::array<GeometryPassDraw,3> draws;std::array<BankPair,3> constants;std::array<std::array<float,4>,6> configs;
    std::array<uint8_t,6*256> nativeConstants{};std::array<ComPtr<ID3D12Resource>,2> originalInstances;
    for(UINT eye=0;eye<2;++eye){const UINT start=eye?9:3;std::vector<uint8_t> data((start+2)*48);std::memcpy(data.data()+start*48,instances.data(),sizeof(instances));originalInstances[eye]=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,data.size(),data.data());}
    for(UINT i=0;i<3;++i) {
        auto& input=draws[i];input.family=family;input.viewMask=i==0?3:i==1?1:2;input.geometry.instanceRebaseSafe=true;constants[i].binding=binding;input.constants={&constants[i],1};
        for(UINT eye=0;eye<2;++eye) {
            auto& config=configs[2*i+eye];config={eye?.08f:-.08f,i==1?.25f:i==2?-.25f:0,.2f+.2f*i,.7f-.2f*i};std::memcpy(nativeConstants.data()+(2*i+eye)*256,config.data(),16);
            constants[i].eyes[eye].constants={reinterpret_cast<const uint8_t*>(config.data()),16};auto& source=input.geometry.eyes[eye];auto& d=source.draw;
            source.index=Lease(indices.Get());source.vertices[0]=Lease(vertices.Get());source.vertices[7]=Lease(originalInstances[eye].Get());
            source.instances.assign(reinterpret_cast<const uint8_t*>(instances.data()),reinterpret_cast<const uint8_t*>(instances.data())+sizeof(instances));
            d.frame=1;d.side=1-eye;d.pipeline=reinterpret_cast<uintptr_t>(original.Get());d.root=reinterpret_cast<uintptr_t>(root.Get());d.indices=3;d.instances=2;d.firstInstance=eye?9:3;d.flags=191;d.inputSlotMask=129;d.rtCount=pd.NumRenderTargets;
            d.index={indices->GetGPUVirtualAddress(),sizeof(indexBytes),DXGI_FORMAT_R16_UINT};d.vertices[0]={vertices->GetGPUVirtualAddress(),sizeof(vertexBytes),8};d.vertices[7]={originalInstances[eye]->GetGPUVirtualAddress(),UINT(originalInstances[eye]->GetDesc().Width),48};
            d.viewport={0,0,128,64,0,1};d.scissor={LONG(i*2),0,128,64};source.state.original=original;source.state.topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            source.state.stencil=11+i;source.state.blendFactor={.3f+.2f*i,.7f,.6f,1};source.state.rate.known=true;
        }
    }
    auto cb=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,nativeConstants.size(),nativeConstants.data());ComPtr<ID3D12DescriptorHeap> cbHeap,rtvs,dsvs;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,18,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&cbHeap)));
    const auto cbStride=device->GetDescriptorHandleIncrementSize(hd.Type);auto cbCpu=cbHeap->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<6;++i){D3D12_CONSTANT_BUFFER_VIEW_DESC v{cb->GetGPUVirtualAddress()+256*i,256};
        for(UINT slot=0;slot<3;++slot){device->CreateConstantBufferView(slot==1?&v:nullptr,cbCpu);cbCpu.ptr+=cbStride;}}
    hd={D3D12_DESCRIPTOR_HEAP_TYPE_RTV,7,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvs)));hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvs)));
    const auto rtStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV),dsStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    std::array<ComPtr<ID3D12Resource>,7> colors,depths;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=128;image.Height=64;image.MipLevels=image.SampleDesc.Count=1;
    for(UINT i=0;i<colors.size();++i){image.DepthOrArraySize=i==2?2:1;image.Format=DXGI_FORMAT_R8G8B8A8_UNORM;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&colors[i])));
        device->CreateRenderTargetView(colors[i].Get(),nullptr,{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+i*rtStride});
        image.Format=pd.DSVFormat;image.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depths[i])));
        device->CreateDepthStencilView(depths[i].Get(),nullptr,{dsvs->GetCPUDescriptorHandleForHeapStart().ptr+i*dsStride});}
    ComPtr<ID3D12Resource> rateImage,rateUpload;D3D12_PLACED_SUBRESOURCE_FOOTPRINT rateLayout{};
    if(depthOnly) {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 caps{};Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6,&caps,sizeof(caps)));
        if(caps.VariableShadingRateTier<D3D12_VARIABLE_SHADING_RATE_TIER_2 || !caps.ShadingRateImageTileSize){std::puts("SKIP depth-only inherited-image test needs VRS tier 2");return 77;}
        auto rate=image;rate.Width=(128+caps.ShadingRateImageTileSize-1)/caps.ShadingRateImageTileSize;rate.Height=(64+caps.ShadingRateImageTileSize-1)/caps.ShadingRateImageTileSize;
        rate.DepthOrArraySize=1;rate.Format=DXGI_FORMAT_R8_UINT;rate.Flags=D3D12_RESOURCE_FLAG_NONE;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&rate,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&rateImage)));
        UINT64 size{};device->GetCopyableFootprints(&rate,0,1,0,&rateLayout,nullptr,nullptr,&size);std::vector<uint8_t> data(SIZE_T(size),D3D12_SHADING_RATE_2X2);rateUpload=Buffer(device.Get(),D3D12_HEAP_TYPE_UPLOAD,size,data.data());
        for(auto& draw:draws)for(auto& source:draw.geometry.eyes){source.state.rateImage=rateImage;source.state.rate.image=rateImage.Get();source.state.rate.combiners[1]=D3D12_SHADING_RATE_COMBINER_OVERRIDE;}
    }
    GeometryPassTargets targets;targets.depth=depths[2];if(!depthOnly)targets.colors.push_back(colors[2]);auto pass=std::make_unique<StereoGeometryPass>();
    auto bad=draws;bad[0].geometry.eyes[1].state.stencil++;Require(FAILED(pass->Create(device.Get(),bad,targets)),"Mismatched eye state accepted");
    bad=draws;bad[0].viewMask=4;Require(FAILED(pass->Create(device.Get(),bad,targets)),"Invalid view mask accepted");
    if(!depthOnly){bad=draws;bad[0].geometry.eyes[0].state.rate.image=reinterpret_cast<ID3D12Resource*>(1);Require(FAILED(pass->Create(device.Get(),bad,targets)),"Pixel-shader pass accepted an unproved rate image");}
    Require(FAILED(pass->Create(device.Get(),draws,{{colors[0]},depths[2]})),"Single-layer output accepted");
    Check(pass->Create(device.Get(),draws,targets));Require(pass->DrawCount()==3 && pass->InstanceBytes()==96,"Pass inputs not owned/deduplicated");
    StereoBindingBank sourceBank;std::array<BankDraw,3> sourceDraws;
    for(UINT i=0;i<3;++i)sourceDraws[i]={variant->root.Get(),&variant->plan,{&constants[i],1},{},root.Get()};
    {auto plan=variant->plan;plan.bindings[0].sourceOffset=0;auto bad=sourceDraws;bad[0].plan=&plan;StereoBindingBank rejected;
        Require(FAILED(rejected.CreatePass(device.Get(),bad)),"Invalid original CBV table mapping accepted");}
    Check(sourceBank.CreatePass(device.Get(),sourceDraws));
    ComPtr<ID3D12DescriptorHeap> sourceCpu;
    hd={D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,18,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&sourceCpu)));
    cbCpu=sourceCpu->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<6;++i){D3D12_CONSTANT_BUFFER_VIEW_DESC view{cb->GetGPUVirtualAddress()+256*i,256};
        for(UINT slot=0;slot<3;++slot){device->CreateConstantBufferView(slot==1?&view:nullptr,cbCpu);cbCpu.ptr+=cbStride;}}
    std::array<SharedBankTable,4> sourceTables;std::vector<GeometrySourcePassDraw> sourceInputs;SourceTargets sourceTargets;sourceTargets.depthOnly=depthOnly;
    sourceInputs.reserve(4);
    for(UINT eye=0;eye<2;++eye){
        sourceTargets.colors[eye]={rtvs->GetCPUDescriptorHandleForHeapStart().ptr+(5+eye)*rtStride};
        sourceTargets.depths[eye]={dsvs->GetCPUDescriptorHandleForHeapStart().ptr+(5+eye)*dsStride};
        for(UINT i=0;i<draws.size();++i)if(draws[i].viewMask&(1u<<eye)){
            auto& table=sourceTables[sourceInputs.size()];table.parameter=0;table.offset=(2*i+eye)*3;table.count=3;table.cpuHeap=sourceCpu;table.leases.push_back(cb);
            GeometrySourcePassDraw input;input.geometry.geometry=draws[i].geometry.eyes[eye];input.contract=family->pass;
            input.binding={root,{static_cast<const uint8_t*>(bytes->GetBufferPointer()),bytes->GetBufferSize()},{&table,1}};
            sourceInputs.push_back(std::move(input));sourceTargets.eyes.push_back(eye);
        }
    }
    auto sourcePass=std::make_unique<StereoGeometryPass>();auto badSources=sourceInputs;
    badSources[0].geometry.geometry.state.original.Reset();Require(FAILED(sourcePass->CreateSources(device.Get(),badSources)),"Source pass accepted an unowned original PSO");
    badSources=sourceInputs;badSources[0].geometry.geometry.draw.flags|=64;Require(FAILED(sourcePass->CreateSources(device.Get(),badSources)),"Source pass accepted uncaptured predication");
    badSources=sourceInputs;badSources[0].geometry.geometry.state.rate.known=false;Require(FAILED(sourcePass->CreateSources(device.Get(),badSources)),"Source pass accepted unknown VRS");
    Check(sourcePass->CreateSources(device.Get(),sourceInputs));Require(sourcePass->SourceCount()==4 && !sourcePass->SourceRecordedCount() && !sourcePass->InstanceBytes(),"Source pass changed source occurrence count or rebased instances");
    Require(FAILED(sourcePass->CreateSources(device.Get(),sourceInputs)) && !pass->SourceCount(),"Prepared source/paired initialization was mixed");
    StereoDepthComparison depthComparison,wrongEyes;
    if(depthOnly){Check(depthComparison.Begin(device.Get(),draws,depths[0]->GetDesc()));
        auto swapped=std::make_shared<families::Family>(*family);auto wrong=draws;auto routing=layout;std::swap(routing.locations[0],routing.locations[1]);swapped->pipeline.Reset();
        Check(CreateViewInstancedPipeline(device.Get(),pd,routing,&swapped->pipeline));for(auto& d:wrong)d.family=swapped;
        Check(wrongEyes.Begin(device.Get(),wrong,depths[0]->GetDesc()));}
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    if(rateImage) {
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=rateUpload.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=rateLayout;
        to.pResource=rateImage.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={rateImage.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE};list->ResourceBarrier(1,&barrier);
        ComPtr<ID3D12GraphicsCommandList5> rates;Check(list.As(&rates));rates->RSSetShadingRateImage(rateImage.Get());const D3D12_SHADING_RATE_COMBINER combiners[]{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,D3D12_SHADING_RATE_COMBINER_OVERRIDE};rates->RSSetShadingRate(D3D12_SHADING_RATE_1X1,combiners);
    }
    Require(!sourceBank.BindSourceEye(list.Get(),0),"Original binding before ownership capture accepted");
    Require(sourceBank.CaptureEye(list.Get(),0) && sourceBank.CaptureEye(list.Get(),1),"Original bank capture failed");
    Require(!sourceBank.BindSourceEye(list.Get(),2),"Invalid reference eye accepted");
    const float clear[4]{};for(UINT i=0;i<colors.size();++i){list->ClearRenderTargetView({rtvs->GetCPUDescriptorHandleForHeapStart().ptr+i*rtStride},clear,0,nullptr);list->ClearDepthStencilView({dsvs->GetCPUDescriptorHandleForHeapStart().ptr+i*dsStride},D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,1,0,0,nullptr);}
    for(UINT reference=0;reference<2;++reference)for(UINT eye=0;eye<2;++eye){const UINT target=reference?3+eye:eye;
        auto rt=D3D12_CPU_DESCRIPTOR_HANDLE{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+target*rtStride},dsView=D3D12_CPU_DESCRIPTOR_HANDLE{dsvs->GetCPUDescriptorHandleForHeapStart().ptr+target*dsStride};
        list->OMSetRenderTargets(depthOnly?0:1,depthOnly?nullptr:&rt,FALSE,&dsView);list->SetPipelineState(original.Get());list->SetGraphicsRootSignature(root.Get());ID3D12DescriptorHeap* heaps[]{cbHeap.Get()};list->SetDescriptorHeaps(1,heaps);
        for(UINT i=0;i<draws.size();++i)if(draws[i].viewMask&(1u<<eye)){const auto& source=draws[i].geometry.eyes[eye];const auto& d=source.draw;
            if(reference)Require(sourceBank.BindSourceEye(list.Get(),eye,i),"Original bank binding failed");
            else list->SetGraphicsRootDescriptorTable(0,{cbHeap->GetGPUDescriptorHandleForHeapStart().ptr+(2*i+eye)*3*cbStride});
            list->IASetPrimitiveTopology(source.state.topology);list->OMSetStencilRef(source.state.stencil);list->OMSetBlendFactor(source.state.blendFactor.data());
            list->RSSetViewports(1,&d.viewport);list->RSSetScissorRects(1,&d.scissor);list->IASetIndexBuffer(&d.index);list->IASetVertexBuffers(0,1,&d.vertices[0]);list->IASetVertexBuffers(7,1,&d.vertices[7]);list->DrawIndexedInstanced(d.indices,d.instances,d.firstIndex,d.baseVertex,d.firstInstance);}}
    std::memset(configs.data(),0xFF,sizeof(configs));for(auto& input:draws)for(auto& eye:input.geometry.eyes)std::fill(eye.instances.begin(),eye.instances.end(),0xFF);
    Require(pass->Record(list.Get()),"Pass recording failed");Require(!pass->Record(list.Get()),"A consumed pass recorded twice");pass.reset();
    Require(!sourcePass->Record(list.Get()) && !sourcePass->RecordSource(list.Get(),1,BindSourceTargets,&sourceTargets),"Source pass bypassed its mode/ordered prefix");
    Require(sourcePass->RecordSource(list.Get(),0,BindSourceTargets,&sourceTargets),"First original source did not record");
    ComPtr<ID3D12CommandAllocator> foreignAllocator;ComPtr<ID3D12GraphicsCommandList> foreignList;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&foreignAllocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,foreignAllocator.Get(),nullptr,IID_PPV_ARGS(&foreignList)));
    Require(!sourcePass->RecordSource(foreignList.Get(),1,BindSourceTargets,&sourceTargets) && sourcePass->SourceRecordedCount()==1,
        "A foreign recording continued the partially emitted source prefix");Check(foreignList->Close());
    sourceTargets.fail=1;Require(!sourcePass->RecordSource(list.Get(),1,BindSourceTargets,&sourceTargets) && sourcePass->SourceRecordedCount()==1,"Failed target bind lost the exact emitted prefix");
    Require(!sourcePass->RecordSource(list.Get(),0,BindSourceTargets,&sourceTargets) && !sourcePass->RecordSource(list.Get(),2,BindSourceTargets,&sourceTargets),"Source prefix repeated or jumped an occurrence");
    sourceTargets.fail=UINT_MAX;for(UINT i=1;i<sourceInputs.size();++i)Require(sourcePass->RecordSource(list.Get(),i,BindSourceTargets,&sourceTargets),"Remaining original source stream did not record");
    Require(sourcePass->SourceRecordedCount()==4 && !sourcePass->RecordSource(list.Get(),4,BindSourceTargets,&sourceTargets),"Source stream omitted or duplicated a command");
    auto resetInputs=sourceInputs;
    for(auto& input:resetInputs){input.geometry.geometry.state.rate={};input.geometry.geometry.state.rate.known=true;input.geometry.geometry.state.rateImage.Reset();}
    StereoGeometryPass resetPass;Check(resetPass.CreateSources(device.Get(),resetInputs));
    Check(foreignList->Reset(foreignAllocator.Get(),nullptr));
    Require(resetPass.RecordSource(foreignList.Get(),0,BindSourceTargets,&sourceTargets),"Reset fixture could not record its source prefix");
    Check(foreignList->Close());cvr::gpu::ResetCommandResources(foreignList.Get());Check(foreignAllocator->Reset());
    Check(foreignList->Reset(foreignAllocator.Get(),nullptr));
    Require(!resetPass.RecordSource(foreignList.Get(),1,BindSourceTargets,&sourceTargets) && resetPass.SourceRecordedCount()==1,
        "Reset recording reused the old source-prefix receipt");Check(foreignList->Close());
    sourcePass.reset();sourceInputs.clear();for(auto& table:sourceTables)table={};sourceCpu.Reset();
    struct Copy {ID3D12Resource* resource;UINT subresource;D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;UINT rows;UINT64 rowBytes;};std::vector<Copy> copies;UINT64 total{};
    for(UINT kind=0;kind<3;++kind)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<4;++mode){auto* r=(kind?depths:colors)[mode==2?2:mode==1?3+eye:mode==3?5+eye:eye].Get();const UINT slice=mode==2?eye:0,sub=slice+(kind==2?(mode==2?2:1):0);auto desc=r->GetDesc();Copy c{r,sub};UINT64 size{};
        device->GetCopyableFootprints(&desc,sub,1,total,&c.layout,&c.rows,&c.rowBytes,&size);total+=size;copies.push_back(c);}
    auto rb=Buffer(device.Get(),D3D12_HEAP_TYPE_READBACK,total);
    for(UINT i=0;i<colors.size();++i)for(UINT kind=0;kind<2;++kind){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={(kind?depths:colors)[i].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,kind?D3D12_RESOURCE_STATE_DEPTH_WRITE:D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&barrier);}
    for(const auto& c:copies){D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=c.resource;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=c.subresource;to.pResource=rb.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=c.layout;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    Check(list->Close());ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));ID3D12CommandList* raw=list.Get();auto flight=cvr::gpu::PrepareCommandResources(1,&raw);queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(fence->SetEventOnCompletion(1,event));const auto wait=WaitForSingleObject(event,5000);CloseHandle(event);Require(wait==WAIT_OBJECT_0,"GPU timeout");
    void* mapped{};Check(rb->Map(0,nullptr,&mapped));size_t nonblack{};
    for(size_t i=0;i<copies.size();i+=4)for(UINT mode=1;mode<4;++mode){const auto& a=copies[i];const auto& b=copies[i+mode];Require(a.rows==b.rows && a.rowBytes==b.rowBytes,"Plane footprint mismatch");
        for(UINT y=0;y<a.rows;++y){const auto* row=static_cast<uint8_t*>(mapped)+a.layout.Offset+size_t(y)*a.layout.Footprint.RowPitch;const auto* actual=static_cast<uint8_t*>(mapped)+b.layout.Offset+size_t(y)*b.layout.Footprint.RowPitch;
            Require(!std::memcmp(row,actual,SIZE_T(a.rowBytes)),"Manual original vs bank original vs source stream vs merged color/depth/stencil differs");if(depthOnly?i>=16:i<8)for(UINT x=0;x<a.rowBytes;++x)nonblack+=row[x]!=0;}}
    rb->Unmap(0,nullptr);Require(nonblack>1000,"Blank reference/merged pass");
    if(depthOnly) {
        DepthComparisonResult result;HRESULT status=S_FALSE;
        for(UINT attempt=0;attempt<5000 && status==S_FALSE;++attempt){status=depthComparison.Read(result);if(status==S_FALSE)Sleep(1);}
        Check(status);Require(status==S_OK && result.complete && result.draws==3,"Asynchronous depth comparison incomplete");
        for(UINT eye=0;eye<2;++eye)Require(result.written[eye]>100 && !result.depthDifferent[eye] && !result.stencilDifferent[eye] && !result.invalidDepth[eye],"Owned original/paired depth comparison failed");
        status=S_FALSE;for(UINT attempt=0;attempt<5000 && status==S_FALSE;++attempt){status=wrongEyes.Read(result);if(status==S_FALSE)Sleep(1);}
        Check(status);Require(status==S_OK && result.complete && result.depthDifferent[0] && result.depthDifferent[1],"Swapped-eye negative control was not detected");
    }
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T n{};info->GetMessage(i,nullptr,&n);std::vector<uint8_t> data(n);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(info->GetMessage(i,message,&n));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("D3D12 validation error");}}
    cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
    std::puts(depthOnly?"PASS PS-less depth/stencil exact with inherited VRS image vs canonical 1x1 and masks 3/1/2":
        "PASS original shaders vs whole prepared view-instanced pass: color/depth/stencil exact, masks 3/1/2, blend/scissor/instance rebase and fenced lifetime");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
