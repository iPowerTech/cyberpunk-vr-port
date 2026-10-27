#include "Render/NativeTextureRead.hpp"
#include <map>
#include <vector>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace cvr::stereo;
using Memory=std::map<uint64_t,std::vector<uint8_t>>;
template<class T> void Put(Memory& memory,uint64_t address,const T& value){
    auto& bytes=memory[address];bytes.resize(sizeof(value));std::memcpy(bytes.data(),&value,sizeof(value));
}
bool Read(void* user,uint64_t address,void* out,size_t bytes){
    auto& memory=*static_cast<Memory*>(user);auto it=memory.upper_bound(address);if(it==memory.begin())return false;--it;
    const auto offset=address-it->first;if(offset>it->second.size() || bytes>it->second.size()-offset)return false;
    std::memcpy(out,it->second.data()+offset,bytes);return true;
}
void Require(bool good){if(!good)throw std::runtime_error("native texture state assertion failed");}
D3D12_RESOURCE_DESC Texture(DXGI_FORMAT format=DXGI_FORMAT_BC7_UNORM_SRGB){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=256;desc.Height=128;desc.DepthOrArraySize=1;desc.MipLevels=9;
    desc.Format=format;desc.SampleDesc.Count=1;return desc;
}
D3D12_SHADER_RESOURCE_VIEW_DESC View(const D3D12_RESOURCE_DESC& desc){
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;
    view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels=UINT_MAX;return view;
}
void TextureShapes(){
    for(const auto format:{DXGI_FORMAT_BC1_UNORM_SRGB,DXGI_FORMAT_BC7_UNORM_SRGB}){
        Require(texture_read::detail::AlphaFormat(format));
        auto desc=Texture(format);Require(texture_read::detail::Shape(desc));
        desc.MipLevels=1;Require(texture_read::detail::Shape(desc));
        desc.Width=4096;desc.Height=4096;desc.MipLevels=13;Require(texture_read::detail::Shape(desc));
        desc.MipLevels=14;Require(!texture_read::detail::Shape(desc));
        desc.Width=4;desc.Height=4;desc.MipLevels=3;Require(texture_read::detail::Shape(desc));
        desc.MipLevels=4;Require(!texture_read::detail::Shape(desc));
    }
    for(const auto format:{DXGI_FORMAT_BC1_UNORM,DXGI_FORMAT_BC7_UNORM,
        DXGI_FORMAT_BC1_TYPELESS,DXGI_FORMAT_BC7_TYPELESS,DXGI_FORMAT_BC3_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_UNORM}){
        Require(!texture_read::detail::AlphaFormat(format));
        Require(!texture_read::detail::Shape(Texture(format)));
    }
    auto r8=Texture(DXGI_FORMAT_R8_UNORM);r8.Height=2;r8.MipLevels=1;
    Require(texture_read::detail::Shape(r8));
    r8.Width=1;r8.Height=1;Require(texture_read::detail::Shape(r8));
    r8.MipLevels=0;Require(!texture_read::detail::Shape(r8));
    r8.MipLevels=2;Require(!texture_read::detail::Shape(r8));
    r8.Width=8;r8.Height=4;Require(texture_read::detail::Shape(r8));
    for(const auto format:{DXGI_FORMAT_R8_UNORM,DXGI_FORMAT_R16_UNORM,DXGI_FORMAT_R16G16_UNORM,
        DXGI_FORMAT_R16G16B16A16_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,DXGI_FORMAT_R32_UINT}){
        auto typed=Texture(format);typed.Width=47;typed.Height=33;typed.MipLevels=6;typed.DepthOrArraySize=64;
        Require(texture_read::detail::TypedFormat(format) && texture_read::detail::Shape(typed));
        typed.MipLevels=7;Require(!texture_read::detail::Shape(typed));
    }
    auto reject=[](auto mutate){auto desc=Texture();mutate(desc);Require(!texture_read::detail::Shape(desc));};
    reject([](auto& d){d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;});
    reject([](auto& d){d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;});
    for(const UINT value:{0u,1u,2u,255u,4097u}){
        reject([=](auto& d){d.Width=value;});reject([=](auto& d){d.Height=value;});
    }
    reject([](auto& d){d.Width=UINT64_MAX;});
    for(const UINT16 value:{UINT16(0),UINT16(2049),UINT16(65535)})reject([=](auto& d){d.DepthOrArraySize=value;});
    auto array=Texture();array.DepthOrArraySize=2048;Require(texture_read::detail::Shape(array));
    reject([](auto& d){d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;});
    for(const UINT count:{0u,2u,4u})reject([=](auto& d){d.SampleDesc.Count=count;});
    reject([](auto& d){d.SampleDesc.Quality=1;});
    reject([](auto& d){d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;});
    reject([](auto& d){d.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;});
    reject([](auto& d){d.MipLevels=0;});
    reject([](auto& d){d.MipLevels=10;});
}
void ArraySubresources(){
    auto desc=Texture(DXGI_FORMAT_R16_UNORM);desc.Width=64;desc.Height=32;desc.MipLevels=4;desc.DepthOrArraySize=4;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2DArray={1,2,1,2,0,0};
    std::vector<UINT> indices;
    Require(texture_read::detail::ViewSubresources(desc,view,indices) && indices==std::vector<UINT>({5,6,9,10}));
    view.Texture2DArray.MipLevels=UINT_MAX;
    Require(texture_read::detail::ViewSubresources(desc,view,indices) && indices==std::vector<UINT>({5,6,7,9,10,11}));
    auto reject=[&](auto mutate){auto bad=view;mutate(bad);indices={123};
        Require(!texture_read::detail::ViewSubresources(desc,bad,indices) && indices.empty());};
    reject([](auto& v){v.Texture2DArray.ArraySize=0;});
    reject([](auto& v){v.Texture2DArray.ArraySize=UINT_MAX;});
    reject([](auto& v){v.Texture2DArray.FirstArraySlice=4;});
    reject([](auto& v){v.Texture2DArray.FirstArraySlice=3;});
    reject([](auto& v){v.Texture2DArray.MipLevels=0;});
    reject([](auto& v){v.Texture2DArray.MostDetailedMip=4;});
    reject([](auto& v){v.Texture2DArray.PlaneSlice=1;});
    reject([](auto& v){v.Texture2DArray.ResourceMinLODClamp=-1;});
    reject([](auto& v){v.Texture2DArray.ResourceMinLODClamp=4;});
    reject([](auto& v){v.Texture2DArray.ResourceMinLODClamp=std::numeric_limits<float>::quiet_NaN();});
    reject([](auto& v){v.Texture2DArray.ResourceMinLODClamp=std::numeric_limits<float>::infinity();});
    reject([](auto& v){v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBE;});
    auto scalar=View(desc);Require(!texture_read::detail::ViewSubresources(desc,scalar,indices));
    desc.DepthOrArraySize=1;view.Texture2DArray={0,UINT_MAX,0,1,0,0};
    Require(texture_read::detail::ViewSubresources(desc,view,indices) && indices==std::vector<UINT>({0,1,2,3}));
}
void ViewMipRanges(){
    const auto desc=Texture();auto view=View(desc);UINT first=99,count=99;
    auto valid=[&](UINT expectedFirst,UINT expectedCount){
        Require(texture_read::detail::ViewMips(desc,view,first,count));
        Require(first==expectedFirst && count==expectedCount);
    };
    valid(0,9);view.Texture2D.MostDetailedMip=3;valid(3,6);
    view.Texture2D.MipLevels=2;valid(3,2);
    view.Texture2D.MostDetailedMip=8;view.Texture2D.MipLevels=UINT_MAX;valid(8,1);
    view.Texture2D.MipLevels=1;valid(8,1);
    auto reject=[&](auto mutate){auto bad=View(desc);mutate(bad);first=count=99;
        Require(!texture_read::detail::ViewMips(desc,bad,first,count) && !first && !count);};
    reject([](auto& v){v.Texture2D.MipLevels=0;});
    reject([](auto& v){v.Texture2D.MipLevels=10;});
    reject([](auto& v){v.Texture2D.MostDetailedMip=8;v.Texture2D.MipLevels=2;});
    reject([](auto& v){v.Texture2D.MostDetailedMip=9;});
    reject([](auto& v){v.Texture2D.MostDetailedMip=UINT_MAX;});
    reject([](auto& v){v.Texture2D.MostDetailedMip=1;v.Texture2D.MipLevels=UINT_MAX-1;});
    reject([](auto& v){v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;});
    reject([](auto& v){v.Format=DXGI_FORMAT_BC1_UNORM_SRGB;});
    reject([](auto& v){v.Shader4ComponentMapping=0;});
    reject([](auto& v){v.Texture2D.PlaneSlice=1;});
    auto malformed=desc;malformed.MipLevels=0;first=count=99;
    Require(!texture_read::detail::ViewMips(malformed,View(desc),first,count) && !first && !count);
    auto r8=Texture(DXGI_FORMAT_R8_UNORM);r8.Height=2;r8.MipLevels=1;
    Require(texture_read::detail::ViewMips(r8,View(r8),first,count) && first==0 && count==1);
}
void FootprintRanges(){
    uint64_t bytes{};
    for(const auto format:{DXGI_FORMAT_BC1_UNORM_SRGB,DXGI_FORMAT_BC7_UNORM_SRGB}){
        const uint64_t blockBytes=format==DXGI_FORMAT_BC1_UNORM_SRGB?8:16;
        for(const UINT size:{1u,2u,4u}){
            const D3D12_PLACED_SUBRESOURCE_FOOTPRINT terminal{512,{format,size,size,1,256}};
            Require(texture_read::detail::FootprintRange(terminal,1,blockBytes,512+blockBytes,bytes) && bytes==blockBytes);
            Require(!texture_read::detail::FootprintRange(terminal,1,blockBytes,511+blockBytes,bytes) && !bytes);
        }
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{1024,{format,8,8,1,256}};
        const auto extent=256+2*blockBytes;
        Require(texture_read::detail::FootprintRange(layout,2,2*blockBytes,1024+extent,bytes) && bytes==extent);
        Require(texture_read::detail::FootprintRange(layout,2,2*blockBytes,1536,bytes) && bytes==extent);
        Require(!texture_read::detail::FootprintRange(layout,2,2*blockBytes,1023+extent,bytes) && !bytes);
    }
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{512,{DXGI_FORMAT_BC7_UNORM_SRGB,8,8,1,256}};
    auto reject=[&](auto mutate,UINT rows=2,uint64_t rowBytes=32,uint64_t capacity=1024){
        auto bad=layout;mutate(bad);bytes=99;
        Require(!texture_read::detail::FootprintRange(bad,rows,rowBytes,capacity,bytes) && !bytes);
    };
    reject([](auto& f){f.Offset=1;});
    reject([](auto& f){f.Offset=1024;});
    reject([](auto& f){f.Footprint.Depth=0;});
    reject([](auto& f){f.Footprint.Depth=2;});
    reject([](auto& f){f.Footprint.RowPitch=0;});
    reject([](auto& f){f.Footprint.RowPitch=255;});
    reject([](auto&){},0);reject([](auto&){},2,0);reject([](auto&){},2,257);
    reject([](auto&){},2,UINT64_MAX);reject([](auto&){},UINT_MAX);
    reject([](auto& f){f.Offset=UINT64_MAX-511;},2,256,UINT64_MAX);
    const D3D12_PLACED_SUBRESOURCE_FOOTPRINT nearEnd{UINT64_MAX-511,{DXGI_FORMAT_BC7_UNORM_SRGB,1,1,1,256}};
    Require(texture_read::detail::FootprintRange(nearEnd,1,16,UINT64_MAX,bytes) && bytes==16);
}
void WritableStates(){
    for(const UINT value:{UINT(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),UINT(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        texture_read::detail::ShaderReads,texture_read::detail::AllowedReads}){
        Require(texture_read::detail::SourceReadState({value,1},16));
        Require(texture_read::detail::SourceReadState({value,2},1));
        Require(!texture_read::detail::SourceReadState({value,2},16));
        Require(!texture_read::detail::SourceReadState({value,1},0));
    }
    Require(!texture_read::detail::SourceReadState({UINT(D3D12_RESOURCE_STATE_COPY_SOURCE),1},4));
    Require(!texture_read::detail::SourceReadState({texture_read::detail::ShaderReads|D3D12_RESOURCE_STATE_RENDER_TARGET,1},4));
    Require(!texture_read::detail::SourceReadState({texture_read::detail::ShaderReads,5},4));
    for(const auto expected:{D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_DEPTH_WRITE}){
        Require(texture_read::detail::WritableState({uint32_t(expected),1},expected));
        for(uint32_t status=0;status<=9;++status){
            if(status!=1)Require(!texture_read::detail::WritableState({uint32_t(expected),status},expected));
        }
        for(const uint32_t state:{0u,4u,8u,16u,20u,32u,0xc0u,0x400u,0x800u,0x80000000u}){
            if(state!=uint32_t(expected))Require(!texture_read::detail::WritableState({state,1},expected));
        }
    }
    for(const uint32_t expected:{0u,8u,20u,32u,0xc0u,0x400u,0x800u,UINT_MAX})
        Require(!texture_read::detail::WritableState({expected,1},expected));
}
void WritableIdentityMetadata(){
    // Pure metadata only: no COM objects or simulated native ownership.
    texture_read::WritableTargetIdentity original;
    original.creation.kind=2;original.creation.initialStateKnown=true;
    original.creation.initialState=D3D12_RESOURCE_STATE_RENDER_TARGET;
    original.creation.heapOffset=65536;original.creation.allocationBytes=131072;
    original.native={0x1000000,0x12340000,2};
    original.recording.generation=7;original.recording.context=0x2000;
    original.recording.list=0x3000;original.recording.registry=original.native.registry;
    original.originalContext={0x2000,0x4000,0x3000};
    original.resourceDesc=Texture(DXGI_FORMAT_R8G8B8A8_UNORM);
    original.resourceDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    original.resourceHeapProperties.Type=D3D12_HEAP_TYPE_DEFAULT;
    original.resourceHeapProperties.CreationNodeMask=original.resourceHeapProperties.VisibleNodeMask=1;
    original.resourceHeapFlags=D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    original.heapDesc={1048576,original.resourceHeapProperties,65536,original.resourceHeapFlags};
    original.expectedWritableState=D3D12_RESOURCE_STATE_RENDER_TARGET;
    original.allocatorOwnerObservation=0x77770000;
    std::memcpy(original.nativeRecord.data(),&original.native.resource,8);original.nativeRecord[8]=3;
    std::memcpy(original.nativeRecord.data()+0x10,&original.allocatorOwnerObservation,8);
    const auto observation=texture_read::detail::WritableRecord(original.nativeRecord);
    Require(observation.resource==original.native.resource && observation.owner==original.allocatorOwnerObservation && observation.kind==3);
    Require(texture_read::detail::SameWritableIdentityMetadata(original,original));
    const auto changed=[&](auto change){auto current=original;change(current);
        Require(!texture_read::detail::SameWritableIdentityMetadata(original,current));};
    changed([](auto& x){x.creation.kind=1;});
    changed([](auto& x){x.creation.initialStateKnown=false;});
    changed([](auto& x){x.creation.initialState=D3D12_RESOURCE_STATE_DEPTH_WRITE;});
    changed([](auto& x){++x.creation.heapOffset;});changed([](auto& x){++x.creation.allocationBytes;});
    changed([](auto& x){++x.native.registry;});changed([](auto& x){++x.native.resource;});
    changed([](auto& x){++x.native.handle;});changed([](auto& x){++x.native.reserved;});
    changed([](auto& x){++x.recording.generation;});changed([](auto& x){++x.recording.context;});
    changed([](auto& x){++x.recording.list;});changed([](auto& x){++x.recording.registry;});
    changed([](auto& x){++x.originalContext.context;});changed([](auto& x){++x.originalContext.state;});
    changed([](auto& x){++x.originalContext.list;});
    changed([](auto& x){x.resourceDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;});
    changed([](auto& x){++x.resourceDesc.Alignment;});changed([](auto& x){++x.resourceDesc.Width;});
    changed([](auto& x){++x.resourceDesc.Height;});changed([](auto& x){++x.resourceDesc.DepthOrArraySize;});
    changed([](auto& x){++x.resourceDesc.MipLevels;});
    changed([](auto& x){x.resourceDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;});
    changed([](auto& x){++x.resourceDesc.SampleDesc.Count;});changed([](auto& x){++x.resourceDesc.SampleDesc.Quality;});
    changed([](auto& x){x.resourceDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;});
    changed([](auto& x){x.resourceDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;});
    changed([](auto& x){++x.heapDesc.SizeInBytes;});changed([](auto& x){++x.heapDesc.Alignment;});
    changed([](auto& x){x.heapDesc.Flags=D3D12_HEAP_FLAG_NONE;});
    const auto heapChanges=[&](auto property){
        changed([&](auto& x){property(x.heapDesc.Properties);});
        changed([&](auto& x){property(x.resourceHeapProperties);});
    };
    heapChanges([](auto& x){x.Type=D3D12_HEAP_TYPE_CUSTOM;});
    heapChanges([](auto& x){x.CPUPageProperty=D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;});
    heapChanges([](auto& x){x.MemoryPoolPreference=D3D12_MEMORY_POOL_L1;});
    heapChanges([](auto& x){++x.CreationNodeMask;});heapChanges([](auto& x){++x.VisibleNodeMask;});
    changed([](auto& x){x.resourceHeapFlags=D3D12_HEAP_FLAG_NONE;});
    changed([](auto& x){x.expectedWritableState=D3D12_RESOURCE_STATE_DEPTH_WRITE;});
    changed([](auto& x){++x.allocatorOwnerObservation;});
    for(size_t i=0;i<original.nativeRecord.size();++i){
        changed([&](auto& x){x.nativeRecord[i]^=1;});
        auto record=original.nativeRecord;record[i]^=1;
        const bool identityByte=i<9 || (i>=0x10 && i<0x18);
        Require((texture_read::detail::WritableRecord(record)!=observation)==identityByte);
    }
}
int main()try{
    TextureShapes();ViewMipRanges();ArraySubresources();FootprintRanges();WritableStates();WritableIdentityMetadata();
    const texture_read::detail::Allocation committed{0,0,0,1},placed{0x100,1024,256,2};
    Require(texture_read::detail::KnownAllocation(committed) && texture_read::detail::KnownAllocation(placed));
    Require(!texture_read::detail::KnownAllocation({0x100,UINT64_MAX,256,2}) &&
        !texture_read::detail::KnownAllocation({0x100,0,UINT64_MAX,2}) && !texture_read::detail::KnownAllocation({}));
    auto alias=[&](const auto& source,uint64_t before,const auto& from,uint64_t after,const auto& to){
        return texture_read::detail::AliasAffects(1,source,before,from,after,to);
    };
    const texture_read::detail::Allocation unknown{},otherHeap{0x200,1024,256,2},disjoint{0x100,1280,256,2},overlap{0x100,1200,256,2};
    Require(!alias(placed,0,unknown,2,otherHeap) && !alias(placed,2,otherHeap,0,unknown));
    Require(!alias(placed,0,unknown,2,disjoint) && alias(placed,0,unknown,2,overlap));
    Require(alias(placed,2,overlap,0,unknown) && alias(placed,0,unknown,0,unknown));
    Require(alias(placed,2,unknown,0,unknown) && alias(placed,2,otherHeap,3,unknown));
    Require(!alias(committed,0,unknown,0,unknown) && !alias(committed,2,unknown,3,overlap));
    Require(!alias(committed,2,placed,0,unknown) && !alias(committed,0,unknown,2,placed));
    Require(alias(unknown,0,unknown,0,unknown) && alias(unknown,2,unknown,0,unknown));
    Require(alias(unknown,0,unknown,2,placed) && alias(placed,0,unknown,2,unknown));
    Require(alias(committed,0,unknown,1,committed) && alias(placed,1,placed,2,disjoint));
    const texture_read::detail::Allocation unknownRange{0x100,0,0,2},unrelatedUnknownRange{0x200,0,0,2};
    Require(alias(placed,0,unknown,2,unknownRange) && !alias(placed,0,unknown,2,unrelatedUnknownRange));
    auto uploaded=[] {
        texture_read::detail::CreationProof proof{true};proof.Written(101);
        proof.BeforeSubmit(101,true);proof.Submitted(101,true);return proof;
    };
    texture_read::detail::CreationProof proof{true};Require(!proof.Acquire());
    proof.Written(101);Require(proof.valid && !proof.Acquire());
    proof.BeforeSubmit(101,true);Require(proof.pending && !proof.Acquire());
    proof.Submitted(101,true);Require(proof.uploaded && proof.Acquire());
    proof.Written(101);Require(!proof.valid && !proof.Acquire());
    proof={true};proof.Written(0);proof.Written(101);Require(!proof.valid && !proof.Acquire());
    proof={true};proof.Written(101);proof.Written(202);Require(proof.valid && !proof.Acquire());
    proof.BeforeSubmit(101,true);proof.Written(303);proof.Written(202);
    Require(proof.valid && proof.pending && !proof.Acquire());
    proof.BeforeSubmit(202,true);proof.Submitted(202,true);proof.Abandon(202);
    proof.Submitted(101,true);proof.Abandon(101);Require(proof.valid && !proof.Acquire());
    proof.BeforeSubmit(303,true);proof.Submitted(303,true);Require(proof.Acquire() && proof.Acquire());
    proof={true};proof.Written(101);proof.BeforeSubmit(101,false);proof.Submitted(101,true);Require(!proof.valid && !proof.Acquire());
    proof={true};proof.Written(101);proof.Submitted(101,true);Require(!proof.valid && !proof.Acquire());
    proof={true};proof.Written(101);proof.BeforeSubmit(101,true);proof.BeforeSubmit(101,true);Require(!proof.valid);
    proof={true};proof.Written(101);proof.Abandon(101);Require(!proof.valid && !proof.Acquire());
    proof=uploaded();proof.Abandon(101);Require(proof.Acquire());
    proof=uploaded();proof.BeforeSubmit(101,true);Require(!proof.valid && !proof.Acquire());
    proof=uploaded();Require(proof.Acquire());proof.BeforeSubmit(101,true);Require(!proof.valid);
    proof=uploaded();proof.BeforeSubmit(202,true);proof.Abandon(202);Require(!proof.valid && !proof.Acquire());
    proof=uploaded();proof.Submitted(202,true);Require(!proof.valid && !proof.Acquire());
    proof=uploaded();const auto firstEpoch=proof.epoch;proof.Written(202);
    Require(proof.valid && proof.epoch==firstEpoch+1 && !proof.Matches(firstEpoch) && !proof.Acquire());
    proof.BeforeSubmit(202,true);proof.Submitted(202,true);proof.Abandon(101);
    proof.Written(303);proof.BeforeSubmit(303,true);proof.Submitted(303,true);
    Require(proof.valid && proof.Acquire());
    for(const auto recording:{0ull,101ull,202ull}){
        proof=uploaded();Require(proof.Acquire());const auto acquiredEpoch=proof.epoch;
        proof.Written(recording);proof.BeforeSubmit(recording,true);proof.Submitted(recording,true);
        Require(!proof.valid && !proof.Acquire() && proof.epoch==acquiredEpoch);
    }
    proof={true};for(uint64_t recording=1;recording<=128;++recording)proof.Written(recording);
    Require(proof.valid && !proof.Acquire());proof.Written(129);Require(!proof.valid && !proof.Acquire());
    proof={};proof.Written(101);Require(!proof.valid && !proof.Acquire());
    proof=uploaded();proof.Invalidate();Require(!proof.Acquire());
    proof=uploaded();Require(proof.Acquire());proof.Invalidate();Require(!proof.valid);
    std::array<uint8_t,0x58> fixedRecord{};const uint64_t allocationOwner=0x77770000;const uint32_t shaderReads=0xc0;
    fixedRecord[8]=1;std::memcpy(fixedRecord.data()+0x10,&allocationOwner,8);std::memcpy(fixedRecord.data()+0x48,&shaderReads,4);
    for(const uint16_t flags:{0x41u,0xc1u}){std::memcpy(fixedRecord.data()+0x56,&flags,2);Require(texture_read::detail::FixedOwner(fixedRecord)==allocationOwner);}
    for(const uint16_t flags:{0u,0x40u,0x43u,0x45u,0x49u,0x141u}){
        std::memcpy(fixedRecord.data()+0x56,&flags,2);Require(!texture_read::detail::FixedOwner(fixedRecord));
    }
    fixedRecord[0x56]=0x41;fixedRecord[0x57]=0;
    for(const auto kind:{0u,2u,3u}){fixedRecord[8]=uint8_t(kind);Require(!texture_read::detail::FixedOwner(fixedRecord));}
    fixedRecord[8]=1;fixedRecord[0x48]=0x40;Require(!texture_read::detail::FixedOwner(fixedRecord));
    fixedRecord[0x48]=0xc0;std::memset(fixedRecord.data()+0x10,0,8);Require(!texture_read::detail::FixedOwner(fixedRecord));
    uint32_t required{};
    for(const auto previous:{0u,0x40u,0x80u,0xc0u,0x800u,0x840u,0x880u,0x8c0u}){
        Require(texture_read::detail::CopyReadMask({previous,1},required) && required==0x8c0);
        Require(texture_read::detail::CopyReadMask({previous,2},required) && required==0x8c0);
    }
    Require(texture_read::detail::CopyReadMask({0,5},required) && required==0x8c0);
    Require(!texture_read::detail::HasReadState({0x8c0,5},0x8c0));
    Require(!texture_read::detail::CopyReadMask({D3D12_RESOURCE_STATE_COPY_DEST,1},required) && !required);
    for(const auto status:{0u,3u,4u,6u,7u,8u}){
        Require(!texture_read::detail::CopyReadMask({0xc0,status},required) && !required);
        Require(!texture_read::detail::HasReadState({0x8c0,status},0x8c0));
    }
    Require(texture_read::detail::HasReadState({0x8c0,1},0x8c0));
    Require(texture_read::detail::HasReadState({0x8c0,2},0xc0));
    Require(texture_read::detail::HasReadState({0xc0,1},0xc0));
    Require(!texture_read::detail::HasReadState({0xc0,1},0x8c0));
    Require(!texture_read::detail::HasReadState({0x800,1},0xc0));
    Require(!texture_read::detail::HasReadState({0x840,1},0x8c0));
    Require(!texture_read::detail::HasReadState({0x880,1},0x8c0));
    Require(!texture_read::detail::HasReadState({0x8c8,1},0x8c0));
    const texture_read::NativeTexture source{0x1000000,0x12340000,2};const uint64_t context=0x2000,list=0x5555;
    Memory memory;std::array<uint8_t,0x58> record{};std::memcpy(record.data(),&source.resource,8);
    const auto address=source.registry+0x2f1d8+(source.handle-1)*0xb0;
    Put(memory,address,record);Put(memory,context+0x30,list);Put(memory,context+0x534,uint32_t(0));
    struct Map{uint64_t buckets;uint32_t count,bucketCount;uint64_t data;uint32_t capacity,stride;};
    Map map{0x4000,1,4,0x5000,2,20};std::array<uint32_t,4> buckets{UINT_MAX,UINT_MAX,0,UINT_MAX};
    std::array<uint32_t,5> entry{UINT_MAX,2,2,65535,0xc0};
    auto state=[&]{return texture_read::ReadState(source,context,list,Read,&memory);};
    Put(memory,context+0x538,map);Put(memory,map.buckets,buckets);Put(memory,map.data,entry);
    Require(state().status==1 && state().value==0xc0);
    for(const auto expected:{D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_DEPTH_WRITE}){
        entry[4]=expected;Put(memory,map.data,entry);
        Require(texture_read::detail::WritableState(state(),expected));
        Put(memory,context+0x534,uint32_t(1));Require(!texture_read::detail::WritableState(state(),expected));
        Put(memory,context+0x534,uint32_t(0));
        record[0x56]=0x40;Put(memory,address,record);Require(!texture_read::detail::WritableState(state(),expected));
        record[0x56]=0;Put(memory,address,record);
        Put(memory,context+0x30,list+1);Require(!texture_read::detail::WritableState(state(),expected));
        Put(memory,context+0x30,list);
        record[0]^=1;Put(memory,address,record);Require(!texture_read::detail::WritableState(state(),expected));
        record[0]^=1;Put(memory,address,record);
        Require(texture_read::detail::WritableState(state(),expected));
    }
    entry[4]=0x8c0;Put(memory,map.data,entry);Require(texture_read::detail::HasReadState(state(),0x8c0));
    entry[4]=0xc0;Put(memory,map.data,entry);
    for(const auto stride:{20u,24u,32u}){map.stride=stride;Put(memory,context+0x538,map);Require(state().status==1);}
    entry[3]=(1u<<16)|2;entry[4]=0x80000000;Put(memory,map.data,entry);
    Put(memory,context+0x598,uint64_t(0x6000));Put(memory,context+0x5a0,uint32_t(3));
    Put(memory,context+0x5a4,uint32_t(3));Put(memory,0x6000,std::array<uint32_t,3>{0,0,0xc0});
    Require(state().status==2 && state().value==0xc0);
    Put(memory,0x6000,std::array<uint32_t,3>{0,0,0x8c0});Require(texture_read::detail::HasReadState(state(),0x8c0));
    Put(memory,0x6000,std::array<uint32_t,3>{0,0,0xc0});
    Put(memory,context+0x5a4,uint32_t(2));Require(state().status==7);Put(memory,context+0x5a4,uint32_t(3));
    Put(memory,context+0x5a0,uint32_t(2));Require(state().status==7);Put(memory,context+0x5a0,uint32_t(3));
    Put(memory,context+0x598,uint64_t(0));Put(memory,8,uint32_t(0xc0));Require(state().status==7);
    Put(memory,context+0x598,uint64_t(0x6000));
    entry[3]=(2u<<16)|2;Put(memory,map.data,entry);Require(state().status==7);
    entry[3]=(1u<<16)|65535;Put(memory,map.data,entry);Require(state().status==7);
    entry[3]=(1u<<16)|2;Put(memory,map.data,entry);
    Put(memory,0x6000,std::array<uint32_t,3>{0,0,0x80000000});Require(state().status==7);
    entry[4]=0x800;Put(memory,map.data,entry);Require(state().status==1 && state().value==0x800);
    Put(memory,context+0x534,uint32_t(1));Require(state().status==4);Put(memory,context+0x534,uint32_t(0));
    record[0x56]=0x40;Put(memory,address,record);Require(state().status==8);record[0x56]=0;Put(memory,address,record);
    Put(memory,context+0x30,list+1);Require(state().status==3);Put(memory,context+0x30,list);
    record[0]^=1;Put(memory,address,record);Require(state().status==3);record[0]^=1;Put(memory,address,record);
    memory.erase(context+0x538);Require(state().status==6);
    Put(memory,context+0x538,Map{});Require(state().status==5);
    auto malformedEmpty=Map{};malformedEmpty.capacity=1;Put(memory,context+0x538,malformedEmpty);Require(state().status==6);
    map.count=0;Put(memory,context+0x538,map);Require(state().status==5);map.count=1;
    map.stride=16;Put(memory,context+0x538,map);Require(state().status==6);map.stride=20;
    map.count=3;Put(memory,context+0x538,map);Require(state().status==6);map.count=1;
    map.count=2;map.bucketCount=1;Put(memory,context+0x538,map);Require(state().status==6);map.count=1;map.bucketCount=4;
    Put(memory,context+0x538,map);entry[1]=3;Put(memory,map.data,entry);Require(state().status==5);
    entry[0]=0;Put(memory,map.data,entry);Require(state().status==6);
    buckets[2]=map.capacity;Put(memory,map.buckets,buckets);Require(state().status==6);
    buckets[2]=UINT_MAX;Put(memory,map.buckets,buckets);Require(state().status==5);
    buckets[2]=0;Put(memory,map.buckets,buckets);memory.erase(map.data);Require(state().status==6);
    map.count=2;Put(memory,context+0x538,map);
    const std::array<uint32_t,10> collision{1,6,6,65535,0x800,UINT_MAX,2,2,65535,0xc0};
    Put(memory,map.data,collision);Require(state().status==1 && state().value==0xc0);
    map.count=65;map.capacity=65;map.bucketCount=65;Put(memory,context+0x538,map);
    std::array<uint32_t,65> longBuckets{};longBuckets.fill(UINT_MAX);longBuckets[2]=0;Put(memory,map.buckets,longBuckets);
    std::array<uint32_t,65*5> longChain{};
    for(UINT i=0;i<65;++i){const UINT handle=2+(i+1)*65;longChain[i*5]=i==64?UINT_MAX:i+1;
        longChain[i*5+1]=handle;longChain[i*5+2]=handle;longChain[i*5+3]=65535;longChain[i*5+4]=0xc0;}
    Put(memory,map.data,longChain);Require(state().status==6);
    Require(texture_read::ReadState(source,0,list,Read,&memory).status==3);
    Require(texture_read::ReadState(source,UINT64_MAX,list,Read,&memory).status==3);
    D3D12_RESOURCE_DESC material{};material.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    material.Width=material.Height=1024;material.DepthOrArraySize=6;material.MipLevels=11;material.SampleDesc.Count=1;
    material.Format=DXGI_FORMAT_BC5_TYPELESS;
    Require(texture_read::detail::ImmutableShape(material) && !texture_read::detail::Shape(material));
    for(const auto flags:{D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL}){
        material.Flags=flags;Require(!texture_read::detail::ImmutableShape(material));}
    material.Flags=D3D12_RESOURCE_FLAG_NONE;material.SampleDesc.Count=2;Require(!texture_read::detail::ImmutableShape(material));
    material.SampleDesc.Count=1;material.Format=DXGI_FORMAT_UNKNOWN;Require(!texture_read::detail::ImmutableShape(material));
    material.Format=DXGI_FORMAT_BC7_UNORM;material.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;Require(!texture_read::detail::ImmutableShape(material));
    std::puts("PASS native texture creation receipts, writable metadata equality, alias scope, fixed class, immutable asset eligibility, combined read policy and native state tables");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
