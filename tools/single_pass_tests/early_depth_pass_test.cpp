#include "Render/EarlyDepthPass.hpp"
#include "Render/NativeStereoCameraUpload.hpp"
#include "Render/NativeMaterialRead.hpp"
namespace cvr::stereo::camera_upload {Context CurrentContext(uintptr_t){return {};}}
#include "Render/EarlyDepthLimits.hpp"
#include "Render/DepthCommitGate.hpp"
#include "Render/ViewInstancedPipeline.hpp"
#include "Render/CommandResources.hpp"
#include "Render/StereoTargetArray.hpp"
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <thread>
#include <string>
using namespace cvr::stereo;
using Microsoft::WRL::ComPtr;
namespace cvr::stereo::native_probe {bool Enabled(){return true;}bool ObserveBindings(){return true;}}
extern "C" {std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};}
void Check(HRESULT h){if(FAILED(h))throw std::runtime_error("D3D12 failed "+std::to_string(unsigned(h)));}
void Require(bool value){if(!value)throw std::runtime_error("early depth assertion failed");}
namespace skinning_fixture {
ID3D12Resource* texture{};
ID3D12Resource* executionMarkers{},*markerValues{};UINT eye{};
D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COPY_DEST;
UINT attempts{},copies{};uint32_t failure{};bool failNext{},receiptValid=true;
}
namespace alpha_fixture {
ID3D12Resource* material{},*texture{};
std::array<bool,2> valid{true,true};
UINT copies{},textureCopies{};
}
namespace cvr::stereo::material_read {
uint32_t LastFailure(){return 0;}
bool CopySnapshot(ID3D12GraphicsCommandList* list,const material_binding::Lease& source,ID3D12Resource* destination,
                  uint64_t offset,buffer_read::Proof& proof){
    Require(list && source.source.resource.Get()==alpha_fixture::material && destination && skinning_fixture::eye<2);
    proof={};proof.receipt=1;proof.recording=reinterpret_cast<uintptr_t>(list);proof.generation=skinning_fixture::eye+1;
    list->CopyBufferRegion(destination,offset,source.source.resource.Get(),source.sourceOffset,material_binding::LogicalBytes);
    ++alpha_fixture::copies;return true;
}
bool ValidateSnapshotSubmission(const material_binding::Lease& source,const buffer_read::Proof& proof,ID3D12CommandQueue* queue){
    return queue && source.source.resource.Get()==alpha_fixture::material && proof.receipt==1 &&
        proof.generation>=1 && proof.generation<=2 && alpha_fixture::valid[proof.generation-1];
}
}
namespace cvr::stereo::texture_read {
// Native registration is mocked here. Copies, transitions, immutable snapshots,
// comparisons and predicated draws still execute on the D3D12 test device.
uint32_t LastFailure(){return skinning_fixture::failure;}
bool Validate(const Lease& source,ID3D12CommandQueue*){
    return (alpha_fixture::texture && source.resource.Get()==alpha_fixture::texture) ||
        (source.resource.Get()==skinning_fixture::texture && skinning_fixture::receiptValid);
}
bool CopySubresource(ID3D12GraphicsCommandList* list,const Lease& source,UINT mip,ID3D12Resource* destination,const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout){
    Require(list && source.resource.Get()==alpha_fixture::texture && destination && mip<source.resource->GetDesc().MipLevels);
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=source.resource.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=mip;
    to.pResource=destination;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);++alpha_fixture::textureCopies;return true;
}
bool Copy(ID3D12GraphicsCommandList* list,const Lease& source,ID3D12Resource* destination,const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout){
    using namespace skinning_fixture;++attempts;failure=0;
    Require(list && source.resource.Get()==texture && destination);
    if(failNext){failNext=false;failure=41;return false;}
    constexpr auto shaders=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    Require((state&shaders)==shaders);
    if(!(state&D3D12_RESOURCE_STATE_COPY_SOURCE)){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={texture,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,state|D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);state|=D3D12_RESOURCE_STATE_COPY_SOURCE;}
    D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=texture;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource=destination;to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layout;
    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    // Inherit the production snapshot predicate; CPU call counts cannot prove
    // that a recorded copy was skipped when its permit changes at submission.
    if(executionMarkers){Require(eye<2 && markerValues);list->CopyBufferRegion(executionMarkers,UINT64(eye)*8,markerValues,8,8);}
    ++copies;return true;
}
}
std::vector<uint8_t> File(const char* path){std::ifstream f(path,std::ios::binary);Require(bool(f));return {std::istreambuf_iterator<char>(f),{}};}
ComPtr<ID3D12Resource> Upload(ID3D12Device* d,const void* data,UINT bytes){
    D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
    r.Width=bytes;r.Height=r.DepthOrArraySize=r.MipLevels=r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;Check(d->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)));
    void* mapped{};Check(result->Map(0,nullptr,&mapped));std::memcpy(mapped,data,bytes);result->Unmap(0,nullptr);return result;
}
// The existing geometry shaders stay unchanged: this fixture verifies capture,
// admission and native-depth fallback, not the production skinning shader math.
struct SkinningCheck {
    static constexpr UINT Width=7,Height=3;
    std::array<uint8_t,128> boneBytes{};
    std::array<skinning::BoneRange,2> ranges{{{16,16},{64,32}}};
    ComPtr<ID3D12Resource> bones,image,initialPixels,changedPixels;
    ComPtr<ID3D12Resource> markers,markerUpload,markerReadback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    texture_read::Lease lease;
    void Create(ID3D12Device* device){
        for(UINT i=0;i<boneBytes.size();++i)boneBytes[i]=uint8_t(i*3+1);
        bones=Upload(device,boneBytes.data(),UINT(boneBytes.size()));
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=Width;desc.Height=Height;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8_UNORM;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&image)));
        UINT rows{};UINT64 rowBytes{},total{};device->GetCopyableFootprints(&desc,0,1,0,&layout,&rows,&rowBytes,&total);
        Require(rows==Height && rowBytes==Width && total!=UINT64_MAX);
        std::vector<uint8_t> pixels(size_t(layout.Footprint.RowPitch)*Height,0xad);
        for(UINT y=0;y<Height;++y)for(UINT x=0;x<Width;++x)pixels[size_t(y)*layout.Footprint.RowPitch+x]=uint8_t(y*Width+x+1);
        initialPixels=Upload(device,pixels.data(),UINT(pixels.size()));
        pixels[size_t(Height-1)*layout.Footprint.RowPitch+Width-1]^=0x80;
        changedPixels=Upload(device,pixels.data(),UINT(pixels.size()));
        lease.resource=image;skinning_fixture::texture=image.Get();skinning_fixture::state=D3D12_RESOURCE_STATE_COPY_DEST;
    }
    void CreateMarkers(ID3D12Device* device){
        const uint64_t values[]{0,1};markerUpload=Upload(device,values,sizeof(values));
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=sizeof(values);desc.Height=1;
        desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&markers)));
        heap.Type=D3D12_HEAP_TYPE_READBACK;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&markerReadback)));
        skinning_fixture::executionMarkers=markers.Get();skinning_fixture::markerValues=markerUpload.Get();
    }
    void ResetMarkers(ID3D12GraphicsCommandList* list){
        for(UINT side=0;side<2;++side)list->CopyBufferRegion(markers.Get(),UINT64(side)*8,markerUpload.Get(),0,8);
    }
    void ReadMarkers(ID3D12GraphicsCommandList* list){
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={markers.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};
        list->ResourceBarrier(1,&barrier);list->CopyResource(markerReadback.Get(),markers.Get());
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
    }
    void ValidateMarkers(std::array<uint64_t,2> expected){
        void* mapped{};const D3D12_RANGE read{0,16};Check(markerReadback->Map(0,&read,&mapped));Require(mapped!=nullptr);
        std::array<uint64_t,2> actual{};std::memcpy(actual.data(),mapped,16);const D3D12_RANGE none{};markerReadback->Unmap(0,&none);
        Require(actual==expected);std::printf("GPU snapshot-copy execution: MAIN %llu, peer %llu\n",actual[0],actual[1]);
    }
    void Reset(){
        void* mapped{};Check(bones->Map(0,nullptr,&mapped));std::memcpy(mapped,boneBytes.data(),boneBytes.size());bones->Unmap(0,nullptr);
        skinning_fixture::attempts=skinning_fixture::copies=skinning_fixture::failure=0;skinning_fixture::failNext=false;skinning_fixture::receiptValid=true;
    }
    void ChangeBones(){
        void* mapped{};Check(bones->Map(0,nullptr,&mapped));static_cast<uint8_t*>(mapped)[95]^=0x80;bones->Unmap(0,nullptr);
    }
    void WriteTexture(ID3D12GraphicsCommandList* list,bool changed=false){
        auto& state=skinning_fixture::state;D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={image.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,D3D12_RESOURCE_STATE_COPY_DEST};
        if(state!=D3D12_RESOURCE_STATE_COPY_DEST)list->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION from{},to{};from.pResource=changed?changedPixels.Get():initialPixels.Get();
        from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=layout;
        to.pResource=image.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&barrier);state=barrier.Transition.StateAfter;
    }
};
struct OutputCheck {
    std::array<StereoTargetArray,2> arrays;
    ComPtr<ID3D12Resource> readback;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,8> layouts;
    std::array<UINT,8> strides{},components{};
    UINT64 bytes{};
    void Create(ID3D12Device* device,D3D12_RESOURCE_DESC desc){
        for(auto& a:arrays)Check(a.Initialize(device,desc,D3D12_RESOURCE_STATE_DEPTH_WRITE));
        desc.DepthOrArraySize=2;
        for(UINT plane=0;plane<2;++plane)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<2;++mode){
            const auto index=plane*4+eye*2+mode;auto& layout=layouts[index];UINT rows{};UINT64 rowBytes{},size{};
            device->GetCopyableFootprints(&desc,eye+plane*2,1,bytes,&layout,&rows,&rowBytes,&size);
            Require(size!=UINT64_MAX && rowBytes%desc.Width==0);strides[index]=UINT(rowBytes/desc.Width);
            Require(plane?(strides[index]==1 || strides[index]==8):(strides[index]==4 || strides[index]==8));
            components[index]=plane && strides[index]==8?4:0;
            bytes=(layout.Offset+UINT64(layout.Footprint.RowPitch)*rows+511)&~UINT64(511);
        }
        D3D12_RESOURCE_DESC b{};b.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;b.Width=bytes;b.Height=1;b.DepthOrArraySize=b.MipLevels=1;
        b.SampleDesc.Count=1;b.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
        Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    }
    void Capture(ID3D12GraphicsCommandList* list,UINT eye,ID3D12Resource* actual,ID3D12Resource* expected){
        Check(arrays[0].ImportEye(list,eye,D3D12_RESOURCE_STATE_DEPTH_WRITE,actual,D3D12_RESOURCE_STATE_DEPTH_WRITE));
        Check(arrays[1].ImportEye(list,eye,D3D12_RESOURCE_STATE_DEPTH_WRITE,expected,D3D12_RESOURCE_STATE_DEPTH_WRITE));
    }
    void Copy(ID3D12GraphicsCommandList* list){
        for(auto& a:arrays){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition={a.Resource(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&b);}
        for(UINT plane=0;plane<2;++plane)for(UINT eye=0;eye<2;++eye)for(UINT mode=0;mode<2;++mode){D3D12_TEXTURE_COPY_LOCATION from{},to{};
            from.pResource=arrays[mode].Resource();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=eye+plane*2;
            to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=layouts[plane*4+eye*2+mode];
            list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    }
    void Validate(bool alpha=false){
        void* data{};const D3D12_RANGE range{0,SIZE_T(bytes)};Check(readback->Map(0,&range,&data));UINT64 written[2]{},holes[2]{},different{};
        for(UINT plane=0;plane<2;++plane)for(UINT eye=0;eye<2;++eye){const auto i=plane*4+eye*2;const auto& a=layouts[i];const auto& b=layouts[i+1];
            for(UINT y=0;y<a.Footprint.Height;++y)for(UINT x=0;x<a.Footprint.Width;++x){
                const auto* actual=static_cast<const uint8_t*>(data)+a.Offset+UINT64(y)*a.Footprint.RowPitch+x*strides[i]+components[i];
                const auto* expected=static_cast<const uint8_t*>(data)+b.Offset+UINT64(y)*b.Footprint.RowPitch+x*strides[i+1]+components[i+1];
                const uint32_t clear{};const bool discarded=alpha && !((x+y)&1);
                different+=std::memcmp(actual,discarded?reinterpret_cast<const uint8_t*>(&clear):expected,plane?1:4)!=0;
                if(!plane){uint32_t depth{};std::memcpy(&depth,expected,4);written[eye]+=depth!=0;holes[eye]+=discarded && depth!=0;}
            }
        }
        const D3D12_RANGE none{};readback->Unmap(0,&none);Require(!different && written[0]>16 && written[1]>16);
        if(alpha)for(UINT eye=0;eye<2;++eye)Require(holes[eye]>16 && written[eye]-holes[eye]>16);
    }
};
int main(int argc,char** argv)try{
    Require(argc>=3 && argc<=5);const bool mainOnly=argc==4 && std::string(argv[3])=="main-only";
    const bool mismatch=argc==4 && std::string(argv[3])=="whole-target-mismatch";
    const std::string mode=argc>=4?argv[3]:"";const bool optimized=mode.starts_with("optimized");
    const bool alphaOptimized=mode.starts_with("optimized-alpha");
    const bool combinedAlpha=mode.starts_with("optimized-alpha-skinning") || mode=="native-alpha-skinning-reference";
    const bool missingSkin=mode=="optimized-alpha-skinning-missing-skin",missingAlpha=mode=="optimized-alpha-skinning-missing-alpha";
    const bool staleSkin=mode=="optimized-alpha-skinning-stale",failedSkin=mode=="optimized-alpha-skinning-failed-skin";
    const bool failedAlpha=mode=="optimized-alpha-skinning-failed-alpha",skinCapacity=mode=="optimized-alpha-skinning-capacity";
    const bool jointRejected=missingSkin || missingAlpha || staleSkin || failedSkin || failedAlpha || skinCapacity;
    const bool skinCaptured=!missingSkin;
    const bool alphaCaptured=!missingAlpha;
    const bool sameList=mode=="optimized-alpha-list-reuse",detectReplay=mode=="optimized-alpha-replay-detected";
    const bool alphaReference=mode=="native-alpha-reference" || mode=="native-alpha-reject" || mode=="native-alpha-skinning-reference";
    const bool alpha=alphaReference || alphaOptimized;
    const bool alphaMaterialMismatch=mode=="optimized-alpha-material" || mode=="optimized-alpha-skinning-material";
    const bool alphaMipMismatch=mode=="optimized-alpha-mip" || mode=="optimized-alpha-skinning-mip";
    const bool alphaIndexMismatch=mode=="optimized-alpha-index" || mode=="optimized-alpha-skinning-index";
    const bool alphaRejectMain=mode=="optimized-alpha-reject-main",alphaRejectPeer=mode=="optimized-alpha-reject-peer";
    const bool alphaReject=mode=="native-alpha-reject";Require((argc==5)==alpha);
    const bool nativeCamera=mode=="optimized-native-camera" || mode=="optimized-native-camera-reject" || alpha;
    const bool timing=mode=="native-timing";
    const bool capacity=mode=="optimized-capacity";
    const bool instanceProof=mode=="optimized-instances";
    const bool skinProof=mode.starts_with("optimized-skinning") || (combinedAlpha && alphaOptimized),skinReuse=mode=="optimized-skinning-reuse";
    const bool skinPermitMain=mode=="optimized-skinning-reject-main-copy",skinPermitPeer=mode=="optimized-skinning-reject-peer-copy";
    const bool skinPermits=skinPermitMain || skinPermitPeer;
    const bool reuse=mode=="optimized-reuse" || capacity || instanceProof || skinReuse || skinPermits;
    const UINT repeat=capacity?early_depth::MaxDraws-1:1;
    const bool deferred=mode=="optimized-deferred";
    bool rejected=mode=="optimized-reject" || mode=="optimized-native-camera-reject",depthMismatch=mode=="optimized-depth";
    const bool stencilMismatch=mode=="optimized-stencil";
    const bool nonzeroInitial=mode=="optimized-initial";
    const bool aborted=mode=="optimized-abort";
    const bool whole=argc>=4 && (mode=="whole-target" || mismatch || optimized || timing || alpha);
    const bool cross=argc>=4,both=cross && mode=="cross",peerOnly=cross && mode=="peer-only";
    Require(!cross || both || mainOnly || peerOnly || whole);const bool extras=!mainOnly && !both && !deferred;
    const auto vs=File(argv[1]),paired=File(argv[2]);const auto pixel=alpha?File(argv[4]):std::vector<uint8_t>{};
    ComPtr<ID3D12Debug> debug;Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)));debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter1> adapter;
    Check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device2> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device)));
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 options{};Check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3,&options,sizeof(options)));if(!options.ViewInstancingTier)return 77;
    ComPtr<ID3D12InfoQueue> messages;Check(device.As(&messages));
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,3,0,0,0};D3D12_ROOT_PARAMETER p{};
    p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;p.DescriptorTable={1,&range};
    D3D12_DESCRIPTOR_RANGE alphaRanges[]{{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,4,0,1}};
    D3D12_ROOT_PARAMETER parameters[]{p,{}};parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;parameters[1].DescriptorTable={2,alphaRanges};
    D3D12_ROOT_SIGNATURE_DESC desc{alpha?2u:1u,parameters,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};ComPtr<ID3DBlob> rootBytes,error;
    Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&rootBytes,&error));ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(0,rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    scene_state::RootCreated(root.Get(),rootBytes->GetBufferPointer(),rootBytes->GetBufferSize());
    auto variant=std::make_shared<families::RootVariant>();PairedBinding pair{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100};
    Check(BuildPairedRoot(rootBytes->GetBufferPointer(),rootBytes->GetBufferSize(),{&pair,1},variant->plan));
    Check(device->CreateRootSignature(0,variant->plan.serialized.data(),variant->plan.serialized.size(),IID_PPV_ARGS(&variant->root)));
    D3D12_INPUT_ELEMENT_DESC inputs[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"INSTANCE",0,DXGI_FORMAT_R32G32B32A32_FLOAT,7,0,D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,1}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.VS={vs.data(),vs.size()};pd.InputLayout={inputs,2};
    pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.SampleDesc.Count=1;pd.SampleMask=UINT_MAX;pd.DSVFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;
    pd.DepthStencilState.DepthEnable=TRUE;pd.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;pd.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_GREATER;
    pd.DepthStencilState.StencilEnable=TRUE;pd.DepthStencilState.StencilReadMask=pd.DepthStencilState.StencilWriteMask=255;
    pd.DepthStencilState.FrontFace=pd.DepthStencilState.BackFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_REPLACE,D3D12_COMPARISON_FUNC_ALWAYS};
    ComPtr<ID3D12PipelineState> original,opaqueOriginal;
    if(alpha){Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&opaqueOriginal)));pd.PS={pixel.data(),pixel.size()};}
    Check(device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&original)));
    families::Family family;family.pairedRoot=variant;family.sourceRoot=root;family.pass.depth=pd.DSVFormat;family.pass.samples=pd.SampleDesc;family.instanceOrdinalIndependent=true;
    family.pass.pixelShader=alpha;family.pass.fixedRasterState=family.pass.depthStateKnown=true;family.pass.depthState=pd.DepthStencilState;
    family.alphaDepthContract=combinedAlpha?2u:alpha?1u:0u;family.skinningReadContract=combinedAlpha?2u:0u;family.nativeCameraStages=nativeCamera?1u:0u;
    pd.pRootSignature=variant->root.Get();pd.VS={paired.data(),paired.size()};ViewInstancedLayout vi;vi.allowMasking=true;
    Check(CreateViewInstancedPipeline(device.Get(),pd,vi,&family.pipeline));
    if(nativeCamera){pd.pRootSignature=root.Get();Check(CreateViewInstancedPipeline(device.Get(),pd,vi,&family.nativeCameraPipeline));}
    const float vertex[]{-1,-1,0,1,1,-1},instance[]{0,-.3f,0,0,0,.3f,0,0,.4f,-.3f,0,0,-.6f,.2f,0,0};const uint16_t index[]{0,1,2};
    auto vb=Upload(device.Get(),vertex,sizeof(vertex)),ib=Upload(device.Get(),index,sizeof(index)),inst=Upload(device.Get(),instance,sizeof(instance));
    std::array<float,4> camera[2]{{-.3f,0,0,.6f},{.3f,0,0,.6f}};BankPair values;values.binding=pair;
    for(UINT i=0;i<2;++i)values.eyes[i].constants={reinterpret_cast<uint8_t*>(camera[i].data()),16};
    std::array<uint8_t,768> constants{};std::memcpy(constants.data(),camera[0].data(),16);std::memcpy(constants.data()+256,camera[1].data(),16);
    if(nativeCamera)std::memcpy(constants.data()+272,camera[0].data(),16);
    auto cb=Upload(device.Get(),constants.data(),UINT(constants.size()));ComPtr<ID3D12DescriptorHeap> heap,dsv;
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,reuse?9u:alpha?8u:6u,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
    const auto step=device->GetDescriptorHandleIncrementSize(hd.Type);
    for(UINT i=0;i<hd.NumDescriptors;++i){D3D12_CONSTANT_BUFFER_VIEW_DESC c{cb->GetGPUVirtualAddress()+(i/3)*256,256};device->CreateConstantBufferView(i%3==1?&c:nullptr,{heap->GetCPUDescriptorHandleForHeapStart().ptr+i*step});}
    ComPtr<ID3D12Resource> alphaTexture,alphaPixels,alphaChangedPixels,alphaMaterial,alphaMaterialUpload,alphaChangedMaterial;
    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> alphaLayouts{};std::array<UINT,4> alphaRows{};std::array<UINT64,4> alphaRowBytes{};
    material_binding::Lease alphaB4;texture_read::Lease alphaLease;const std::array<UINT,4> alphaMips{0,1,2,3};
    if(alpha){
        D3D12_RESOURCE_DESC image{};image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;image.Width=image.Height=alphaOptimized?8:2;
        image.DepthOrArraySize=image.SampleDesc.Count=1;image.MipLevels=alphaOptimized?4:1;
        image.Format=alphaOptimized?DXGI_FORMAT_BC1_UNORM_SRGB:DXGI_FORMAT_R8_UNORM;
        D3D12_HEAP_PROPERTIES memory{};memory.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&image,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&alphaTexture)));
        UINT64 bytes{};device->GetCopyableFootprints(&image,0,image.MipLevels,0,alphaLayouts.data(),alphaRows.data(),alphaRowBytes.data(),&bytes);
        Require(bytes && bytes!=UINT64_MAX);std::vector<uint8_t> pixels(size_t(bytes),0);
        if(alphaOptimized){
            UINT indices{};for(UINT y=0;y<4;++y)for(UINT x=0;x<4;++x)if(!((x+y)&1))indices|=1u<<(2*(y*4+x));
            for(UINT mip=0;mip<4;++mip)for(UINT row=0;row<alphaRows[mip];++row)for(UINT64 block=0;block<alphaRowBytes[mip];block+=8){
                auto* at=pixels.data()+alphaLayouts[mip].Offset+UINT64(row)*alphaLayouts[mip].Footprint.RowPitch+block;
                const uint16_t white=0xffff;std::memcpy(at,&white,2);std::memcpy(at+4,&indices,4);
            }
        }else pixels[1]=pixels[alphaLayouts[0].Footprint.RowPitch]=255;
        alphaPixels=Upload(device.Get(),pixels.data(),UINT(pixels.size()));
        if(alphaOptimized){pixels[alphaLayouts[3].Offset+alphaRowBytes[3]-1]^=0x40;
            alphaChangedPixels=Upload(device.Get(),pixels.data(),UINT(pixels.size()));}
        std::array<uint8_t,512> materialBytes{};const UINT indexValue=43;const float cutoff=.5f;
        std::memcpy(materialBytes.data(),&indexValue,4);std::memcpy(materialBytes.data()+4,&cutoff,4);
        alphaMaterialUpload=Upload(device.Get(),materialBytes.data(),UINT(materialBytes.size()));
        if(alphaOptimized){
            D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=materialBytes.size();
            buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&alphaMaterial)));
            materialBytes[272]=1;alphaChangedMaterial=Upload(device.Get(),materialBytes.data(),UINT(materialBytes.size()));
            alphaB4.source.resource=alphaMaterial;alphaB4.source.bytes=materialBytes.size();alphaB4.source.heapType=D3D12_HEAP_TYPE_DEFAULT;alphaB4.native.record.kind=1;
            alphaLease.resource=alphaTexture;alpha_fixture::material=alphaMaterial.Get();alpha_fixture::texture=alphaTexture.Get();
        }else alphaMaterial=alphaMaterialUpload;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=image.Format;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=image.MipLevels;
        device->CreateShaderResourceView(alphaTexture.Get(),&srv,{heap->GetCPUDescriptorHandleForHeapStart().ptr+6*step});
        const D3D12_CONSTANT_BUFFER_VIEW_DESC material{alphaMaterial->GetGPUVirtualAddress(),512};
        device->CreateConstantBufferView(&material,{heap->GetCPUDescriptorHandleForHeapStart().ptr+7*step});
    }
    hd={D3D12_DESCRIPTOR_HEAP_TYPE_DSV,2,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)));
    D3D12_RESOURCE_DESC texture{};texture.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;texture.Width=128;texture.Height=64;
    texture.DepthOrArraySize=texture.MipLevels=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;texture.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;ComPtr<ID3D12Resource> nativeTarget;
    Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&nativeTarget)));
    device->CreateDepthStencilView(nativeTarget.Get(),nullptr,dsv->GetCPUDescriptorHandleForHeapStart());
    ComPtr<ID3D12Resource> peerTarget;
    ComPtr<ID3D12Resource> expectedTarget;OutputCheck outputs;
    ComPtr<ID3D12QueryHeap> statistics;ComPtr<ID3D12Resource> statisticsReadback;
    D3D12_CPU_DESCRIPTOR_HANDLE expectedDsv{dsv->GetCPUDescriptorHandleForHeapStart().ptr+device->GetDescriptorHandleIncrementSize(hd.Type)};
    if(timing){Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&peerTarget)));
        device->CreateDepthStencilView(peerTarget.Get(),nullptr,expectedDsv);}
    if(optimized || alpha){Check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&texture,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&expectedTarget)));
        device->CreateDepthStencilView(expectedTarget.Get(),nullptr,expectedDsv);outputs.Create(device.Get(),texture);}
    if(optimized){
        const D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS,1,0};Check(device->CreateQueryHeap(&qd,IID_PPV_ARGS(&statistics)));
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
        buffer.Height=1;buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto memory=hp;memory.Type=D3D12_HEAP_TYPE_READBACK;
        Check(device->CreateCommittedResource(&memory,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&statisticsReadback)));}
    std::array<ComPtr<ID3D12CommandQueue>,2> queues;D3D12_COMMAND_QUEUE_DESC q{};
    Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queues[0])));if(cross)Check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queues[1])));else queues[1]=queues[0];
    std::array<ComPtr<ID3D12CommandAllocator>,2> allocators;std::array<ComPtr<ID3D12GraphicsCommandList>,2> lists;
    native_probe::DrawRecord draw{};draw.frame=1;draw.pipeline=reinterpret_cast<uintptr_t>(original.Get());draw.root=reinterpret_cast<uintptr_t>(root.Get());
    draw.sequence=combinedAlpha?2u:1u;draw.depth=dsv->GetCPUDescriptorHandleForHeapStart().ptr;draw.indices=3;draw.instances=2;draw.viewport={0,0,128,64,0,1};draw.scissor={0,0,128,64};
    EarlyDepthPass pass;Check(pass.Create(device.Get(),128,64,timing?3:optimized?2:UINT(whole)));
    ComPtr<ID3D12Resource> proofSource;
    if(instanceProof){
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=64;
        desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        Check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&proofSource)));
    }
    SkinningCheck skin;if(skinProof)skin.Create(device.Get());if(skinPermits)skin.CreateMarkers(device.Get());
    const uint8_t changedByte=0xff;auto changed=Upload(device.Get(),&changedByte,1);
    const UINT generations=sameList?3u:skinPermits?2u:reuse?4u:1u;
    ID3D12GraphicsCommandList* reusedList{};
    for(UINT generation=0;generation<generations;++generation){
    if(generation){Require(pass.Restart()==S_OK);outputs=OutputCheck{};outputs.Create(device.Get(),texture);}
    if(reuse){rejected=!skinReuse && !skinPermits && generation==1;depthMismatch=!skinReuse && !skinPermits && generation==2;Require(pass.Restart()==S_FALSE);}
    if(alphaOptimized){
        alpha_fixture::copies=alpha_fixture::textureCopies=0;alpha_fixture::valid={true,true};
        Require(!pass.ShaderSource(nullptr) && pass.ShaderProofCounts()==std::array<uint64_t,4>{} && pass.AlphaProofCounts()==std::array<uint64_t,6>{});
        if(sameList)Require(pass.Restart()==S_FALSE);
    }
    const bool instanceMismatch=instanceProof && generation==3;
    const bool skinBoneMismatch=mode=="optimized-skinning-bones" || mode=="optimized-alpha-skinning-bones";
    const bool skinTextureMismatch=mode=="optimized-skinning-texture" || mode=="optimized-alpha-skinning-texture";
    const bool skinCaptureFailure=mode=="optimized-skinning-copy-failure" || (skinReuse && generation==1);
    const bool skinPeerFailure=mode=="optimized-skinning-peer-failure" || (skinReuse && generation==2);
    const bool rejectMainCopy=skinPermitMain && !generation,rejectPeerCopy=skinPermitPeer && !generation;
    const bool rejectSnapshots=rejectMainCopy || rejectPeerCopy;
    if(skinProof){skin.Reset();Require(!pass.ShaderSource(nullptr) && pass.ShaderProofCounts()==std::array<uint64_t,4>{});}
    std::array<float,4> alternateCamera{};
    if(reuse){
        camera[0][0]=-.3f+.05f*generation;camera[1][0]=.3f-.05f*generation;
        alternateCamera=camera[1];alternateCamera[0]+=.17f;alternateCamera[3]=.8f;
        void* mapped{};Check(cb->Map(0,nullptr,&mapped));std::memcpy(mapped,camera[0].data(),16);
        std::memcpy(static_cast<uint8_t*>(mapped)+256,camera[1].data(),16);std::memcpy(static_cast<uint8_t*>(mapped)+512,alternateCamera.data(),16);cb->Unmap(0,nullptr);
    }
    StereoBindingBank bank;Check(bank.Create(device.Get(),variant->root.Get(),variant->plan,{&values,1}));
    ComPtr<ID3D12Fence> hold;if(reuse && generation==1)Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&hold)));
    ComPtr<ID3D12Fence> skinMainDone;if(skinProof)Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&skinMainDone)));
    ComPtr<ID3D12Fence> mainCopyHold;if(rejectPeerCopy)Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&mainCopyHold)));
    draw.firstInstance=0;draw.instances=2;
    for(UINT eye=0;eye<2;++eye){
        if(skinProof && eye && !mainCopyHold){
            for(UINT i=0;i<500 && !skinMainDone->GetCompletedValue();++i)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Require(skinMainDone->GetCompletedValue()==1);if(skinBoneMismatch)skin.ChangeBones();
        }
        skinning_fixture::eye=eye;
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[eye])));
        if(sameList && (eye || generation)){
            if(eye)lists[eye]=lists[0];
            Check(lists[eye]->Reset(allocators[eye].Get(),original.Get()));
            pass.Reset(lists[eye].Get());cvr::gpu::ResetCommandResources(lists[eye].Get());Require(!pass.Failed());
        }else Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[eye].Get(),original.Get(),IID_PPV_ARGS(&lists[eye])));
        if(sameList){if(!reusedList)reusedList=lists[eye].Get();Require(lists[eye].Get()==reusedList);}
        auto* list=lists[eye].Get();draw.side=eye;scene_state::Reset(list,true);list->SetGraphicsRootSignature(root.Get());scene_state::Root(list,root.Get());
        ID3D12DescriptorHeap* hs=heap.Get();list->SetDescriptorHeaps(1,&hs);scene_state::Heaps(list,1,&hs);
        const D3D12_GPU_DESCRIPTOR_HANDLE table{heap->GetGPUDescriptorHandleForHeapStart().ptr+(eye?0:3)*step};list->SetGraphicsRootDescriptorTable(0,table);scene_state::Table(list,0,table);
        if(alpha){
            const D3D12_GPU_DESCRIPTOR_HANDLE pixelTable{heap->GetGPUDescriptorHandleForHeapStart().ptr+6*step};
            list->SetGraphicsRootDescriptorTable(1,pixelTable);scene_state::Table(list,1,pixelTable);
            Require(cvr::gpu::KeepCommandResources(list,{heap.Get(),alphaTexture.Get(),alphaMaterial.Get(),alphaPixels.Get()}));
            if(!eye || (alphaOptimized && alphaMipMismatch)){
                if(eye || generation){D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition={alphaTexture.Get(),eye?3u:D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST};list->ResourceBarrier(1,&barrier);}
                const UINT first=eye?3:0,last=alphaOptimized?4:1;
                for(UINT mip=first;mip<last;++mip){D3D12_TEXTURE_COPY_LOCATION from{},to{};
                    from.pResource=eye?alphaChangedPixels.Get():alphaPixels.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=alphaLayouts[mip];
                    to.pResource=alphaTexture.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=mip;
                    list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
                D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition={alphaTexture.Get(),eye?3u:D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,
                    alphaOptimized?D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};list->ResourceBarrier(1,&barrier);}
            if(alphaOptimized){
                Require(cvr::gpu::KeepCommandResources(list,{alphaMaterialUpload.Get(),alphaChangedMaterial.Get(),alphaChangedPixels.Get()}));
                if(!eye || alphaMaterialMismatch){
                    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    barrier.Transition={alphaMaterial.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_GENERIC_READ,D3D12_RESOURCE_STATE_COPY_DEST};
                    if(eye || generation)list->ResourceBarrier(1,&barrier);
                    list->CopyBufferRegion(alphaMaterial.Get(),0,eye?alphaChangedMaterial.Get():alphaMaterialUpload.Get(),0,512);
                    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_GENERIC_READ;
                    list->ResourceBarrier(1,&barrier);
                }
            }
        }
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);scene_state::Topology(list,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->OMSetStencilRef(7);scene_state::Stencil(list,7);const auto target=timing && eye?expectedDsv:dsv->GetCPUDescriptorHandleForHeapStart();
        draw.depth=target.ptr;list->OMSetRenderTargets(0,nullptr,FALSE,&target);
        list->RSSetViewports(1,&draw.viewport);list->RSSetScissorRects(1,&draw.scissor);
        const D3D12_INDEX_BUFFER_VIEW iv{ib->GetGPUVirtualAddress(),sizeof(index),DXGI_FORMAT_R16_UINT};list->IASetIndexBuffer(&iv);
        const D3D12_VERTEX_BUFFER_VIEW views[]{{vb->GetGPUVirtualAddress(),sizeof(vertex),8},{inst->GetGPUVirtualAddress(),sizeof(instance),16}};
        list->IASetVertexBuffers(0,1,views);list->IASetVertexBuffers(7,1,views+1);
        draw.vertices[7]=views[1];
        if(whole){list->ClearDepthStencilView(target,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);
            if(optimized || alpha){
                list->ClearDepthStencilView(expectedDsv,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,0,0,0,nullptr);
                const D3D12_RECT edge{127,63,128,64};
                if(nonzeroInitial)for(auto h:{target,expectedDsv})list->ClearDepthStencilView(h,D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,.2f,19,1,&edge);
                if(eye && (depthMismatch || stencilMismatch))for(auto h:{target,expectedDsv})
                    list->ClearDepthStencilView(h,depthMismatch?D3D12_CLEAR_FLAG_DEPTH:D3D12_CLEAR_FLAG_STENCIL,.2f,19,1,&edge);
                list->OMSetRenderTargets(0,nullptr,FALSE,&expectedDsv);
                if(alpha)list->SetPipelineState(opaqueOriginal.Get());
                if(!eye){list->DrawIndexedInstanced(3,2,0,0,0);
                    if(reuse)list->SetGraphicsRootDescriptorTable(0,{heap->GetGPUDescriptorHandleForHeapStart().ptr+6*step});
                    list->DrawIndexedInstanced(3,1,0,0,3);if(reuse)list->SetGraphicsRootDescriptorTable(0,table);}
                else list->DrawIndexedInstanced(3,3,0,0,1);
                if(alpha)list->SetPipelineState(original.Get());
                list->OMSetRenderTargets(0,nullptr,FALSE,&target);
            }
            if(instanceProof && (!eye || instanceMismatch)){
                D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition={proofSource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_DEST};
                if(generation || eye)list->ResourceBarrier(1,&b);
                if(!eye)list->CopyBufferRegion(proofSource.Get(),0,inst.Get(),0,64);
                else list->CopyBufferRegion(proofSource.Get(),31,changed.Get(),0,1);
                std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);
            }
            if(skinProof){
                if(skinPermits && !eye)skin.ResetMarkers(list);
                if(!eye || skinTextureMismatch)skin.WriteTexture(list,eye!=0);
                if(eye && skinPeerFailure)skinning_fixture::failNext=true;
            }
            auto* resource=timing && eye?peerTarget.Get():nativeTarget.Get();
            Require(pass.StartNative(list,draw,resource,proofSource.Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
            Require(pass.StartNative(list,draw,resource));}
        if(timing){
            Require(!pass.Main(list,draw,family,bank));Require(!pass.Original(list,draw));Require(!pass.Peer(list,draw));
            Require(pass.ObserveNative(list,draw));list->DrawIndexedInstanced(3,2,0,0,0);
            if(!eye){Require(pass.ObserveNative(list,draw));list->DrawIndexedInstanced(3,1,0,0,3);}
            Require(pass.FinishNative(list,eye));Require(!pass.ObserveNative(list,draw));
            if(eye)Require(pass.Readback(list));
        }else if(!eye){const uint8_t invalid[]{1,3},masks[]{uint8_t(both?3:2),uint8_t(mainOnly || peerOnly?2:3)};
            const float wrong[]{3,0,0,0};
            const std::span<const uint8_t> additional=extras?std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(mismatch || rejected?wrong:instance+8),16):std::span<const uint8_t>{};
            Require(!pass.Main(list,draw,family,bank,invalid));
            if(instanceProof){
                Require(!pass.CaptureInstances(list,proofSource.Get(),60,8,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
                Require(pass.CaptureInstances(list,proofSource.Get(),0,16,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
                if(generation==1){pass.ReleaseInstanceRead(list);pass.ReleaseInstanceRead(list);}
                Require(pass.CaptureInstances(list,proofSource.Get(),16,16,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
                Require(pass.InstanceProofCounts()==std::array<uint64_t,2>{2,32});
            }
            if(skinProof){
                if(skinCaptureFailure){
                    skinning_fixture::failNext=true;
                    Require(!pass.CaptureSkinning(list,skin.bones.Get(),skin.ranges,skin.lease) && texture_read::LastFailure()==41);
                    Require(!pass.ShaderSource(nullptr) && pass.ShaderProofCounts()==std::array<uint64_t,4>{} && !skinning_fixture::copies);
                }
                if(skinCaptured)Require(pass.CaptureSkinning(list,skin.bones.Get(),skin.ranges,skin.lease,
                    combinedAlpha?UINT(draw.sequence)-(staleSkin?1u:0u):0u));
                if(failedSkin)Require(!pass.CaptureSkinning(list,skin.bones.Get(),{},skin.lease,UINT(draw.sequence)));
                if(skinCapacity){const std::vector<skinning::BoneRange> tooMany(DepthCommitGate::MaxBufferProofs,{16,16});
                    Require(!pass.CaptureSkinning(list,skin.bones.Get(),tooMany,skin.lease,UINT(draw.sequence)));}
                Require(pass.ShaderSource(skin.bones.Get())==skinCaptured && pass.ShaderSource(skin.image.Get())==skinCaptured && !pass.ShaderSource(inst.Get()));
                Require(pass.ShaderProofCounts()==(skinCaptured?std::array<uint64_t,4>{1,0,48,SkinningCheck::Width*SkinningCheck::Height}:std::array<uint64_t,4>{}));
            }
            if(alphaReject){
                Require(!pass.Main(list,draw,family,bank,masks,additional,0,true,false));
                Require(!pass.Main(list,draw,family,bank,masks,additional,0,false,true));
                auto invalidFamily=family;invalidFamily.alphaDepthContract=0;
                Require(!pass.Main(list,draw,invalidFamily,bank,masks,additional,0,true,true));
                invalidFamily=family;invalidFamily.nativeCameraStages=3;
                Require(!pass.Main(list,draw,invalidFamily,bank,masks,additional,0,true,true));
                invalidFamily=family;invalidFamily.pass.pixelShader=false;
                Require(!pass.Main(list,draw,invalidFamily,bank,masks,additional,0,true,true));
                for(UINT forbidden:{0u,2u,3u}){EarlyDepthPass other;Check(other.Create(device.Get(),128,64,forbidden));
                    if(forbidden)Require(other.StartNative(list,draw,nativeTarget.Get()));
                    Require(!other.Main(list,draw,family,bank,masks,additional,0,true,true));}
            }
            if(alphaOptimized){
                Require(!pass.Main(list,draw,family,bank,masks,additional,0,true,false,true));
                const UINT invalidMip=4;
                if(!combinedAlpha){
                    Require(!pass.CaptureAlpha(list,alphaB4,alphaLease,{&invalidMip,1},43,UINT(draw.sequence)));
                    Require(!pass.CaptureAlpha(list,alphaB4,alphaLease,alphaMips,32768,UINT(draw.sequence)));
                    Require(!pass.CaptureAlpha(list,alphaB4,alphaLease,alphaMips,43,0));
                }
                if(staleSkin){Require(!pass.CaptureAlpha(list,alphaB4,alphaLease,alphaMips,43,UINT(draw.sequence)));
                    Require(pass.AlphaProofCounts()[5]==1);}
                if(failedAlpha){Require(!pass.CaptureAlpha(list,alphaB4,alphaLease,{&invalidMip,1},43,UINT(draw.sequence)));
                    Require(pass.AlphaProofCounts()[5]==4);}
                if(alphaCaptured)Require(pass.CaptureAlpha(list,alphaB4,alphaLease,alphaMips,alphaIndexMismatch?44u:43u,UINT(draw.sequence)));
                Require(pass.ShaderSource(alphaMaterial.Get())==alphaCaptured && pass.ShaderSource(alphaTexture.Get())==alphaCaptured);
                auto wrongSequence=draw;++wrongSequence.sequence;
                Require(!pass.Main(list,wrongSequence,family,bank,masks,additional,0,true,false,true));
                Require(!pass.Main(list,draw,family,bank,masks,additional,0,true,true,true));
                if(combinedAlpha){auto incompatible=family;incompatible.skinningReadContract=0;
                    Require(!pass.Main(list,draw,incompatible,bank,masks,additional,0,true,false,true));}
            }
            if(jointRejected){Require(!pass.Main(list,draw,family,bank,masks,additional,0,true,false,true));Require(pass.Original(list,draw));}
            else Require(reuse?pass.MainConstants(list,draw,family,{&values,1},masks,additional):pass.Main(list,draw,family,bank,masks,additional,0,nativeCamera,alphaReference,alphaOptimized));
            if(alphaOptimized)Require(!pass.Main(list,draw,family,bank,masks,additional,0,true,false,true));
            for(UINT i=1;i<repeat;++i)Require(pass.MainConstants(list,draw,family,{&values,1},masks));
            if(whole){auto fallback=draw;fallback.firstInstance=3;fallback.instances=1;
                if(aborted){pass.AbortNative(list,0);Require(!pass.Original(list,fallback));Require(!pass.Main(list,draw,family,bank));
                    // No explicit target bind: abort must restore the game's native DSV.
                    list->DrawIndexedInstanced(3,1,0,0,3);
                }else {
                    if(reuse){auto alternate=values;alternate.eyes[1].constants={reinterpret_cast<const uint8_t*>(alternateCamera.data()),16};const uint8_t mask=2;
                        Require(pass.MainConstants(list,fallback,family,{&alternate,1},{&mask,1}));Require(pass.ArenaCount()==1);
                        if(capacity)Require(!pass.MainConstants(list,fallback,family,{&alternate,1},{&mask,1}));}
                    else Require(pass.Original(list,fallback));
                    Require(pass.FinishNative(list,0));}}
        }else {
            if(whole){draw.firstInstance=1;draw.instances=3;const InstanceRun remaining{deferred?1u:2u,deferred?2u:1u};
                if(optimized)list->BeginQuery(statistics.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
                Require(pass.PeerRemainder(list,draw,{&remaining,1}));
                if(optimized){list->EndQuery(statistics.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0);
                    list->ResolveQueryData(statistics.Get(),D3D12_QUERY_TYPE_PIPELINE_STATISTICS,0,1,statisticsReadback.Get(),0);
                    Require(pass.CommitNative(!rejected && !aborted && !jointRejected));}
                Require(pass.FinishNative(list,1));}
            else {draw.instances=1;
                if(!mainOnly && !peerOnly){if(both)Require(pass.Peer(list,draw));draw.firstInstance=1;Require(pass.Peer(list,draw));}
                if(extras){draw.firstInstance=2;Require(pass.Peer(list,draw));}}
            Require(pass.Readback(list));}
        if(optimized || alpha){outputs.Capture(list,eye,nativeTarget.Get(),expectedTarget.Get());if(eye)outputs.Copy(list);}
        if(skinPermits && eye)skin.ReadMarkers(list);
        Check(list->Close());ID3D12CommandList* command=list;auto keep=cvr::gpu::PrepareCommandResources(1,&command);
        if(hold && eye)Check(queues[eye]->Wait(hold.Get(),1));
        if(mainCopyHold && !eye)Check(queues[eye]->Wait(mainCopyHold.Get(),1));
        if((rejectMainCopy && !eye) || (rejectPeerCopy && eye))skinning_fixture::receiptValid=false;
        if((alphaRejectMain && !eye) || (alphaRejectPeer && eye))alpha_fixture::valid[eye]=false;
        pass.BeforeSubmit(queues[eye].Get(),1,&command);queues[eye]->ExecuteCommandLists(1,&command);cvr::gpu::SubmitCommandResources(queues[eye].Get(),std::move(keep));pass.Submitted(queues[eye].Get(),1,&command);
        if(skinProof && !eye)Check(queues[eye]->Signal(skinMainDone.Get(),1));
        if((reuse || sameList) && !eye)Require(pass.Restart()==S_FALSE);
    }
    if(mainCopyHold){Require(!skinMainDone->GetCompletedValue());Check(mainCopyHold->Signal(1));}
    if(hold){DepthComparisonResult pending;Require(pass.Read(pending)==S_FALSE && pass.Restart()==S_FALSE);Check(hold->Signal(1));}
    DepthComparisonResult result;HRESULT status=S_FALSE;
    for(unsigned i=0;i<500 && status==S_FALSE;++i){status=pass.Read(result);if(status==S_FALSE)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    Check(status);Require(result.complete && result.draws==(whole && !aborted?repeat+1:1u));
    if(!optimized && !timing)Require((mismatch?result.depthDifferent[0]>0:result.depthDifferent[0]==0) && !result.depthDifferent[1] &&
        (mismatch || !result.stencilDifferent[0]) && !result.stencilDifferent[1] && result.written[1]>16 &&
        (mainOnly?result.written[0]==0:result.written[0]>16));
    if(whole){Require(pass.NativeCounts()==(timing?std::array<uint32_t,8>{3,2,1,0,0,0,0,0}:
        std::array<uint32_t,8>{optimized?2u:1u,aborted?1u:repeat+1,1,aborted || reuse?0u:jointRejected?2u:1u,1,1,1,2}));Require(pass.NativeDifferences()==std::array<uint64_t,4>{});
        const auto times=pass.GpuTimes();
        for(UINT side=0;side<2;++side){UINT64 frequency{};Check(queues[side]->GetTimestampFrequency(&frequency));
            Require(times[side*3+1]>times[side*3] && times[side*3+2]==frequency);
            std::printf("GPU side %u: %.6f ms\n",side,1000.0*(times[side*3+1]-times[side*3])/frequency);}
        if(timing)Require(pass.OptimizedCounts()==std::array<uint64_t,8>{0,UINT64_MAX,0,0,0,0,0,0});}
    if(optimized){outputs.Validate(alpha);const auto gate=pass.OptimizedCounts();Require(gate[0]==1 && gate[1]==uint64_t(!rejected && !aborted && !depthMismatch && !stencilMismatch && !instanceMismatch && !skinBoneMismatch && !skinTextureMismatch && !skinPeerFailure && !rejectSnapshots &&
        !alphaMaterialMismatch && !alphaMipMismatch && !alphaIndexMismatch && !alphaRejectMain && !alphaRejectPeer && !jointRejected) && gate[2]==(jointRejected?2u:aborted || deferred?3u:4u)+2*(repeat-1) && !gate[3] && gate[4]==1 && gate[5]==1 && !gate[6]);
        std::array<uint64_t,4> expectedProofs{};
        if(skinProof && skinCaptured)expectedProofs={1,skinPeerFailure?2u:3u,48,SkinningCheck::Width*SkinningCheck::Height};
        if(alphaOptimized && alphaCaptured){++expectedProofs[0];expectedProofs[1]+=5;expectedProofs[2]+=292;expectedProofs[3]+=56;}
        Require(pass.ShaderProofCounts()==expectedProofs);
        if(alphaOptimized){
            Require(alpha_fixture::copies==(alphaCaptured?2u:0u) && alpha_fixture::textureCopies==(alphaCaptured?8u:0u));
            const auto proof=pass.AlphaProofCounts();
            Require(proof[0]==UINT(alphaCaptured) && proof[1]==UINT(alphaCaptured) && proof[2]==(alphaCaptured?4u:0u) && proof[3]==(alphaCaptured?292u:0u) && proof[4]==(alphaCaptured?56u:0u));
            if(alphaRejectMain || alphaRejectPeer)Require(proof[5]==(alphaRejectMain?3000u:3001u));
            if(jointRejected)Require(proof[5]==(missingAlpha?0u:9u));
            Require(gate[7]==uint64_t(!alphaRejectMain && !alphaRejectPeer && !jointRejected));
            std::printf("GPU alpha snapshot generation %u: decision %llu, MAIN/PEER b4, four BC1 mips and expected texture index\n",generation,gate[1]);
        }
        if(skinProof){
            Require(gate[7]==uint64_t(!skinPeerFailure && !rejectSnapshots && !jointRejected) && skinning_fixture::attempts==(skinCaptured?2u:0u)+UINT(skinCaptureFailure) &&
                skinning_fixture::copies==(skinCaptured?(skinPeerFailure?1u:2u):0u) && !skinning_fixture::failNext);
            std::printf("Skinning snapshot generation %u: gate %llu, native registration mocked\n",generation,gate[1]);
            if(skinPermits)skin.ValidateMarkers({uint64_t(!rejectMainCopy),uint64_t(!rejectSnapshots)});
        }
        void* mapped{};Check(statisticsReadback->Map(0,nullptr,&mapped));D3D12_QUERY_DATA_PIPELINE_STATISTICS stats{};
        std::memcpy(&stats,mapped,sizeof(stats));const D3D12_RANGE none{};statisticsReadback->Unmap(0,&none);
        Require(stats.IAPrimitives==(gate[1]?(deferred?2u:1u):3u));
        std::printf("GPU input primitives: %llu (%s branch)\n",stats.IAPrimitives,gate[1]?"optimized":"native fallback");}
    if(alphaReference){outputs.Validate(true);Require(!pass.CommitNative(true));
        std::puts("GPU alpha reference: exact textured discard holes and native depth/stencil; optimized admission rejected");}
    if(detectReplay){
        // Detection is not replay support: do not execute a recording whose
        // private resource transitions require the native one-shot contract.
        ID3D12CommandList* replay=lists[0].Get();pass.BeforeSubmit(queues[0].Get(),1,&replay);Require(pass.Failed());
    }
    for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes{};messages->GetMessage(i,nullptr,&bytes);std::vector<uint8_t> data(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Check(messages->GetMessage(i,m,&bytes));if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"%s\n",m->pDescription);Require(false);}}
    // Retain the old recording across Restart; only the next actual list Reset
    // may detach its resource batch and replace its per-side generation tokens.
    if(!sameList || generation+1==generations)for(auto& list:lists)cvr::gpu::ResetCommandResources(list.Get());
    cvr::gpu::CollectCommandResources();
    }
    std::printf("PASS early paired depth, separate native eye recordings, %s queue and exact depth/stencil\n",cross?"cross":"same");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
