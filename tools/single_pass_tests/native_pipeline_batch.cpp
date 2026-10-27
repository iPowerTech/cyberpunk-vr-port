#include "Render/StereoPairedRoot.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/NativeStereoFamilies.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <bcrypt.h>
#include <json/json.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <cstdio>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
using namespace cvr::stereo;
static void Require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
static void Check(HRESULT hr,const char* message){Require(SUCCEEDED(hr),message);}
static Json::Value Load(const fs::path& path) {
    std::ifstream f(path);Require(bool(f),"Missing JSON input");Json::Value v;f>>v;return v;
}
static const Json::Value& Field(const Json::Value& v,const char* name) {
    Require(v.isObject() && v.isMember(name),name);return v[name];
}
static std::string Hash(const std::vector<uint8_t>& bytes) {
    BCRYPT_ALG_HANDLE alg{};Require(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider");
    uint8_t hash[32]{};const auto status=BCryptHash(alg,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),hash,sizeof(hash));
    BCryptCloseAlgorithmProvider(alg,0);Require(status>=0,"SHA256 hash");
    std::string text;for(auto b:hash){text+="0123456789abcdef"[b>>4];text+="0123456789abcdef"[b&15];}return text;
}
static std::vector<uint8_t> Read(const fs::path& path,const std::string& expected) {
    Require(fs::is_regular_file(path) && fs::file_size(path)<1024*1024,"Invalid binary input");
    std::ifstream f(path,std::ios::binary);std::vector<uint8_t> data{std::istreambuf_iterator<char>(f),{}};
    Require(Hash(data)==expected,"Binary content hash mismatch");return data;
}
static std::string Hex(HRESULT hr){char text[12]{};std::snprintf(text,sizeof(text),"0x%08X",UINT(hr));return text;}
static Json::Value Messages(ID3D12InfoQueue* info) {
    Json::Value result(Json::arrayValue);
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i) {
        SIZE_T size{};info->GetMessage(i,nullptr,&size);std::vector<uint8_t> bytes(size);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Check(info->GetMessage(i,m,&size),"Debug message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) {
            Json::Value row;row["id"]=UINT(m->ID);row["severity"]=UINT(m->Severity);row["text"]=m->pDescription;result.append(row);
        }
    }
    info->ClearStoredMessages();return result;
}
#define UINT_FIELD(dst,field,json) dst.field=static_cast<decltype(dst.field)>(Field(json,#field).asUInt())
#define FLOAT_FIELD(dst,field,json) dst.field=static_cast<decltype(dst.field)>(Field(json,#field).asDouble())
static D3D12_DEPTH_STENCILOP_DESC Stencil(const Json::Value& j) {
    D3D12_DEPTH_STENCILOP_DESC d{};
    UINT_FIELD(d,StencilFailOp,j);UINT_FIELD(d,StencilDepthFailOp,j);UINT_FIELD(d,StencilPassOp,j);UINT_FIELD(d,StencilFunc,j);return d;
}
struct NativeDesc {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    std::vector<std::string> names;
    std::vector<D3D12_INPUT_ELEMENT_DESC> input;
    explicit NativeDesc(const Json::Value& j) {
        const auto& so=Field(j,"StreamOutput");
        Require(Field(so,"NumEntries").asUInt()==0 && Field(so,"NumStrides").asUInt()==0,"Stream output needs an explicit replay implementation");
        UINT_FIELD(desc.StreamOutput,RasterizedStream,so);
        const auto& b=Field(j,"BlendState");UINT_FIELD(desc.BlendState,AlphaToCoverageEnable,b);UINT_FIELD(desc.BlendState,IndependentBlendEnable,b);
        Require(Field(b,"RenderTarget").size()==8,"Invalid blend array");
        for(UINT i=0;i<8;++i) {
            const auto& from=b["RenderTarget"][i];auto& to=desc.BlendState.RenderTarget[i];
            UINT_FIELD(to,BlendEnable,from);UINT_FIELD(to,LogicOpEnable,from);
            UINT_FIELD(to,SrcBlend,from);UINT_FIELD(to,DestBlend,from);UINT_FIELD(to,BlendOp,from);
            UINT_FIELD(to,SrcBlendAlpha,from);UINT_FIELD(to,DestBlendAlpha,from);UINT_FIELD(to,BlendOpAlpha,from);
            UINT_FIELD(to,LogicOp,from);UINT_FIELD(to,RenderTargetWriteMask,from);
        }
        const auto& r=Field(j,"RasterizerState");auto& raster=desc.RasterizerState;
        UINT_FIELD(raster,FillMode,r);UINT_FIELD(raster,CullMode,r);UINT_FIELD(raster,FrontCounterClockwise,r);
        raster.DepthBias=Field(r,"DepthBias").asInt();FLOAT_FIELD(raster,DepthBiasClamp,r);FLOAT_FIELD(raster,SlopeScaledDepthBias,r);
        UINT_FIELD(raster,DepthClipEnable,r);UINT_FIELD(raster,MultisampleEnable,r);UINT_FIELD(raster,AntialiasedLineEnable,r);
        UINT_FIELD(raster,ForcedSampleCount,r);UINT_FIELD(raster,ConservativeRaster,r);
        const auto& d=Field(j,"DepthStencilState");auto& depth=desc.DepthStencilState;
        UINT_FIELD(depth,DepthEnable,d);UINT_FIELD(depth,DepthWriteMask,d);UINT_FIELD(depth,DepthFunc,d);
        UINT_FIELD(depth,StencilEnable,d);UINT_FIELD(depth,StencilReadMask,d);UINT_FIELD(depth,StencilWriteMask,d);
        depth.FrontFace=Stencil(Field(d,"FrontFace"));depth.BackFace=Stencil(Field(d,"BackFace"));
        const auto& elements=Field(Field(j,"InputLayout"),"pInputElementDescs");
        Require(elements.size()==j["InputLayout"]["NumElements"].asUInt() && elements.size()<=32,"Invalid input elements");
        names.reserve(elements.size());input.reserve(elements.size());
        for(const auto& e:elements) {
            names.push_back(Field(e,"SemanticName").asString());D3D12_INPUT_ELEMENT_DESC element{};element.SemanticName=names.back().c_str();
            UINT_FIELD(element,SemanticIndex,e);UINT_FIELD(element,Format,e);UINT_FIELD(element,InputSlot,e);
            UINT_FIELD(element,AlignedByteOffset,e);UINT_FIELD(element,InputSlotClass,e);UINT_FIELD(element,InstanceDataStepRate,e);input.push_back(element);
        }
        desc.InputLayout={input.data(),UINT(input.size())};
        UINT_FIELD(desc,SampleMask,j);UINT_FIELD(desc,IBStripCutValue,j);UINT_FIELD(desc,PrimitiveTopologyType,j);
        UINT_FIELD(desc,NumRenderTargets,j);Require(desc.NumRenderTargets<=8 && Field(j,"RTVFormats").size()==8,"Invalid targets");
        for(UINT i=0;i<8;++i)desc.RTVFormats[i]=DXGI_FORMAT(j["RTVFormats"][i].asUInt());
        UINT_FIELD(desc,DSVFormat,j);UINT_FIELD(desc.SampleDesc,Count,Field(j,"SampleDesc"));UINT_FIELD(desc.SampleDesc,Quality,j["SampleDesc"]);
        UINT_FIELD(desc,NodeMask,j);UINT_FIELD(desc,Flags,j);
        Require(Field(Field(j,"CachedPSO"),"CachedBlobSizeInBytes").asUInt64()==0,"Native cached blob not supported by this export");
    }
};
int wmain(int argc,wchar_t** argv) try {
    Require(argc==4 || (argc==6 && std::wstring(argv[4])==L"--family-manifest"),"native_pipeline_batch <native index.json> <paired manifest.json> <output.json> [--family-manifest package.json]");
    const bool runtime=argc==6;
    if(runtime){Require(SetEnvironmentVariableW(L"CVR_STEREO_FAMILY_MANIFEST",argv[5])!=0,"Set family package");
        Require(families::Enabled(),CyberpunkVR_StereoFamilyError);}
    const fs::path index=argv[1],paired=argv[2],out=argv[3];const auto capture=Load(index),variants=Load(paired);
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"Debug layer");debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"Factory");ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"Adapter");
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)),"Device");
    ComPtr<ID3D12InfoQueue> info;Check(device.As(&info),"Info queue");
    std::map<uint64_t,Json::Value> roots;
    for(const auto& row:capture["creations"])if(row["name"]=="ID3D12Device::CreateRootSignature")roots[row["fields"]["pRootSignature"].asUInt64()]=row["fields"];
    std::map<std::pair<UINT,std::vector<uint8_t>>,ComPtr<ID3D12RootSignature>> rootCache;
    UINT cacheHits{};
    auto root=[&](UINT node,const std::vector<uint8_t>& bytes) {
        auto key=std::make_pair(node,bytes);const auto found=rootCache.find(key);
        if(found!=rootCache.end()){++cacheHits;return found->second;}
        ComPtr<ID3D12RootSignature> value;Check(device->CreateRootSignature(node,bytes.data(),bytes.size(),IID_PPV_ARGS(&value)),"Create root");
        rootCache.emplace(std::move(key),value);return value;
    };
    Json::Value result,rows(Json::arrayValue);UINT linked{},failed{},excluded{};std::array<UINT,4> nativeCameras{};
    for(const auto& creation:capture["creations"]) {
        if(creation["name"]!="ID3D12Device::CreateGraphicsPipeline")continue;
        Json::Value row;row["pso"]=creation["fields"]["pPipelineState"];row["runtimeEligible"]=false;
        try {
            const auto& j=Field(creation["fields"],"pDesc");
            bool supported=true;std::vector<PairedBinding> bindings;std::array<std::vector<uint8_t>,2> original,converted;
            for(const char* stage:{"DS","HS","GS"})if(j[stage]["BytecodeLength"].asUInt64())supported=false;
            for(UINT stage=0;stage<2;++stage) {
                const auto& shader=j[stage?"PS":"VS"];const auto& blob=Field(shader,"pShaderBytecode");
                if(!Field(shader,"BytecodeLength").asUInt64()) {if(!stage)supported=false;continue;}
                const auto sha=Field(blob,"sha256").asString();const auto& variant=variants["shaders"][sha];
                if(variant["status"]!="validated" || variant["stage"]!=(stage?"ps":"vs")){supported=false;continue;}
                original[stage]=Read(index.parent_path()/Field(blob,"file").asString(),sha);
                Require(original[stage].size()==shader["BytecodeLength"].asUInt64(),"Native shader size mismatch");
                converted[stage]=Read(paired.parent_path()/variant.get("file",sha+".dxil").asString(),Field(variant,"sha256").asString());
                if(stage && variants.isMember("pipelineShaders")) {
                    const auto& link=variants["pipelineShaders"][std::to_string(row["pso"].asUInt64())];
                    Require(link["status"]=="validated" && link["psOriginal"]==sha &&
                        link["vsOriginal"]==j["VS"]["pShaderBytecode"]["sha256"] &&
                        link["psConvertedSHA"]==variant["sha256"] &&
                        link["vsConvertedSHA"]==variants["shaders"][link["vsOriginal"].asString()]["sha256"],"Missing or mismatched signature link");
                    converted[stage]=Read(paired.parent_path()/Field(link,"file").asString(),Field(link,"sha256").asString());
                    row["signatureLink"]=link["report"];
                }
                for(const auto& p:variant["report"]["pairs"]) {
                    Require(p["sourceSpace"]==0 && p["count"]==2 && p["register"].asUInt()==p["sourceRegister"].asUInt()*2,"Pair ABI mismatch");
                    Require(p["kind"]=="cbuffer" || p["kind"]=="srv","Unsupported pair type");
                    bindings.push_back({p["kind"]=="cbuffer"?D3D12_DESCRIPTOR_RANGE_TYPE_CBV:D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                        stage?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_VERTEX,p["sourceRegister"].asUInt(),p["space"].asUInt()});
                }
            }
            if(!supported){row["status"]="excluded";row["reason"]="Unconverted graphics stage";++excluded;rows.append(row);continue;}
            const auto& r=roots.at(j["pRootSignature"].asUInt64());const auto& blob=Field(r,"pBlobWithRootSignature");
            auto bytes=Read(index.parent_path()/Field(blob,"file").asString(),Field(blob,"sha256").asString());
            NativeDesc native(j);auto& d=native.desc;auto nativeRoot=root(r["nodeMask"].asUInt(),bytes);d.pRootSignature=nativeRoot.Get();
            if(runtime)families::RootCreated(nativeRoot.Get(),r["nodeMask"].asUInt(),bytes.data(),bytes.size());
            d.VS={original[0].data(),original[0].size()};if(!original[1].empty())d.PS={original[1].data(),original[1].size()};
            info->ClearStoredMessages();ComPtr<ID3D12PipelineState> baseline;
            const auto nativeHr=device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&baseline));
            row["nativeHr"]=Hex(nativeHr);row["nativeMessages"]=Messages(info.Get());Check(nativeHr,"Original captured PSO rejected");
            if(runtime) {
                families::GraphicsCreated(device.Get(),d,baseline.Get());auto family=families::Lookup(baseline.Get());
                Require(family && family->pipeline && family->pairedRoot && family->sourceRoot.Get()==nativeRoot.Get(),"Native family association missing");
                Require(family->record<families::Capacity,"Family record unavailable");
                row["runtimeFamily"]=Json::UInt64(family->id);row["runtimeAsset"]=family->asset;
                Require(family->nativeCameraStages<=3 && bool(family->nativeCameraPipeline)==bool(family->nativeCameraStages),"Native camera pipeline/stage contract differs");
                Require(CyberpunkVR_StereoFamilyNativeCameraRecordBytes==16 &&
                    CyberpunkVR_StereoFamilyNativeCameraPipelines[family->record][0]==reinterpret_cast<uintptr_t>(family->nativeCameraPipeline.Get()) &&
                    CyberpunkVR_StereoFamilyNativeCameraPipelines[family->record][1]==family->nativeCameraStages,"Native camera publication differs");
                row["nativeCameraStages"]=family->nativeCameraStages;++nativeCameras[family->nativeCameraStages];
                const auto hitBefore=CyberpunkVR_StereoFamilyHits.load();families::GraphicsCreated(device.Get(),d,baseline.Get());
                Require(CyberpunkVR_StereoFamilyHits.load()==hitBefore+1 && families::Lookup(baseline.Get())->id==family->id,"Native family cache miss");
                ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
                Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Family test allocator");
                Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),baseline.Get(),IID_PPV_ARGS(&list)),"Family test list");
                families::Reset(list.Get(),baseline.Get());list->SetGraphicsRootSignature(nativeRoot.Get());families::RootBound(list.Get(),nativeRoot.Get());
                CyberpunkVR_StereoFamilyObserve=1;families::ObserveDraw(list.Get(),0x23A938,0);families::ObserveDraw(list.Get(),0x23A938,1);
                Require(CyberpunkVR_StereoFamilyRecords[family->record].draws[0]==1 && CyberpunkVR_StereoFamilyRecords[family->record].draws[1]==1,"Bound family census failed");
                families::Reset(list.Get(),nullptr);const auto unmatched=CyberpunkVR_StereoFamilyUnmatched[0].load();
                families::ObserveDraw(list.Get(),0x23A938,0);Require(CyberpunkVR_StereoFamilyUnmatched[0].load()==unmatched+1,"Reset retained stale family");
                CyberpunkVR_StereoFamilyObserve=0;
                families::PipelineBound(list.Get(),baseline.Get());families::RootBound(list.Get(),nativeRoot.Get());
                std::weak_ptr<const families::Family> weak=family;family.reset();baseline.Reset();
                Require(!weak.expired(),"Command-list family lease was lost with original PSO");
                Check(list->Close(),"Family test close");Check(list->Reset(allocator.Get(),nullptr),"Family test reset");
                families::Reset(list.Get(),nullptr);Check(list->Close(),"Family test reset close");list.Reset();
                Require(weak.expired(),"Native family introduced an original-PSO reference cycle");
            }
            PairedRootPlan plan;Check(BuildPairedRoot(bytes.data(),bytes.size(),bindings,plan),"Build paired root plan");
            auto pairedRoot=root(r["nodeMask"].asUInt(),plan.serialized);
            d.pRootSignature=pairedRoot.Get();d.VS={converted[0].data(),converted[0].size()};
            if(!converted[1].empty())d.PS={converted[1].data(),converted[1].size()};
            ViewInstancedLayout views;views.allowMasking=true;ComPtr<ID3D12PipelineState> stereo;
            const auto hr=CreateViewInstancedPipeline(device.Get(),d,views,&stereo);
            row["pairedHr"]=Hex(hr);row["pairedMessages"]=Messages(info.Get());Check(hr,"Paired PSO rejected");
            row["status"]="linked";row["originalParameters"]=plan.originalParameters;row["rootDwords"]=plan.rootDwords;
            row["rootSha256"]=Hash(plan.serialized);row["pairs"]=UINT(plan.bindings.size());
            for(UINT stage=0;stage<2;++stage){row["bankDescriptors"].append(plan.descriptors[stage]);row["bankParameters"].append(plan.parameters[stage]);}
            ++linked;
        } catch(const std::exception& e){row["status"]="failed";row["reason"]=e.what();row["remainingMessages"]=Messages(info.Get());++failed;}
        std::printf("PSO %llu %s\n",row["pso"].asUInt64(),row["status"].asCString());std::fflush(stdout);rows.append(row);
    }
    result["pipelines"]=rows;auto& summary=result["summary"];summary["linked"]=linked;summary["failed"]=failed;summary["excluded"]=excluded;
    summary["rootCacheEntries"]=UINT(rootCache.size());summary["rootCacheHits"]=cacheHits;
    if(runtime){summary["runtimeFamilyRecords"]=CyberpunkVR_StereoFamilyCount.load();summary["runtimeFamilyHits"]=CyberpunkVR_StereoFamilyHits.load();
        summary["runtimeRootHits"]=CyberpunkVR_StereoFamilyRootHits.load();summary["runtimeFailures"]=CyberpunkVR_StereoFamilyFailures.load();
        summary["runtimeBindingFailures"]=CyberpunkVR_StereoFamilyBindingFailures.load();summary["nativeCameraAssets"]=CyberpunkVR_StereoFamilyNativeCameras.load();
        for(const auto count:nativeCameras)summary["nativeCameraPipelinesByStageMask"].append(count);}
    result["limitation"]="Exact native fixed-function/input state and shader linkage only; resources, drawing, temporal correctness and FPS are not tested by PSO creation.";
    std::ofstream file(out);Require(bool(file),"Output path unavailable");file<<result<<'\n';Require(bool(file),"Output write failed");
    std::printf("%s\n",summary.toStyledString().c_str());std::fflush(stdout);
    return failed || !linked || (runtime && (CyberpunkVR_StereoFamilyFailures.load() || CyberpunkVR_StereoFamilyBindingFailures.load()))?1:0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
