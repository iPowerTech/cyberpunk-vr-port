#include "Render/NativeShaderSnapshot.hpp"
#include "Render/RenderProbeScope.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace cvr::stereo::shader_snapshot;
namespace fixture {bool observing=true;}
namespace cvr::stereo::native_probe {bool ObserveBindings(){return fixture::observing;}}
static void Require(bool value){if(!value)throw std::runtime_error("Shader snapshot assertion failed");}
static void Check(HRESULT result){if(FAILED(result))throw std::runtime_error("D3D12 shader snapshot fixture failed: "+std::to_string(unsigned(result)));}
static std::vector<uint8_t> Bytes(ID3DBlob* blob){
    const auto* begin=static_cast<const uint8_t*>(blob->GetBufferPointer());return {begin,begin+blob->GetBufferSize()};
}
static ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* target){
    ComPtr<ID3DBlob> result,error;
    Check(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,&result,&error));return result;
}
template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Tag,class T>struct alignas(void*) StreamPart {
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type=Tag;T value{};
};
struct CreationStream {
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,ID3D12RootSignature*> root;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,D3D12_SHADER_BYTECODE> vs;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,D3D12_SHADER_BYTECODE> ps;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS,D3D12_SHADER_BYTECODE> hs;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS,D3D12_SHADER_BYTECODE> ds;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS,D3D12_SHADER_BYTECODE> gs;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,D3D12_SHADER_BYTECODE> cs;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS,D3D12_SHADER_BYTECODE> as;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS,D3D12_SHADER_BYTECODE> ms;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,D3D12_INPUT_LAYOUT_DESC> input;
    StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT,D3D12_STREAM_OUTPUT_DESC> output;
    explicit CreationStream(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& source){
        root.value=source.pRootSignature;vs.value=source.VS;ps.value=source.PS;hs.value=source.HS;ds.value=source.DS;gs.value=source.GS;
        input.value=source.InputLayout;output.value=source.StreamOutput;
    }
    cvr::stereo::PipelineStreamInfo Parse()const{
        cvr::stereo::PipelineStreamInfo result;
        Require(cvr::stereo::ReadPipelineStream({sizeof(*this),const_cast<CreationStream*>(this)},result));return result;
    }
};
struct Lifetime final:IUnknown {
    std::atomic<ULONG> refs{1};std::shared_ptr<std::atomic<bool>> released;
    explicit Lifetime(std::shared_ptr<std::atomic<bool>> value):released(std::move(value)){}
    ~Lifetime(){*released=true;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(id!=__uuidof(IUnknown))return E_NOINTERFACE;*out=this;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
static std::shared_ptr<std::atomic<bool>> Watch(ID3D12Object* object){
    constexpr GUID key{0x3956a431,0x5caf,0x4b16,{0x94,0x48,0x5f,0xda,0xb1,0x9d,0xda,0xb2}};
    auto released=std::make_shared<std::atomic<bool>>(false);ComPtr<Lifetime> marker;marker.Attach(new Lifetime(released));
    Check(object->SetPrivateDataInterface(key,marker.Get()));return released;
}
int main()try{
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    constexpr char source[]=R"(
float4 vertex(float3 position : POSITION) : SV_Position { return float4(position, 1); }
float4 pixel() : SV_Target0 { return float4(0.25, 0.5, 0.75, 1); }
)";
    auto vertex=Compile(source,"vertex","vs_5_0"),pixel=Compile(source,"pixel","ps_5_0");
    const auto originalVs=Bytes(vertex.Get()),originalPs=Bytes(pixel.Get());
    auto vs=originalVs,ps=originalPs;
    std::array<char,256> semantic{};std::memcpy(semantic.data(),"POSITION",9);
    D3D12_INPUT_ELEMENT_DESC element{semantic.data(),0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    const D3D12_INPUT_LAYOUT_DESC input{&element,1};
    UINT rootOrdinal=0,pipelineOrdinal=0;
    auto makeRoot=[&](std::vector<uint8_t>& serialized,bool streamOutput=false){
        D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants={0,0,++rootOrdinal};parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        const auto flags=D3D12_ROOT_SIGNATURE_FLAGS(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            (streamOutput?D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT:0));
        const D3D12_ROOT_SIGNATURE_DESC desc{1,&parameter,0,nullptr,flags};
        ComPtr<ID3DBlob> bytes,error;Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));
        serialized=Bytes(bytes.Get());ComPtr<ID3D12RootSignature> root;
        Check(device->CreateRootSignature(0,serialized.data(),serialized.size(),IID_PPV_ARGS(&root)));return root;
    };
    auto makePipeline=[&](ID3D12RootSignature* root,D3D12_GRAPHICS_PIPELINE_STATE_DESC* created=nullptr,
                          D3D12_SHADER_BYTECODE geometry={},D3D12_STREAM_OUTPUT_DESC streamOutput={}){
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root;
        desc.VS={originalVs.data(),originalVs.size()};desc.PS={originalPs.data(),originalPs.size()};desc.InputLayout=input;
        desc.GS=geometry;desc.StreamOutput=streamOutput;
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;desc.SampleDesc.Count=1;desc.SampleMask=UINT_MAX;
        desc.NumRenderTargets=1;desc.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE;desc.RasterizerState.DepthBias=INT(++pipelineOrdinal);
        auto& blend=desc.BlendState.RenderTarget[0];blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.FrontFace=desc.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
        ComPtr<ID3D12PipelineState> pipeline;Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pipeline)));
        if(created)*created=desc;return pipeline;
    };
    std::vector<uint8_t> rootBytes;auto root=makeRoot(rootBytes);const auto originalRoot=rootBytes;
    auto pipeline=makePipeline(root.Get());auto rootReleased=Watch(root.Get()),pipelineReleased=Watch(pipeline.Get());
    Require(!Capture(pipeline.Get()));RootCreated(root.Get(),rootBytes.data(),rootBytes.size());
    rootBytes[0]^=0xff;
    GraphicsCreated(pipeline.Get(),root.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);
    auto snapshot=Capture(pipeline.Get());Require(snapshot && snapshot->id && snapshot->vs==originalVs && snapshot->ps==originalPs && snapshot->root==originalRoot);
    Require(snapshot->creation==CreationContract{} && !snapshot->creation.KnownVsPsOnly());
    Require(snapshot->input.size()==1);const auto& saved=snapshot->input[0];
    Require(saved.semantic=="POSITION" && saved.index==0 && saved.format==UINT(DXGI_FORMAT_R32G32B32_FLOAT) &&
        saved.slot==0 && saved.offset==0 && saved.classification==UINT(D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA) && saved.step==0);
    vs[0]^=0xff;ps[0]^=0xff;semantic[0]='X';element.SemanticIndex=7;element.Format=DXGI_FORMAT_R32G32_FLOAT;
    element.InputSlot=3;element.AlignedByteOffset=64;element.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;element.InstanceDataStepRate=2;
    RootCreated(root.Get(),rootBytes.data(),rootBytes.size());
    GraphicsCreated(pipeline.Get(),root.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);
    Require(Capture(pipeline.Get()).get()==snapshot.get());
    Require(snapshot->vs==originalVs && snapshot->ps==originalPs && snapshot->root==originalRoot && saved.semantic=="POSITION" &&
        saved.index==0 && saved.format==UINT(DXGI_FORMAT_R32G32B32_FLOAT) && saved.slot==0 && saved.offset==0 && saved.classification==0 && saved.step==0);
    const auto id=snapshot->id;pipeline.Reset();root.Reset();Require(*pipelineReleased && *rootReleased);
    Require(snapshot->id==id && snapshot->vs==originalVs && snapshot->root==originalRoot);
    semantic[0]='P';element={semantic.data(),0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0};
    vs=originalVs;ps=originalPs;

    std::vector<uint8_t> negativeRootBytes;auto negativeRoot=makeRoot(negativeRootBytes);
    auto noRoot=makePipeline(negativeRoot.Get());
    GraphicsCreated(noRoot.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);Require(!Capture(noRoot.Get()));
    RootCreated(negativeRoot.Get(),negativeRootBytes.data(),MaxRootBytes+1);
    GraphicsCreated(noRoot.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);Require(!Capture(noRoot.Get()));
    RootCreated(negativeRoot.Get(),negativeRootBytes.data(),negativeRootBytes.size());
    auto badShader=makePipeline(negativeRoot.Get());
    GraphicsCreated(badShader.Get(),negativeRoot.Get(),{vs.data(),MaxShaderBytes+1},{ps.data(),ps.size()},input);Require(!Capture(badShader.Get()));
    auto badPixel=makePipeline(negativeRoot.Get());
    GraphicsCreated(badPixel.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),MaxShaderBytes+1},input);Require(!Capture(badPixel.Get()));
    auto badInput=makePipeline(negativeRoot.Get());const D3D12_INPUT_LAYOUT_DESC oversized{&element,MaxInputs+1};
    GraphicsCreated(badInput.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},oversized);Require(!Capture(badInput.Get()));
    auto badSemantic=makePipeline(negativeRoot.Get());element.SemanticName=nullptr;
    GraphicsCreated(badSemantic.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);Require(!Capture(badSemantic.Get()));
    element.SemanticName=semantic.data();auto gated=makePipeline(negativeRoot.Get());
    {cvr::stereo::probe::Scope internal;GraphicsCreated(gated.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);}
    Require(!Capture(gated.Get()));fixture::observing=false;
    GraphicsCreated(gated.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);fixture::observing=true;Require(!Capture(gated.Get()));
    GraphicsCreated(gated.Get(),negativeRoot.Get(),{vs.data(),vs.size()},{ps.data(),ps.size()},input);
    const auto later=Capture(gated.Get());Require(later && later->id>id && later->vs==originalVs);
    Require(later->creation==CreationContract{});
    {cvr::stereo::probe::Scope internal;Require(!Capture(gated.Get()));}
    fixture::observing=false;Require(!Capture(gated.Get()));fixture::observing=true;Require(Capture(gated.Get())==later);
    Require(DescribeCreation({})==CreationContract{});
    Require(DescribeStreamCreation({})==CreationContract{});
    D3D12_GRAPHICS_PIPELINE_STATE_DESC direct{};auto directPipeline=makePipeline(negativeRoot.Get(),&direct);
    GraphicsCreated(directPipeline.Get(),direct);const auto full=Capture(directPipeline.Get());
    Require(full && full->creation.KnownVsPsOnly() && full->creation.streamOutput==StreamOutputPresence::Absent &&
        full->vs==originalVs && full->ps==originalPs);
    for(uint32_t stage=0;stage<3;++stage){auto other=direct;
        (stage==0?other.HS:stage==1?other.DS:other.GS)=direct.VS;
        const auto classified=DescribeCreation(other);
        Require(classified.stages==StageCompleteness::OtherStages && !classified.KnownVsPsOnly() &&
            classified.streamOutput==StreamOutputPresence::Absent);
    }
    auto noPixel=direct;noPixel.PS={};Require(DescribeCreation(noPixel).KnownVsPsOnly());
    auto missingVertex=direct;missingVertex.VS={};Require(DescribeCreation(missingVertex)==CreationContract{});
    auto missingPixel=direct;missingPixel.PS.pShaderBytecode=nullptr;Require(DescribeCreation(missingPixel)==CreationContract{});
    auto changed=direct;changed.GS=direct.VS;changed.StreamOutput.NumEntries=1;
    GraphicsCreated(directPipeline.Get(),changed);
    Require(Capture(directPipeline.Get())==full && full->creation.KnownVsPsOnly() && full->creation.streamOutput==StreamOutputPresence::Absent);
    // A legacy snapshot is never silently promoted by a later metadata claim.
    GraphicsCreated(gated.Get(),direct);Require(Capture(gated.Get())==later && later->creation==CreationContract{});

    constexpr char geometrySource[]=R"(
struct Vertex { float4 position : SV_Position; };
[maxvertexcount(3)]
void geometry(triangle Vertex input[3], inout TriangleStream<Vertex> output) {
    output.Append(input[0]); output.Append(input[1]); output.Append(input[2]);
}
)";
    auto gs=Compile(geometrySource,"geometry","gs_5_0");D3D12_GRAPHICS_PIPELINE_STATE_DESC geometryDesc{};
    auto geometryPipeline=makePipeline(negativeRoot.Get(),&geometryDesc,{gs->GetBufferPointer(),gs->GetBufferSize()});
    GraphicsCreated(geometryPipeline.Get(),geometryDesc);const auto geometrySnapshot=Capture(geometryPipeline.Get());
    Require(geometrySnapshot && geometrySnapshot->creation.stages==StageCompleteness::OtherStages &&
        geometrySnapshot->creation.streamOutput==StreamOutputPresence::Absent && geometrySnapshot->vs==originalVs && geometrySnapshot->ps==originalPs);

    std::vector<uint8_t> streamRootBytes;auto streamRoot=makeRoot(streamRootBytes,true);
    RootCreated(streamRoot.Get(),streamRootBytes.data(),streamRootBytes.size());
    const D3D12_SO_DECLARATION_ENTRY declaration{0,"SV_Position",0,0,4,0};const UINT stride=16;
    const D3D12_STREAM_OUTPUT_DESC streamOutput{&declaration,1,&stride,1,0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC outputDesc{};
    auto outputPipeline=makePipeline(streamRoot.Get(),&outputDesc,{},streamOutput);
    GraphicsCreated(outputPipeline.Get(),outputDesc);const auto outputSnapshot=Capture(outputPipeline.Get());
    Require(outputSnapshot && outputSnapshot->creation.KnownVsPsOnly() && outputSnapshot->creation.streamOutput==StreamOutputPresence::Present);
    for(uint32_t shape=0;shape<2;++shape){auto output=direct;
        if(shape)output.StreamOutput.NumStrides=1;else output.StreamOutput.NumEntries=1;
        Require(DescribeCreation(output).streamOutput==StreamOutputPresence::Present);
    }

    // The stream contract comes from the real parser, including explicit empty
    // stages and the distinction between absent and nonempty stream output.
    auto parsed=CreationStream(direct).Parse();
    auto parsedPipeline=makePipeline(negativeRoot.Get());GraphicsStreamCreated(parsedPipeline.Get(),parsed);
    const auto parsedSnapshot=Capture(parsedPipeline.Get());
    Require(parsedSnapshot && parsedSnapshot->creation.KnownVsPsOnly() && parsedSnapshot->creation.streamOutput==StreamOutputPresence::Absent);
    for(uint32_t stage=0;stage<7;++stage)for(uint32_t output=0;output<3;++output){
        CreationStream stream(direct);
        const std::array<D3D12_SHADER_BYTECODE*,6> others{&stream.hs.value,&stream.ds.value,&stream.gs.value,
            &stream.cs.value,&stream.as.value,&stream.ms.value};
        if(stage)*others[stage-1]=direct.VS;
        if(output==1)stream.output.value.NumEntries=1;
        if(output==2)stream.output.value.NumStrides=1;
        const auto metadata=stream.Parse();const auto classified=DescribeStreamCreation(metadata);
        Require(metadata.otherStages==bool(stage) && metadata.streamOutput==bool(output) &&
            classified.stages==(stage?StageCompleteness::OtherStages:StageCompleteness::VsPsOnly) &&
            classified.streamOutput==(output?StreamOutputPresence::Present:StreamOutputPresence::Absent));
    }
    struct MinimalStream {
        StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,ID3D12RootSignature*> root;
        StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,D3D12_SHADER_BYTECODE> vs;
        StreamPart<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,D3D12_SHADER_BYTECODE> ps;
    } minimal;minimal.root.value=direct.pRootSignature;minimal.vs.value=direct.VS;minimal.ps.value=direct.PS;
    cvr::stereo::PipelineStreamInfo absentOutput;
    Require(cvr::stereo::ReadPipelineStream({sizeof(minimal),&minimal},absentOutput) &&
        DescribeStreamCreation(absentOutput).streamOutput==StreamOutputPresence::Absent);
    auto parsedOutput=CreationStream(outputDesc).Parse();D3D12_GRAPHICS_PIPELINE_STATE_DESC anotherOutputDesc{};
    auto parsedOutputPipeline=makePipeline(streamRoot.Get(),&anotherOutputDesc,{},streamOutput);
    GraphicsStreamCreated(parsedOutputPipeline.Get(),parsedOutput);const auto parsedOutputSnapshot=Capture(parsedOutputPipeline.Get());
    Require(parsedOutputSnapshot && parsedOutputSnapshot->creation.KnownVsPsOnly() &&
        parsedOutputSnapshot->creation.streamOutput==StreamOutputPresence::Present);
    CreationStream invalidStream(direct);cvr::stereo::PipelineStreamInfo rejected;
    Require(!cvr::stereo::ReadPipelineStream({sizeof(invalidStream)-1,&invalidStream},rejected));
    invalidStream.gs.type=D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
    Require(!cvr::stereo::ReadPipelineStream({sizeof(invalidStream),&invalidStream},rejected));
    invalidStream.gs.type=static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(63);
    Require(!cvr::stereo::ReadPipelineStream({sizeof(invalidStream),&invalidStream},rejected));
    CreationStream changedStream(outputDesc);changedStream.gs.value=geometryDesc.GS;
    GraphicsStreamCreated(parsedPipeline.Get(),changedStream.Parse());
    Require(Capture(parsedPipeline.Get())==parsedSnapshot && parsedSnapshot->creation.KnownVsPsOnly() &&
        parsedSnapshot->creation.streamOutput==StreamOutputPresence::Absent);
    parsed.vs={};Require(DescribeStreamCreation(parsed)==CreationContract{});
    auto pinned=Watch(directPipeline.Get());directPipeline.Reset();Require(*pinned && full->creation.KnownVsPsOnly());
    std::puts("PASS immutable native shader/input/root snapshots, explicit stage/SO provenance, COM lifetime, limits and gates; WARP without GPU commands");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
