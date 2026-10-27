#include "Render/WorldMarkerProjection.hpp"
#include "Render/WorldMarkerOwnership.hpp"
#include "Render/WorldMarkerScale.hpp"
#include "Render/WorldMarkerShader.hpp"
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <array>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
void Log(const char* f,...) { va_list a;va_start(a,f);vprintf(f,a);va_end(a); }
void Check(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
void Hr(HRESULT hr) { if(FAILED(hr)) { char text[64];sprintf_s(text,"D3D12 failure %08X",unsigned(hr));throw std::runtime_error(text); } }
extern "C" {
void* WorldMarkerTestGeometry=nullptr;
float WorldMarkerTestXmm[24]{};
int RunWorldMarkerThunkTest(void*);
void WorldMarkerTestEnd();
}
bool testStackHasCaller=false;
extern "C" void RecordWorldMarkerStack(void* geometry) {
    WorldMarkerTestGeometry=geometry;
    void* frames[24]{};
    const auto count=CaptureStackBackTrace(0,24,frames,nullptr);
    const auto begin=reinterpret_cast<uintptr_t>(&RunWorldMarkerThunkTest);
    const auto end=reinterpret_cast<uintptr_t>(&WorldMarkerTestEnd);
    for(unsigned i=0;i<count;++i) {
        const auto address=reinterpret_cast<uintptr_t>(frames[i]);
        if(address>=begin && address<end) testStackHasCaller=true;
    }
}
struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence> fence;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    uint64_t serial=0;
    Gpu() {
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));
        Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
        D3D12_COMMAND_QUEUE_DESC q{};Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)));
        Hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
        Hr(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd)));
        Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    }
    ~Gpu() { CloseHandle(event); }
    ComPtr<ID3D12Resource> Buffer(size_t size,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;
        d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> r;Hr(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,state,nullptr,IID_PPV_ARGS(&r)));return r;
    }
    void Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};cmd->ResourceBarrier(1,&b);
    }
    void Submit() {
        Hr(cmd->Close());ID3D12CommandList* lists[]={cmd.Get()};queue->ExecuteCommandLists(1,lists);
        Hr(queue->Signal(fence.Get(),++serial));Hr(fence->SetEventOnCompletion(serial,event));
        Check(WaitForSingleObject(event,10000)==WAIT_OBJECT_0,"WARP timeout");
        Hr(allocator->Reset());Hr(cmd->Reset(allocator.Get(),nullptr));
    }
};
struct Vertex { float position[3],uv[2],color[4]; };
using Vertices=std::array<Vertex,6>;
using Camera=std::array<float,212>;
Camera InkCamera(float width=2560,float height=2560) {
    Camera p{};p[0]=2/width;p[3]=-1;p[5]=-2/height;p[7]=1;p[10]=p[15]=1;p[188]=width;p[189]=height;return p;
}
ComPtr<ID3DBlob> Compile(bool procedural) {
    const auto code=cvr::markers::VertexShaderCode(procedural);
    ComPtr<ID3DBlob> shader;Hr(D3DCreateBlob(code.BytecodeLength,&shader));
    std::memcpy(shader->GetBufferPointer(),code.pShaderBytecode,code.BytecodeLength);return shader;
}
std::vector<float> Run(Gpu& gpu,D3D12_SHADER_BYTECODE shader,bool procedural,const Vertices& vertices,
                       const Camera& camera,const std::array<float,28>& object,D3D12_SHADER_BYTECODE pixel={}) {
    D3D12_ROOT_PARAMETER params[4]{};
    const unsigned registers[]={1,5,7};
    for(int i=0;i<3;++i) {params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[i].Descriptor.ShaderRegister=registers[i];params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;}
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,128,0,0,D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[3].DescriptorTable={1,&range};
    // Native ink PS binds its image at t77 and its sampler at s2.
    std::array<D3D12_STATIC_SAMPLER_DESC,16> samplers{};
    for(unsigned i=0;i<samplers.size();++i) {
        auto& sampler=samplers[i];sampler.ShaderRegister=i;sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=FLT_MAX;
    }
    D3D12_ROOT_SIGNATURE_DESC root{4,params,UINT(samplers.size()),samplers.data(),D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT|D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT};
    ComPtr<ID3DBlob> serialized,error;Hr(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error));
    ComPtr<ID3D12RootSignature> signature;Hr(gpu.device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&signature)));
    const D3D12_INPUT_ELEMENT_DESC input[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,20,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    const D3D12_SO_DECLARATION_ENTRY output[]={ {0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0},{0,"TEXCOORD",1,0,BYTE(procedural?4:3),0} };
    const UINT stride=procedural ? 48 : 44;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};d.pRootSignature=signature.Get();d.VS=shader;
    d.StreamOutput={output,3,&stride,1,D3D12_SO_NO_RASTERIZED_STREAM};d.InputLayout={input,3};
    d.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;d.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;d.RasterizerState.DepthClipEnable=TRUE;
    d.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    d.DepthStencilState.FrontFace=d.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    for(auto& b:d.BlendState.RenderTarget) { b.SrcBlend=b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlend=b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15; }
    d.SampleMask=UINT_MAX;d.SampleDesc.Count=1;d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    if(pixel.pShaderBytecode) {
        d.PS=pixel;d.StreamOutput={};d.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets=1;d.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    ComPtr<ID3D12PipelineState> pso;
    const auto pipelineResult=gpu.device->CreateGraphicsPipelineState(&d,IID_PPV_ARGS(&pso));
    if(FAILED(pipelineResult)) {
        ComPtr<ID3D12InfoQueue> messages;
        if(SUCCEEDED(gpu.device.As(&messages))) {
            const auto count=messages->GetNumStoredMessages();
            for(uint64_t i=count>8 ? count-8 : 0;i<count;++i) {
                size_t size=0;messages->GetMessage(i,nullptr,&size);std::vector<char> bytes(size);
                auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
                if(SUCCEEDED(messages->GetMessage(i,message,&size))) std::cerr<<message->pDescription<<'\n';
            }
        }
    }
    Hr(pipelineResult);
    if(pixel.pShaderBytecode) return {}; // Link validation, no unbound PS resources are executed.
    auto upload=gpu.Buffer(4096,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    auto result=gpu.Buffer(4096,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    auto readback=gpu.Buffer(4096,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    uint8_t* data=nullptr;Hr(upload->Map(0,nullptr,reinterpret_cast<void**>(&data)));std::memset(data,0,4096);
    gpu.cmd->CopyResource(result.Get(),upload.Get());gpu.Barrier(result.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_STREAM_OUT);
    // Complete the zero copy before reusing this upload buffer for input data.
    gpu.Submit();
    std::memcpy(data,vertices.data(),sizeof(vertices));std::memcpy(data+256,camera.data(),sizeof(camera));std::memcpy(data+1280,object.data(),sizeof(object));
    float* material=reinterpret_cast<float*>(data+1536);material[4]=.7f;material[5]=.8f;
    gpu.cmd->SetGraphicsRootSignature(signature.Get());gpu.cmd->SetPipelineState(pso.Get());
    gpu.cmd->SetGraphicsRootConstantBufferView(0,upload->GetGPUVirtualAddress()+256);
    gpu.cmd->SetGraphicsRootConstantBufferView(1,upload->GetGPUVirtualAddress()+1280);
    gpu.cmd->SetGraphicsRootConstantBufferView(2,upload->GetGPUVirtualAddress()+1536);
    D3D12_VERTEX_BUFFER_VIEW vb{upload->GetGPUVirtualAddress(),UINT(sizeof(vertices)),UINT(sizeof(Vertex))};
    gpu.cmd->IASetVertexBuffers(0,1,&vb);gpu.cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    D3D12_STREAM_OUTPUT_BUFFER_VIEW so{result->GetGPUVirtualAddress(),4080,result->GetGPUVirtualAddress()+4088};
    gpu.cmd->SOSetTargets(0,1,&so);gpu.cmd->DrawInstanced(6,1,0,0);
    gpu.Barrier(result.Get(),D3D12_RESOURCE_STATE_STREAM_OUT,D3D12_RESOURCE_STATE_COPY_SOURCE);gpu.cmd->CopyResource(readback.Get(),result.Get());gpu.Submit();
    void* bytes=nullptr;Hr(readback->Map(0,nullptr,&bytes));
    Check(*reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(bytes)+4088)==6*stride,"stream output size");
    std::vector<float> out(6*stride/4);std::memcpy(out.data(),bytes,out.size()*4);
    readback->Unmap(0,nullptr);upload->Unmap(0,nullptr);return out;
}
std::array<float,28> Identity() {std::array<float,28> p{};p[0]=p[5]=p[10]=p[15]=1;return p;}
Vertices SampleVertices() {
    Vertices v{};
    for(int i=0;i<6;++i) { v[i]={{350.f+i*170,1200.f+i*45,0},{.05f*i,.8f-.03f*i},{.8f,.3f,.5f,.7f}}; }
    return v;
}
void GpuProjection(bool procedural) {
    Gpu gpu;auto blob=Compile(procedural);D3D12_SHADER_BYTECODE shader{blob->GetBufferPointer(),blob->GetBufferSize()};
    const float depths[]={.6f,2.24207f,2.390405f,2.649214f,9.728778f,1000.f};
    const int stride=procedural?12:11;
    for(bool mainRight:{true,false}) for(float fov:{75.f,108.54f}) for(float zoom:{1.f,4.f}) {
        auto camera=InkCamera();auto original=SampleVertices();auto tagged=original;
        const float f=zoom/std::tan(fov*.00872664626f),ipd=.064f;
        const float sourceEye=mainRight ? ipd*.5f : -ipd*.5f;
        for(int i=0;i<6;++i) {
            const float targetX=.2f*depths[i];
            original[i].position[0]=tagged[i].position[0]=(f*(targetX-sourceEye)/depths[i]+1)*1280;
            tagged[i].position[2]=cvr::markers::EncodeDepth(1/depths[i]);
        }
        const auto main=Run(gpu,shader,procedural,tagged,camera,Identity());
        const auto baseline=Run(gpu,shader,procedural,original,camera,Identity());
        for(size_t i=0;i<main.size();++i) Check(std::abs(main[i]-baseline[i])<2e-6f,"MAIN changed when depth tagged");
        camera[2]=cvr::markers::StereoScale(ipd,fov,1,zoom,mainRight);
        const auto second=Run(gpu,shader,procedural,tagged,camera,Identity());
        for(int i=0;i<6;++i) {
            const float expected=f*(.2f*depths[i]+sourceEye)/depths[i];
            Check(std::abs(second[i*stride]-expected)<2e-5f,"marker misses the other eye's projected world target");
            for(int j=1;j<stride;++j) Check(std::abs(second[i*stride+j]-main[i*stride+j])<2e-6f,"parallax changed vertical position/style/UV");
        }
    }
}
std::vector<char> Read(const std::string& path) {std::ifstream f(path,std::ios::binary);Check(bool(f),"native capture fixture missing");return {std::istreambuf_iterator<char>(f),{}};}
void NativeParity(const std::string& directory) {
    Gpu gpu;
    for(int variant=0;variant<2;++variant) {
        auto original=Read(directory+(variant ? "/shader_92_2.dxbc" : "/shader_104_20.dxbc"));
        D3D12_SHADER_BYTECODE native{original.data(),original.size()},replacement{};
        Check(cvr::markers::VertexShaderReplacement(native,replacement),"native VS fingerprint not recognized");
        auto vertices=SampleVertices();auto object=Identity();object[0]=.8f;object[1]=.1f;object[3]=20;object[4]=-.1f;object[5]=.8f;object[7]=15;
        vertices[5].position[2]=.25f;
        const auto a=Run(gpu,native,variant!=0,vertices,InkCamera(),object);
        const auto b=Run(gpu,replacement,variant!=0,vertices,InkCamera(),object);
        Check(a.size()==b.size(),"signature changed");
        for(size_t i=0;i<a.size();++i) Check(std::abs(a[i]-b[i])<2e-6f,"untagged output differs from captured native shader");
    }
}
void NativeLink(const std::string& directory) {
    Gpu gpu;
    for(int variant=0;variant<2;++variant) {
        auto original=Read(directory+(variant ? "/shader_92_2.dxbc" : "/shader_104_20.dxbc"));
        auto pixel=Read(directory+(variant ? "/shader_92_3.dxbc" : "/shader_104_21.dxbc"));
        D3D12_SHADER_BYTECODE native{original.data(),original.size()},replacement{};
        Check(cvr::markers::VertexShaderReplacement(native,replacement),"native VS fingerprint not recognized");
        std::cout<<"link native variant "<<variant<<'\n';
        Run(gpu,native,variant!=0,SampleVertices(),InkCamera(),Identity(),{pixel.data(),pixel.size()});
        std::cout<<"link replacement variant "<<variant<<'\n';
        Run(gpu,replacement,variant!=0,SampleVertices(),InkCamera(),Identity(),{pixel.data(),pixel.size()});
    }
}
void TextProjection() {
    Gpu gpu;auto blob=Compile(false);D3D12_SHADER_BYTECODE shader{blob->GetBufferPointer(),blob->GetBufferSize()};
    for(float width:{1280.f,2560.f,3840.f}) for(float depth:{2.2f,9.7f,1000.f}) {
        auto object=Identity();object[0]=.8f;object[1]=.1f;object[3]=20;object[4]=-.1f;object[5]=.8f;object[7]=15;
        const auto baseline=Run(gpu,shader,false,SampleVertices(),InkCamera(width,width),object);
        object[11]=cvr::markers::EncodeDepth(1/depth);
        auto main=object;cvr::markers::ReprojectTextMatrix(main.data(),0);
        const auto normal=Run(gpu,shader,false,SampleVertices(),InkCamera(width,width),main);
        for(size_t i=0;i<baseline.size();++i) Check(std::abs(normal[i]-baseline[i])<2e-6f,"text MAIN changed");
        const float scale=cvr::markers::StereoScale(.064f,108.54f,1,1,true);
        cvr::markers::ReprojectTextMatrix(object.data(),scale*width*.5f);
        const auto second=Run(gpu,shader,false,SampleVertices(),InkCamera(width,width),object);
        for(int i=0;i<6;++i) {
            Check(std::abs(second[i*11]-baseline[i*11]-scale/depth)<2e-5f,"text depth differs from icon depth");
            for(int j=1;j<11;++j) Check(std::abs(second[i*11+j]-baseline[i*11+j])<2e-6f,"text style/transform changed");
        }
    }
}
void BatchedTextProjection() {
    // The native composition path publishes the affine transform in a batch;
    // it does not call either of our per-object constant upload detours. Send
    // that encoded matrix straight to the GPU, as the television/NPC text does.
    Gpu gpu;
    for(bool procedural:{false,true}) {
        auto shader=cvr::markers::VertexShaderCode(procedural);
        for(auto size:{std::array<float,2>{1280,1400},{2560,1440},{3840,3840}})
        for(float depth:{.05f,2.2f,9.7f,1000.f}) {
            auto object=Identity();object[0]=.8f;object[1]=.1f;object[3]=20;
            object[4]=-.1f;object[5]=.8f;object[7]=15;
            auto camera=InkCamera(size[0],size[1]);
            const auto baseline=Run(gpu,shader,procedural,SampleVertices(),camera,object);
            object[11]=cvr::markers::EncodeDepth(1/depth);
            const auto main=Run(gpu,shader,procedural,SampleVertices(),camera,object);
            for(size_t i=0;i<baseline.size();++i)
                Check(std::abs(main[i]-baseline[i])<2e-6f,"batched text depth tag clips MAIN text");
            for(bool mainRight:{false,true}) {
                const float scale=cvr::markers::StereoScale(.064f,108.54f,size[0]/size[1],1,mainRight);
                camera[2]=scale;
                const auto second=Run(gpu,shader,procedural,SampleVertices(),camera,object);
                const int stride=procedural ? 12 : 11;
                for(int v=0;v<6;++v) {
                    Check(std::abs(second[v*stride]-baseline[v*stride]-scale/depth)<2e-5f,
                          "batched text depth differs from icon depth");
                    for(int k=1;k<stride;++k)
                        Check(std::abs(second[v*stride+k]-baseline[v*stride+k])<2e-6f,
                              "batched text changed clip depth, UV or color");
                }
            }
        }
    }
}
int main(int argc,char** argv) try {
    Check(argc>=2,"case required");const std::string name=argv[1];
    using namespace cvr::markers;
    if(name=="marker_size") {
        MarkerScale size;float x=0,y=0;
        Check(size.Target(1,1,x,y) && x==.7f && y==.7f,"world marker scale is not 0.7");
        size.Commit(x,y);
        for(int i=0;i<10000;++i)Check(!size.Target(.7f,.7f,x,y),"marker shrinks on every position update");
        Check(size.Target(1.4f,.8f,x,y) && std::abs(x-.98f)<1e-6f && std::abs(y-.56f)<1e-6f,"game-authored scale update lost");
        size.Commit(x,y);Check(!size.Target(size.appliedX,size.appliedY,x,y),"nonuniform scale compounded");
        Check(!size.Target(std::numeric_limits<float>::quiet_NaN(),1,x,y),"invalid widget scale accepted");
        size={};Check(size.Target(.5f,.5f,x,y) && x==.35f && y==.35f,"new widget inherited previous root ownership");
    } else if(name=="effect_owner") {
        MarkerOwner owner;
        Check(MergeEffectOwner(owner,{17,.5f}) && MergeEffectOwner(owner,{17,.5f}),"one marker's flattened effect loses ownership");
        Check(owner.root==17 && owner.depth==.5f,"effect changes marker depth");
        Check(!MergeEffectOwner(owner,{18,.5f}),"unrelated markers at the same depth merged");
        Check(!MergeEffectOwner(owner,{0,0}),"ordinary HUD content inherited marker projection");
        Check(!MergeEffectOwner(owner,{17,std::numeric_limits<float>::quiet_NaN()}),"invalid effect depth accepted");
        MarkerOwner outer;Check(MergeEffectOwner(outer,owner) && outer.root==17,"nested effect loses original widget owner");
    } else if(name=="depth") {
        const float eye[]={-1830,-2354,31},q[]={0,0,0,1};
        for(float depth:{.05f,2.f,10.f,1000.f}) {
            const float point[]={eye[0]+100,eye[1]+depth,eye[2]-20};
            Check(std::abs(ForwardDepth(point,eye,q)-depth)<.0002f,"radial distance used instead of forward depth");
            Check(std::abs(DecodeDepth(EncodeDepth(1/depth))-1/depth)<2e-6f,"depth encoding precision");
        }
        const float back[]={-1830,-2355,31};Check(ForwardDepth(back,eye,q)==0,"behind-camera marker accepted");
        Check(EncodeDepth(std::numeric_limits<float>::quiet_NaN())==0 && EncodeDepth(-1)==0,"invalid marker depth accepted");
        const float yaw[]={0,0,.70710678f,.70710678f},point[]={-1835,-2354,31};
        Check(std::abs(ForwardDepth(point,eye,yaw)-5)<1e-5f,"rotated camera depth");
    } else if(name=="camera") {
        auto p=InkCamera();Check(IsInkCamera(p.data(),sizeof(p)),"ink camera missed");
        p[0]=.719f;Check(!IsInkCamera(p.data(),sizeof(p)),"world perspective camera accepted");
        p=InkCamera();p[3]=0;Check(!IsInkCamera(p.data(),sizeof(p)),"world/shadow matrix accepted");
        p=InkCamera();p[0]=std::numeric_limits<float>::quiet_NaN();Check(!IsInkCamera(p.data(),sizeof(p)),"invalid camera accepted");
        // The engine's block carries the canvas matrix at floats 0, 32, 48, 64 and 112; identity elsewhere.
        p=InkCamera();
        for(size_t m:{16,80,96,128})p[m]=p[m+5]=p[m+10]=p[m+15]=1;
        for(size_t m:{32,48,64,112})for(size_t i=0;i<16;++i)p[m+i]=p[i];
        auto out=p;
        Check(ShiftInkCamera(out.data(),p.data(),sizeof(p),.9f,.25f,-.1f)==4,"canvas matrix copies missed");
        for(size_t m:{0,32,48,64,112}) {
            Check(out[m]==p[0]*.9f && out[m+5]==p[5] && std::abs(out[m+3]-(-.9f+.25f))<1e-6f &&
                  std::abs(out[m+7]-.9f)<1e-6f,"copy not moved like the first matrix");
        }
        for(size_t m:{16,80,96,128})for(size_t i=0;i<16;++i)Check(out[m+i]==p[m+i],"non-canvas matrix changed");
        Check(out[188]==p[188] && out[189]==p[189],"canvas size changed");
    } else if(name=="sprite" || name=="procedural") GpuProjection(name=="procedural");
    else if(name=="text") TextProjection();
    else if(name=="text_batched") BatchedTextProjection();
    else if(name=="native") {Check(argc==3,"native shader directory required");NativeParity(argv[2]);}
    else if(name=="native_link") {Check(argc==3,"native shader directory required");NativeLink(argv[2]);}
    else if(name=="abi") {
        int geometry=123;Check(RunWorldMarkerThunkTest(&geometry)==1,"mid-function thunk changed native volatile registers/flags");
        Check(WorldMarkerTestGeometry==&geometry,"wrong native geometry register");
        Check(testStackHasCaller,"text thunk lost the native caller during stack unwinding");
        for(float x:WorldMarkerTestXmm) Check(x==1,"mid-function thunk changed XMM lanes");
    } else Check(false,"unknown case");
    std::cout<<"PASS "<<name<<'\n';return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
