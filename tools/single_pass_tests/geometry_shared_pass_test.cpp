#include "Render/StereoGeometryPass.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/CommandResources.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace cvr::stereo;
using Microsoft::WRL::ComPtr;
namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void Check(HRESULT value){if(FAILED(value))throw std::runtime_error("D3D12 HRESULT "+std::to_string(uint32_t(value)));}
static std::vector<uint8_t> Read(const char* path){std::ifstream file(path,std::ios::binary);Require(bool(file),"Shader missing");return {std::istreambuf_iterator<char>(file),{}};}
static std::string Hash(std::span<const uint8_t> bytes){
    BCRYPT_ALG_HANDLE algorithm{};Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider");
    std::array<uint8_t,32> digest{};const auto result=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);Require(result>=0,"SHA256 digest");std::string output;constexpr char hex[]="0123456789abcdef";
    for(const auto byte:digest){output+=hex[byte>>4];output+=hex[byte&15];}return output;
}
class Marker final:public IUnknown {
    std::atomic<ULONG> refs_{1};std::atomic<UINT>& destroyed_;
public:
    explicit Marker(std::atomic<UINT>& destroyed):destroyed_(destroyed){}
    ~Marker(){++destroyed_;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs_;if(!left)delete this;return left;}
};
static void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&barrier);
}
static ComPtr<ID3D12Resource> Buffer(ID3D12Device* device,UINT64 bytes,D3D12_HEAP_TYPE type,const void* data=nullptr){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES heap{};heap.Type=type;ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource)));
    if(data){void* mapped{};Check(resource->Map(0,nullptr,&mapped));std::memcpy(mapped,data,SIZE_T(bytes));resource->Unmap(0,nullptr);}return resource;
}
static ComPtr<ID3D12DescriptorHeap> Heap(ID3D12Device* device,D3D12_DESCRIPTOR_HEAP_TYPE type,UINT count,D3D12_DESCRIPTOR_HEAP_FLAGS flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE){
    const D3D12_DESCRIPTOR_HEAP_DESC desc{type,count,flags,0};ComPtr<ID3D12DescriptorHeap> heap;Check(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)));return heap;
}
static ComPtr<ID3D12Resource> Image(ID3D12Device* device,UINT width,UINT height,UINT16 layers=1){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=width;desc.Height=height;desc.DepthOrArraySize=layers;desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> image;Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&image)));return image;
}
struct __declspec(uuid("543466E9-A4FC-46C0-B645-91E51E5B10CB")) Epoch final:IUnknown {
    std::atomic<ULONG> refs{1};std::atomic<UINT>& destroyed;bool active=true;
    uint64_t list{},generation=1;source_selectors::FamilyKey family;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12CommandQueue> queue;
    std::array<ComPtr<ID3D12Resource>,4> textures;
    ComPtr<ID3D12Resource> material;
    D3D12_SHADER_RESOURCE_VIEW_DESC pairedSrv{};
    std::array<ComPtr<ID3D12Resource>,2> cameraGpu,selectorGpu;
    std::array<std::array<uint8_t,848>,2> cameras{};std::array<UINT,2> indices{5000,6000};
    std::array<source_binding_assembly::Scope,2> scopes;
    std::array<source_selectors::Result,2> selectors;
    std::vector<GeometrySharedResourceRead> resources;std::array<SharedBankTable,3> tables;
    explicit Epoch(std::atomic<UINT>& count):destroyed(count){}
    ~Epoch(){++destroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown) && id!=__uuidof(Epoch))return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto left=--refs;if(!left)delete this;return left;}
    static bool Validate(const GeometrySharedReadContract& contract,const families::Family& value,std::span<const BankPair> pairs,
        std::span<const SharedBankTable> shared,ID3D12GraphicsCommandList* actual){
        ComPtr<Epoch> owner;if(FAILED(contract.epochOwner.As(&owner)) || !owner->active)return false;const auto& proof=*owner.Get();
        if(contract.list!=proof.list || contract.generation!=proof.generation || (actual && reinterpret_cast<uintptr_t>(actual)!=proof.list) ||
            contract.allocatorOwner.Get()!=static_cast<IUnknown*>(proof.allocator.Get()) || contract.queueOwner.Get()!=static_cast<IUnknown*>(proof.queue.Get()) ||
            source_selectors::FamilyKey{value.rootSha256,value.vertexSha256,value.pixelSha256}!=proof.family || shared.size()!=proof.tables.size() ||
            contract.resources.size()!=proof.resources.size())return false;
        for(size_t n=0;n<shared.size();++n)if(shared[n].parameter!=proof.tables[n].parameter || shared[n].count!=proof.tables[n].count ||
            shared[n].offset!=proof.tables[n].offset || shared[n].cpuHeap!=proof.tables[n].cpuHeap)return false;
        for(size_t n=0;n<proof.resources.size();++n){const auto& a=contract.resources[n];const auto& b=proof.resources[n];
            if(a.resource!=b.resource || a.state!=b.state || a.allocationOwner!=b.allocationOwner || a.producerPosition!=b.producerPosition)return false;}
        for(UINT eye=0;eye<2;++eye){const auto& source=contract.eyes[eye];const auto& expected=proof.selectors[eye];
            if(source.scope!=proof.scopes[eye] || source.gpuInputOwner.Get()!=contract.epochOwner.Get() || !source.selectors ||
                source.selectors->family!=expected.family || source.selectors->tables.size()!=1 || source.selectors->selections.size()!=1 ||
                source.selectors->tables[0].key!=expected.tables[0].key || source.selectors->tables[0].offsets!=expected.tables[0].offsets ||
                source.selectors->selections[0].sourceDword!=proof.indices[eye] || source.selectors->selections[0].shaderIndex!=proof.indices[eye])return false;
        }
        if(pairs.size()!=3)return false;
        for(const auto& pair:pairs)for(UINT eye=0;eye<2;++eye){const auto& source=pair.eyes[eye];
            if(pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV && pair.binding.sourceRegister==1){if(source.constants.size()!=848 ||
                    std::memcmp(source.constants.data(),proof.cameras[eye].data(),848))return false;
            }else if(pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_CBV && pair.binding.sourceRegister==4){if(source.constants.size()!=4 ||
                    std::memcmp(source.constants.data(),&proof.indices[eye],4))return false;
            }else if(pair.binding.type==D3D12_DESCRIPTOR_RANGE_TYPE_SRV && pair.binding.sourceRegister==0){if(source.source!=proof.textures[eye] ||
                    std::memcmp(&source.srv,&proof.pairedSrv,sizeof(source.srv)))return false;
            }else return false;
        }return true;
    }
};
static geometry::BufferLease Lease(ID3D12Resource* resource){return geometry::AcquireBuffer({reinterpret_cast<uintptr_t>(resource),resource->GetGPUVirtualAddress(),resource->GetDesc().Width},resource->GetGPUVirtualAddress(),resource->GetDesc().Width);}
int main(int argc,char** argv)try{
    Require(argc==5,"Expected originalVS originalPS pairedVS pairedPS");const auto originalVs=Read(argv[1]),originalPs=Read(argv[2]),pairedVs=Read(argv[3]),pairedPs=Read(argv[4]);
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));ComPtr<ID3D12Device2> device;
    Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));D3D12_FEATURE_DATA_D3D12_OPTIONS3 caps{};
    Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&caps,sizeof(caps)));if(!caps.ViewInstancingTier){std::puts("SKIP hardware has no View Instancing");return 77;}
    std::atomic<UINT> epochsDestroyed{},markersDestroyed{};ComPtr<Epoch> epoch;epoch.Attach(new Epoch(epochsDestroyed));ComPtr<IUnknown> marker;marker.Attach(new Marker(markersDestroyed));
    D3D12_DESCRIPTOR_RANGE ranges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,5,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,16,0,2,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,1,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,67,0,0}};
    D3D12_ROOT_PARAMETER parameters[5]{};for(UINT p=0;p<5;++p){parameters[p].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[p].DescriptorTable={1,ranges+p};
        parameters[p].ShaderVisibility=p?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_ALL;}
    D3D12_STATIC_SAMPLER_DESC fixed{};fixed.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;fixed.AddressU=fixed.AddressV=fixed.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    fixed.BorderColor=D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    fixed.MaxAnisotropy=1;fixed.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;fixed.MaxLOD=D3D12_FLOAT32_MAX;fixed.RegisterSpace=3;fixed.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC rootDesc{5,parameters,1,&fixed,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> blob,error;
    Check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto variant=std::make_shared<families::RootVariant>();const std::array<PairedBinding,3> bindings{{
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100},
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_PIXEL,4,101},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,0,101}}};
    Check(BuildPairedRoot(blob->GetBufferPointer(),blob->GetBufferSize(),bindings,variant->plan));
    Check(device->CreateRootSignature(0,variant->plan.serialized.data(),variant->plan.serialized.size(),IID_PPV_ARGS(&variant->root)));
    D3D12_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root.Get();desc.VS={originalVs.data(),originalVs.size()};desc.PS={originalPs.data(),originalPs.size()};desc.InputLayout={elements,2};
    desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.NumRenderTargets=1;desc.RTVFormats[0]=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.SampleDesc.Count=1;desc.SampleMask=UINT_MAX;
    desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;desc.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=desc.BlendState.RenderTarget[0];blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=15;
    ComPtr<ID3D12PipelineState> original;Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&original)));
    auto family=std::make_shared<families::Family>();family->sourceRoot=root;family->pairedRoot=variant;family->instanceRebaseSafe=true;
    family->rootSha256=Hash({static_cast<const uint8_t*>(blob->GetBufferPointer()),blob->GetBufferSize()});family->vertexSha256=Hash(originalVs);family->pixelSha256=Hash(originalPs);
    epoch->family={family->rootSha256,family->vertexSha256,family->pixelSha256};family->uses={
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,0,1,1,848},{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,5,0,4,1,4},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,0,1,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,1,0,32768,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,5,2,0,16,0},{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,5,3,0,1,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0,67,1,0}};
    family->pass.targets.NumRenderTargets=1;family->pass.targets.RTFormats[0]=desc.RTVFormats[0];family->pass.samples=desc.SampleDesc;
    family->pass.topology=desc.PrimitiveTopologyType;family->pass.fixedRasterState=family->pass.pixelShader=true;
    desc.pRootSignature=variant->root.Get();desc.VS={pairedVs.data(),pairedVs.size()};desc.PS={pairedPs.data(),pairedPs.size()};ViewInstancedLayout layout;layout.allowMasking=true;
    Check(CreateViewInstancedPipeline(device.Get(),desc,layout,&family->pipeline));auto swappedFamily=std::make_shared<families::Family>(*family);
    std::swap(layout.locations[0],layout.locations[1]);swappedFamily->pipeline.Reset();Check(CreateViewInstancedPipeline(device.Get(),desc,layout,&swappedFamily->pipeline));
    D3D12_COMMAND_QUEUE_DESC queueDesc{};Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&epoch->queue)));
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&epoch->allocator)));ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,epoch->allocator.Get(),nullptr,IID_PPV_ARGS(&list)));epoch->list=reinterpret_cast<uintptr_t>(list.Get());
    const float vertices[]{-1,-1,-1,3,3,-1};const uint16_t indices[]{0,1,2};const std::array<float,12> instance{};
    auto vb=Buffer(device.Get(),sizeof(vertices),D3D12_HEAP_TYPE_UPLOAD,vertices),ib=Buffer(device.Get(),sizeof(indices),D3D12_HEAP_TYPE_UPLOAD,indices),inst=Buffer(device.Get(),sizeof(instance),D3D12_HEAP_TYPE_UPLOAD,instance.data());
    auto rtvs=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,10);const UINT rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),samplerStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for(UINT n=0;n<4;++n){epoch->textures[n]=Image(device.Get(),1,1);const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*rtvStride};
        device->CreateRenderTargetView(epoch->textures[n].Get(),nullptr,rtv);const float color[]{n<2?10.f+10.f*n:100.f+100.f*(n-2),0,0,1};list->ClearRenderTargetView(rtv,color,0,nullptr);
        Transition(list.Get(),epoch->textures[n].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        epoch->resources.push_back({epoch->textures[n],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,epoch->textures[n],n+1});}
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=desc.RTVFormats[0];srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    epoch->pairedSrv=srv;
    epoch->material=Image(device.Get(),1,1,4);
    for(UINT layer=0;layer<4;++layer){D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=desc.RTVFormats[0];view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray={0,layer,1,0};const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(6+layer)*rtvStride};
        device->CreateRenderTargetView(epoch->material.Get(),&view,rtv);const float color[]{900.f+100.f*layer,0,0,1};list->ClearRenderTargetView(rtv,color,0,nullptr);}
    Transition(list.Get(),epoch->material.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    epoch->resources.push_back({epoch->material,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,epoch->material,7});
    D3D12_SHADER_RESOURCE_VIEW_DESC arraySrv{};arraySrv.Format=desc.RTVFormats[0];arraySrv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    arraySrv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;arraySrv.Texture2DArray={0,1,1,2,0,0};
    auto materialTable=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1);
    device->CreateShaderResourceView(epoch->material.Get(),&arraySrv,materialTable->GetCPUDescriptorHandleForHeapStart());
    auto sparse=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,6001),samplers=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,16);
    for(UINT n=0;n<6001;++n)device->CreateShaderResourceView(nullptr,&srv,{sparse->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*stride});
    for(UINT eye=0;eye<2;++eye)device->CreateShaderResourceView(epoch->textures[eye+2].Get(),&srv,{sparse->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(epoch->indices[eye])*stride});
    D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;
    for(UINT n=0;n<16;++n)device->CreateSampler(&sampler,{samplers->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(n)*samplerStride});
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    device->CreateSampler(&sampler,{samplers->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(13)*samplerStride});
    epoch->tables[0].parameter=3;epoch->tables[0].count=6001;epoch->tables[0].cpuHeap=sparse;epoch->tables[0].leases={epoch->textures[2],epoch->textures[3],marker};
    epoch->tables[1].parameter=2;epoch->tables[1].count=16;epoch->tables[1].cpuHeap=samplers;epoch->tables[1].leases={marker};
    epoch->tables[2].parameter=4;epoch->tables[2].count=1;epoch->tables[2].cpuHeap=materialTable;epoch->tables[2].leases={epoch->material,marker};
    auto callerCameras=epoch->cameras;auto callerIndices=epoch->indices;auto callerSelectors=epoch->selectors;
    std::array<BankPair,3> pairs;GeometryPassDraw input;input.family=family;input.constants=pairs;input.shared=epoch->tables;
    for(UINT n=0;n<3;++n)pairs[n].binding=bindings[n];
    for(UINT eye=0;eye<2;++eye){const float color[]{float(eye+1),0,0,1};std::memcpy(epoch->cameras[eye].data(),color,sizeof(color));
        const float tail=float(eye+3);std::memcpy(epoch->cameras[eye].data()+844,&tail,sizeof(tail));
        callerCameras[eye]=epoch->cameras[eye];pairs[0].eyes[eye].constants=callerCameras[eye];pairs[1].eyes[eye].constants={reinterpret_cast<const uint8_t*>(&callerIndices[eye]),4};
        pairs[2].eyes[eye].source=epoch->textures[eye];pairs[2].eyes[eye].srv=srv;pairs[2].eyes[eye].state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        std::array<uint8_t,1024> camera{};std::memcpy(camera.data(),epoch->cameras[eye].data(),848);epoch->cameraGpu[eye]=Buffer(device.Get(),1024,D3D12_HEAP_TYPE_UPLOAD,camera.data());
        std::array<uint8_t,256> selector{};std::memcpy(selector.data(),&epoch->indices[eye],4);epoch->selectorGpu[eye]=Buffer(device.Get(),256,D3D12_HEAP_TYPE_UPLOAD,selector.data());
        auto& geometry=input.geometry.eyes[eye];geometry.index=Lease(ib.Get());geometry.vertices[0]=Lease(vb.Get());geometry.vertices[7]=Lease(inst.Get());
        geometry.instances.assign(reinterpret_cast<const uint8_t*>(instance.data()),reinterpret_cast<const uint8_t*>(instance.data())+sizeof(instance));
        auto& draw=geometry.draw;draw.side=eye?0:1;draw.frame=1;draw.sequence=eye+1;draw.pipeline=reinterpret_cast<uintptr_t>(original.Get());draw.root=reinterpret_cast<uintptr_t>(root.Get());
        draw.indices=3;draw.instances=1;draw.flags=191;draw.inputSlotMask=129;draw.rtCount=1;draw.index={ib->GetGPUVirtualAddress(),sizeof(indices),DXGI_FORMAT_R16_UINT};
        draw.vertices[0]={vb->GetGPUVirtualAddress(),sizeof(vertices),8};draw.vertices[7]={inst->GetGPUVirtualAddress(),sizeof(instance),48};draw.viewport={0,0,8,8,0,1};draw.scissor={0,0,8,8};
        geometry.state.original=original;geometry.state.topology=D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;geometry.state.rate.known=true;
        epoch->scopes[eye]={draw.root,epoch->list,1,eye+1,eye+1,draw.side};auto& selected=epoch->selectors[eye];selected.family=epoch->family;selected.selectorEvaluationComplete=true;selected.evaluatedSites=1;
        selected.selections.push_back({0,epoch->indices[eye],epoch->indices[eye],epoch->indices[eye]});source_selectors::TableSelection table;
        table.key={family->rootSha256,5,3,1,0,0,32768};table.offsets={epoch->indices[eye]};table.count=epoch->indices[eye]+1;table.uses={{epoch->indices[eye],1,D3D12_DESCRIPTOR_RANGE_TYPE_SRV}};selected.tables.push_back(std::move(table));
    }
    for(UINT eye=0;eye<2;++eye)epoch->resources.push_back({epoch->selectorGpu[eye],D3D12_RESOURCE_STATE_GENERIC_READ,epoch->selectorGpu[eye],5+eye});
    input.sharedRead.list=epoch->list;input.sharedRead.generation=1;input.sharedRead.epochOwner=epoch;input.sharedRead.allocatorOwner=epoch->allocator;input.sharedRead.queueOwner=epoch->queue;
    input.sharedRead.resources=epoch->resources;input.sharedRead.validate=Epoch::Validate;
    callerSelectors=epoch->selectors;
    for(UINT eye=0;eye<2;++eye)input.sharedRead.eyes[eye]={epoch->scopes[eye],&callerSelectors[eye],epoch};
    auto originalVisible=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,6014,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    auto originalSamplers=Heap(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,16,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    const auto originalCpu=originalVisible->GetCPUDescriptorHandleForHeapStart();
    for(UINT slot=0;slot<12;++slot)device->CreateConstantBufferView(nullptr,{originalCpu.ptr+SIZE_T(slot)*stride});
    for(UINT eye=0;eye<2;++eye){const D3D12_CONSTANT_BUFFER_VIEW_DESC camera{epoch->cameraGpu[eye]->GetGPUVirtualAddress(),1024},selector{epoch->selectorGpu[eye]->GetGPUVirtualAddress(),256};
        device->CreateConstantBufferView(&camera,{originalCpu.ptr+SIZE_T(eye*5+1)*stride});device->CreateConstantBufferView(&selector,{originalCpu.ptr+SIZE_T(eye*5+4)*stride});
        device->CreateShaderResourceView(epoch->textures[eye].Get(),&srv,{originalCpu.ptr+SIZE_T(10+eye)*stride});}
    device->CopyDescriptorsSimple(6001,{originalCpu.ptr+SIZE_T(12)*stride},sparse->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(1,{originalCpu.ptr+SIZE_T(6013)*stride},materialTable->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->CopyDescriptorsSimple(16,originalSamplers->GetCPUDescriptorHandleForHeapStart(),samplers->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    std::array<ComPtr<ID3D12Resource>,2> originals{Image(device.Get(),8,8),Image(device.Get(),8,8)};
    Require(cvr::gpu::KeepCommandResources(list.Get(),{originalVisible.Get(),originalSamplers.Get()}),"Original bindings lacked recording ownership");
    for(UINT eye=0;eye<2;++eye){const D3D12_CPU_DESCRIPTOR_HANDLE rtv{rtvs->GetCPUDescriptorHandleForHeapStart().ptr+SIZE_T(4+eye)*rtvStride};device->CreateRenderTargetView(originals[eye].Get(),nullptr,rtv);
        ID3D12DescriptorHeap* heaps[]{originalVisible.Get(),originalSamplers.Get()};list->SetDescriptorHeaps(2,heaps);list->SetGraphicsRootSignature(root.Get());list->SetPipelineState(original.Get());
        const auto gpu=originalVisible->GetGPUDescriptorHandleForHeapStart();list->SetGraphicsRootDescriptorTable(0,{gpu.ptr+UINT64(eye*5)*stride});
        list->SetGraphicsRootDescriptorTable(1,{gpu.ptr+UINT64(10+eye)*stride});list->SetGraphicsRootDescriptorTable(2,originalSamplers->GetGPUDescriptorHandleForHeapStart());
        list->SetGraphicsRootDescriptorTable(3,{gpu.ptr+UINT64(12)*stride});list->SetGraphicsRootDescriptorTable(4,{gpu.ptr+UINT64(6013)*stride});
        list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const auto& source=input.geometry.eyes[eye].draw;list->RSSetViewports(1,&source.viewport);list->RSSetScissorRects(1,&source.scissor);list->IASetIndexBuffer(&source.index);
        list->IASetVertexBuffers(0,1,&source.vertices[0]);list->IASetVertexBuffers(7,1,&source.vertices[7]);list->DrawIndexedInstanced(3,1,0,0,0);}
    auto output=Image(device.Get(),8,8,2),swapped=Image(device.Get(),8,8,2);const GeometryPassTargets targets{{output},{}};
    auto reject=[&](const GeometryPassDraw& candidate){StereoGeometryPass pass;Require(FAILED(pass.Create(device.Get(),{&candidate,1},targets)) && !pass.DrawCount(),"Invalid shared admission/coverage published a pass");};
    {auto bad=input;bad.sharedRead.validate=nullptr;reject(bad);bad=input;bad.sharedRead.epochOwner.Reset();reject(bad);bad=input;bad.sharedRead.eyes[0].gpuInputOwner.Reset();reject(bad);
        bad=input;bad.sharedRead.eyes[0].scope.eye=0;reject(bad);bad=input;bad.sharedRead.eyes[0].scope.root++;reject(bad);}
    {auto reads=epoch->resources;reads[0].state.reset();auto bad=input;bad.sharedRead.resources=reads;reject(bad);reads=epoch->resources;reads[0].allocationOwner.Reset();bad.sharedRead.resources=reads;reject(bad);}
    {auto selectors=epoch->selectors[1];selectors.selectorEvaluationComplete=false;auto bad=input;bad.sharedRead.eyes[1].selectors=&selectors;reject(bad);
        selectors=epoch->selectors[1];selectors.tables[0].key.parameter=2;reject(bad);selectors=epoch->selectors[1];selectors.tables[0].offsets[0]=6001;reject(bad);
        selectors=epoch->selectors[1];selectors.tables.push_back(selectors.tables[0]);reject(bad);selectors=epoch->selectors[1];selectors.family.pixelSha256="foreign";reject(bad);}
    {auto tables=epoch->tables;--tables[0].count;auto bad=input;bad.shared=tables;reject(bad);tables=epoch->tables;tables[0].parameter=1;bad.shared=tables;reject(bad);
        tables=epoch->tables;tables[2].parameter=3;reject(bad);tables=epoch->tables;tables[2].count=0;reject(bad);
        auto unsupported=std::make_shared<families::Family>(*family);unsupported->uses.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_UAV,5,0,0,1,0});bad=input;bad.family=unsupported;reject(bad);}
    {auto reversed=pairs;std::swap(reversed[2].eyes[0],reversed[2].eyes[1]);auto bad=input;bad.constants=reversed;reject(bad);}
    {auto wrong=pairs;wrong[2].eyes[0].state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;auto bad=input;bad.constants=wrong;reject(bad);
        wrong=pairs;wrong[2].eyes[0].srv.Shader4ComponentMapping=D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(4,1,2,3);reject(bad);}
    auto pass=std::make_unique<StereoGeometryPass>();Check(pass->Create(device.Get(),{&input,1},targets));auto negative=std::make_unique<StereoGeometryPass>();auto negativeInput=input;negativeInput.family=swappedFamily;
    Check(negative->Create(device.Get(),{&negativeInput,1},{{swapped},{}}));epoch->active=false;Require(!pass->Record(list.Get()),"Lost actual epoch allowed recording");epoch->active=true;
    // Callers' mutable spans are not retained; owned private constants/selectors
    // are checked again against this fixture's genuine GPU input owner.
    for(auto& camera:callerCameras)camera.fill(0xEE);callerIndices={UINT_MAX,UINT_MAX};for(auto& selectors:callerSelectors)selectors={};
    Require(pass->Record(list.Get()) && negative->Record(list.Get()),"Certified shared paired pass did not record");
    struct Copy {ComPtr<ID3D12Resource> source;UINT subresource{};D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};};std::array<Copy,6> copies;UINT64 bytes{};
    for(UINT n=0;n<6;++n){auto& copy=copies[n];copy.source=n<2?output:n<4?swapped:originals[n-4];copy.subresource=n<4?n%2:0;const auto image=copy.source->GetDesc();UINT64 size{};
        device->GetCopyableFootprints(&image,copy.subresource,1,bytes,&copy.footprint,nullptr,nullptr,&size);bytes+=size;}
    auto readback=Buffer(device.Get(),bytes,D3D12_HEAP_TYPE_READBACK);Transition(list.Get(),output.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(list.Get(),swapped.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(const auto& source:originals)Transition(list.Get(),source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    for(const auto& copy:copies){D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=copy.source.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=copy.subresource;
        to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=copy.footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}Check(list->Close());
    auto queue=epoch->queue;ComPtr<ID3D12Fence> gate,done;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&done)));
    Check(queue->Wait(gate.Get(),1));ID3D12CommandList* raw=list.Get();auto flight=cvr::gpu::PrepareCommandResources(1,&raw);Require(!flight.batches.empty(),"Pass owners not retained on recording");
    queue->ExecuteCommandLists(1,&raw);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    pass.reset();negative.reset();input={};negativeInput={};pairs={};epoch.Reset();marker.Reset();sparse.Reset();samplers.Reset();
    Require(!epochsDestroyed && !markersDestroyed,"Blocked GPU flight dropped explicit epoch/shared owners");Check(gate->Signal(1));Check(queue->Signal(done.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(done->SetEventOnCompletion(1,event));const auto waited=WaitForSingleObject(event,10000);CloseHandle(event);
    Require(waited==WAIT_OBJECT_0,"Shared geometry GPU timeout");cvr::gpu::ResetCommandResources(list.Get());cvr::gpu::CollectCommandResources();
    Require(epochsDestroyed==1 && markersDestroyed==1,"Completed GPU flight leaked epoch/shared owners or destination-list cycle");
    void* mapped{};Check(readback->Map(0,nullptr,&mapped));const float expected[]{1215,1327,1327,1215,1215,1327};
    for(UINT n=0;n<6;++n)for(UINT y=0;y<8;++y)for(UINT x=0;x<8;++x){const auto* pixel=reinterpret_cast<const float*>(static_cast<const uint8_t*>(mapped)+copies[n].footprint.Offset+SIZE_T(y)*copies[n].footprint.Footprint.RowPitch+SIZE_T(x)*16);
        Require(pixel[0]==expected[n] && pixel[1]==0 && pixel[2]==0 && pixel[3]==1,"High-index shared SRV/sampler or paired eye/layer output differs");}
    readback->Unmap(0,nullptr);ComPtr<ID3D12InfoQueue> info;Check(device.As(&info));
    for(UINT64 n=0;n<info->GetNumStoredMessages();++n){SIZE_T size{};Check(info->GetMessage(n,nullptr,&size));std::vector<uint8_t> data(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());Check(info->GetMessage(n,message,&size));
        if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",message->pDescription);throw std::runtime_error("Shared geometry validation error");}}
    std::puts("PASS hardware paired SRV/848-byte CBV, qualified unbounded selections5000/6000, finite t67 Texture2DArray FirstArraySlice1, original sampler/static sampler, exact two-layer readback, swapped-layer negative and explicit epoch/fence ownership; no native replay permission");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
