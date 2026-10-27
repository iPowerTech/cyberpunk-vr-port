#include "Render/SourceDescriptorViews.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace cvr::stereo;
using descriptors::Value;
using namespace descriptor_views;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint64_t Bits(float value){return std::bit_cast<uint32_t>(value);}
static Value Srv(const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,uint64_t cookie){
    Value value{};value[0]=2;value[1]=cookie?1:0;value[2]=cookie;
    if(!desc){value[1]|=2;if(!cookie)value[0]=0;return value;}
    value[4]=desc->Format;value[5]=desc->ViewDimension;value[6]=desc->Shader4ComponentMapping;
    switch(desc->ViewDimension){
    case D3D12_SRV_DIMENSION_BUFFER:value[7]=desc->Buffer.FirstElement;value[8]=desc->Buffer.NumElements;value[9]=desc->Buffer.StructureByteStride;value[10]=desc->Buffer.Flags;break;
    case D3D12_SRV_DIMENSION_TEXTURE1D:value[7]=desc->Texture1D.MostDetailedMip;value[8]=desc->Texture1D.MipLevels;value[9]=Bits(desc->Texture1D.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:value[7]=desc->Texture1DArray.MostDetailedMip;value[8]=desc->Texture1DArray.MipLevels;value[9]=desc->Texture1DArray.FirstArraySlice;value[10]=desc->Texture1DArray.ArraySize;value[11]=Bits(desc->Texture1DArray.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:value[7]=desc->Texture2D.MostDetailedMip;value[8]=desc->Texture2D.MipLevels;value[9]=desc->Texture2D.PlaneSlice;value[10]=Bits(desc->Texture2D.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:value[7]=desc->Texture2DArray.MostDetailedMip;value[8]=desc->Texture2DArray.MipLevels;value[9]=desc->Texture2DArray.FirstArraySlice;value[10]=desc->Texture2DArray.ArraySize;value[11]=desc->Texture2DArray.PlaneSlice;value[12]=Bits(desc->Texture2DArray.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMS:break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:value[7]=desc->Texture2DMSArray.FirstArraySlice;value[8]=desc->Texture2DMSArray.ArraySize;break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:value[7]=desc->Texture3D.MostDetailedMip;value[8]=desc->Texture3D.MipLevels;value[9]=Bits(desc->Texture3D.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:value[7]=desc->TextureCube.MostDetailedMip;value[8]=desc->TextureCube.MipLevels;value[9]=Bits(desc->TextureCube.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:value[7]=desc->TextureCubeArray.MostDetailedMip;value[8]=desc->TextureCubeArray.MipLevels;value[9]=desc->TextureCubeArray.First2DArrayFace;value[10]=desc->TextureCubeArray.NumCubes;value[11]=Bits(desc->TextureCubeArray.ResourceMinLODClamp);break;
    case D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE:value[7]=desc->RaytracingAccelerationStructure.Location;break;
    default:value[0]=0;
    }
    return value;
}
static Value Uav(const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,uint64_t cookie,uint64_t counter=0){
    Value value{};value[0]=3;value[1]=cookie?1:0;value[2]=cookie;value[3]=counter;
    if(!desc){value[1]|=2;if(!cookie)value[0]=0;return value;}
    value[4]=desc->Format;value[5]=desc->ViewDimension;
    switch(desc->ViewDimension){
    case D3D12_UAV_DIMENSION_BUFFER:value[7]=desc->Buffer.FirstElement;value[8]=desc->Buffer.NumElements;value[9]=desc->Buffer.StructureByteStride;value[10]=desc->Buffer.CounterOffsetInBytes;value[11]=desc->Buffer.Flags;break;
    case D3D12_UAV_DIMENSION_TEXTURE1D:value[7]=desc->Texture1D.MipSlice;break;
    case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:value[7]=desc->Texture1DArray.MipSlice;value[8]=desc->Texture1DArray.FirstArraySlice;value[9]=desc->Texture1DArray.ArraySize;break;
    case D3D12_UAV_DIMENSION_TEXTURE2D:value[7]=desc->Texture2D.MipSlice;value[8]=desc->Texture2D.PlaneSlice;break;
    case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:value[7]=desc->Texture2DArray.MipSlice;value[8]=desc->Texture2DArray.FirstArraySlice;value[9]=desc->Texture2DArray.ArraySize;value[10]=desc->Texture2DArray.PlaneSlice;break;
    case D3D12_UAV_DIMENSION_TEXTURE3D:value[7]=desc->Texture3D.MipSlice;value[8]=desc->Texture3D.FirstWSlice;value[9]=desc->Texture3D.WSize;break;
    default:value[0]=0;
    }
    return value;
}
static Value Sampler(const D3D12_SAMPLER_DESC& desc){
    Value value{};value[0]=4;value[4]=desc.Filter;value[5]=desc.AddressU;value[6]=desc.AddressV;value[7]=desc.AddressW;
    value[8]=Bits(desc.MipLODBias);value[9]=desc.MaxAnisotropy;value[10]=desc.ComparisonFunc;
    for(UINT i=0;i<4;++i)value[11+i]=Bits(desc.BorderColor[i]);value[15]=Bits(desc.MinLOD);value[16]=Bits(desc.MaxLOD);return value;
}
static void Metadata(Value& value,const ResourceMetadata& metadata){
    if(!metadata.described)return;value[1]|=4;
    value[13]=uint64_t(metadata.dimension)|(uint64_t(metadata.format)<<32);value[14]=metadata.width;
    value[15]=uint64_t(metadata.height)|(uint64_t(metadata.depthOrArraySize)<<32)|(uint64_t(metadata.mipLevels)<<48);
    value[16]=uint64_t(metadata.flags)|(uint64_t(metadata.layout)<<32);value[17]=uint64_t(metadata.samples.Count)|(uint64_t(metadata.samples.Quality)<<32);
    if(metadata.heapDescribed){value[1]|=8;value[18]=uint64_t(metadata.heapType)|(uint64_t(metadata.cpuPage)<<16)|(uint64_t(metadata.memoryPool)<<32);}
    value[19]=metadata.bufferGpuVA;
}
static D3D12_SHADER_RESOURCE_VIEW_DESC SrvView(D3D12_SRV_DIMENSION dimension){
    D3D12_SHADER_RESOURCE_VIEW_DESC desc;std::memset(&desc,0xCD,sizeof(desc));
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.ViewDimension=dimension;desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch(dimension){
    case D3D12_SRV_DIMENSION_BUFFER:desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.Buffer={(1ull<<40)+3,17,0,D3D12_BUFFER_SRV_FLAG_RAW};break;
    case D3D12_SRV_DIMENSION_TEXTURE1D:desc.Texture1D={2,UINT_MAX,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:desc.Texture1DArray={2,UINT_MAX,7,UINT_MAX,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:desc.Texture2D={2,UINT_MAX,1,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:desc.Texture2DArray={2,UINT_MAX,7,UINT_MAX,1,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMS:break;
    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:desc.Texture2DMSArray={7,UINT_MAX};break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:desc.Texture3D={2,UINT_MAX,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURECUBE:desc.TextureCube={2,UINT_MAX,-0.0f};break;
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:desc.TextureCubeArray={2,UINT_MAX,6,2,-0.0f};break;
    default:throw std::runtime_error("Fixture SRV dimension");
    }
    return desc;
}
static void SrvRoundTrip(){
    for(UINT dimension=D3D12_SRV_DIMENSION_BUFFER;dimension<=D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;++dimension){
        const auto desc=SrvView(D3D12_SRV_DIMENSION(dimension));
        for(const uint64_t cookie:{0ull,19ull}){
            const auto value=Srv(&desc,cookie);SrvResult result;
            Require(DecodeSrv(value,result) && !result.implicit && result.Description()==&result.view && result.resourceCookie==cookie,"Explicit/null SRV decode failed");
            Require(Srv(result.Description(),result.resourceCookie)==value,"SRV creation description did not round-trip exactly");
        }
    }
    auto desc=SrvView(D3D12_SRV_DIMENSION_BUFFER);desc.Format=DXGI_FORMAT_UNKNOWN;desc.Buffer.StructureByteStride=12;desc.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;
    auto value=Srv(&desc,41);value[1]|=12;value[13]=uint64_t(D3D12_RESOURCE_DIMENSION_BUFFER)|(uint64_t(DXGI_FORMAT_UNKNOWN)<<32);value[14]=1ull<<48;
    value[15]=uint64_t(1)|(uint64_t(1)<<32)|(uint64_t(1)<<48);value[16]=uint64_t(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)|(uint64_t(D3D12_TEXTURE_LAYOUT_ROW_MAJOR)<<32);
    value[17]=1;value[18]=D3D12_HEAP_TYPE_DEFAULT;value[19]=(1ull<<48)+256;
    SrvResult result;Require(DecodeSrv(value,result) && result.metadata.described && result.metadata.heapDescribed,"SRV optional resource metadata was rejected");
    auto encoded=Srv(result.Description(),result.resourceCookie);Metadata(encoded,result.metadata);
    Require(encoded==value && result.view.Buffer.FirstElement==desc.Buffer.FirstElement && result.metadata.bufferGpuVA==value[19],"Metadata leaked into view fields or lost its bit widths");
    value[1]&=~uint64_t(8);value[18]=0;Require(DecodeSrv(value,result) && !result.metadata.heapDescribed,"Optional missing heap metadata was invented");
    const auto implicit=Srv(nullptr,59);Require(DecodeSrv(implicit,result) && result.implicit && !result.Description() && result.resourceCookie==59,"Implicit nonnull SRV became a guessed explicit default");
}
static void UavRoundTrip(){
    const D3D12_UAV_DIMENSION dimensions[]{D3D12_UAV_DIMENSION_BUFFER,D3D12_UAV_DIMENSION_TEXTURE1D,D3D12_UAV_DIMENSION_TEXTURE1DARRAY,
        D3D12_UAV_DIMENSION_TEXTURE2D,D3D12_UAV_DIMENSION_TEXTURE2DARRAY,D3D12_UAV_DIMENSION_TEXTURE3D};
    for(const auto dimension:dimensions){
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc;std::memset(&desc,0xCD,sizeof(desc));desc.Format=DXGI_FORMAT_R32_FLOAT;desc.ViewDimension=dimension;
        switch(dimension){
        case D3D12_UAV_DIMENSION_BUFFER:desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.Buffer={(1ull<<40)+3,17,0,0,D3D12_BUFFER_UAV_FLAG_RAW};break;
        case D3D12_UAV_DIMENSION_TEXTURE1D:desc.Texture1D={2};break;
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:desc.Texture1DArray={2,7,UINT_MAX};break;
        case D3D12_UAV_DIMENSION_TEXTURE2D:desc.Texture2D={2,1};break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:desc.Texture2DArray={2,7,UINT_MAX,1};break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:desc.Texture3D={2,7,UINT_MAX};break;
        default:throw std::runtime_error("Fixture UAV dimension");
        }
        for(const uint64_t cookie:{0ull,19ull}){const auto value=Uav(&desc,cookie);UavResult result;
            Require(DecodeUav(value,result) && !result.implicit && result.Description()==&result.view && result.resourceCookie==cookie,"Explicit/null UAV decode failed");
            Require(Uav(result.Description(),result.resourceCookie,result.counterCookie)==value,"UAV creation description did not round-trip exactly");}
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_UNKNOWN;desc.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer={(1ull<<40)+3,17,12,1ull<<40,D3D12_BUFFER_UAV_FLAG_NONE};const auto counter=Uav(&desc,19,23);UavResult result;
    Require(DecodeUav(counter,result) && result.counterCookie==23 && Uav(result.Description(),result.resourceCookie,result.counterCookie)==counter,"64-bit UAV counter offset/identity changed");
    const auto implicit=Uav(nullptr,59);Require(DecodeUav(implicit,result) && result.implicit && !result.Description() && result.resourceCookie==59,"Implicit UAV became a guessed default");
}
static void SamplerRoundTrip(){
    for(UINT reduction=0;reduction<4;++reduction)for(UINT combination=0;combination<9;++combination){
        const UINT filter=(reduction<<7)|(combination==8?0x55u:(combination&1)|((combination&2)<<1)|((combination&4)<<2));
        D3D12_SAMPLER_DESC desc{};desc.Filter=D3D12_FILTER(filter);desc.AddressU=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        desc.AddressV=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;desc.AddressW=D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;desc.MipLODBias=-0.0f;
        desc.MaxAnisotropy=combination==8?16:UINT_MAX;
        desc.ComparisonFunc=reduction==1?D3D12_COMPARISON_FUNC_LESS_EQUAL:D3D12_COMPARISON_FUNC(combination&1?9:0);
        const uint32_t bits[]{0x80000000,0x7FC12345,0x7F800000,0xFF800000};for(UINT i=0;i<4;++i)desc.BorderColor[i]=std::bit_cast<float>(bits[i]);
        desc.MinLOD=-D3D12_FLOAT32_MAX;desc.MaxLOD=D3D12_FLOAT32_MAX;
        const auto value=Sampler(desc);SamplerResult result;
        Require(DecodeSampler(value,result) && Sampler(result.view)==value,"Sampler filter/ignored values/exact float bits changed");
    }
    D3D12_SAMPLER_DESC desc{};desc.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;desc.AddressU=desc.AddressV=desc.AddressW=D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    desc.BorderColor[0]=-0.0f;desc.BorderColor[1]=0.25f;desc.BorderColor[2]=0.5f;desc.BorderColor[3]=1;desc.MinLOD=-D3D12_FLOAT32_MAX;desc.MaxLOD=D3D12_FLOAT32_MAX;
    const auto value=Sampler(desc);SamplerResult result;
    Require(DecodeSampler(value,result) && Sampler(result.view)==value,"Active border sampler/zero ignored ComparisonFunc changed");
}
static void CbvAndAcceleration(){
    CbvResult result;Value value{};value[0]=1;value[4]=(1ull<<40)+256;value[5]=65536;
    Require(DecodeCbv(value,result) && result.view.BufferLocation==value[4] && result.view.SizeInBytes==value[5],"Aligned CBV decode failed");
    auto origin=value;origin[2]=71;origin[3]=97;
    Require(DecodeCbv(origin,result) && result.resourceCookie==71 && result.originId==97 &&
        result.view.BufferLocation==value[4] && result.view.SizeInBytes==value[5],"Optional numeric CBV origin labels altered the view or were dropped");
    for(UINT which=0;which<7;++which){auto bad=value;
        switch(which){case 0:bad[0]=0;break;case 1:bad[4]=0;break;case 2:++bad[4];break;case 3:bad[5]=65537;break;case 4:bad[5]=0;break;
        case 5:bad[2]=1;break;case 6:bad[4]=UINT64_MAX-255;break;}
        Require(!DecodeCbv(bad,result),"Unknown/null/unaligned/noncanonical CBV was invented");}
    Require(!DecodeCbv({},result),"Untracked null pDesc became a null CBV");
    origin[2]=0;Require(!DecodeCbv(origin,result),"Incomplete CBV origin/cookie pair accepted");
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};desc.ViewDimension=D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;desc.RaytracingAccelerationStructure.Location=(1ull<<40)+256;
    const auto rtas=Srv(&desc,0);SrvResult srv;AccelerationStructureAddressCertificate certificate{desc.RaytracingAccelerationStructure.Location};
    Require(!DecodeSrv(rtas,srv) && DecodeSrv(rtas,srv,&certificate) && Srv(srv.Description(),srv.resourceCookie)==rtas,"RTAS address was inferred or failed exact certificate match");
    ++certificate.location;Require(!DecodeSrv(rtas,srv,&certificate),"Wrong RTAS address certificate accepted");
    auto unaligned=rtas;++unaligned[7];Require(!DecodeSrv(unaligned,srv,&certificate),"Unaligned certified RTAS location accepted");
    certificate.location=rtas[7];auto resource=rtas;resource[1]=1;resource[2]=19;
    Require(!DecodeSrv(resource,srv,&certificate),"RTAS resource cookie replaced the null API resource operand");
}
static void Rejections(){
    auto desc=SrvView(D3D12_SRV_DIMENSION_TEXTURE2D);const auto srv=Srv(&desc,19);
    for(UINT which=0;which<16;++which){auto value=srv;
        switch(which){case 0:value[0]=0;break;case 1:value[0]=3;break;case 2:value[1]|=16;break;case 3:value[2]=0;break;
        case 4:value[3]=1;break;case 5:value[4]=1ull<<32;break;case 6:value[4]=116;break;case 7:value[5]=0;break;
        case 8:value[5]=1ull<<32;break;case 9:value[6]=0;break;case 10:value[7]|=1ull<<32;break;case 11:value[10]|=1ull<<32;break;
        case 12:value[11]=1;break;case 13:value[13]=1;break;case 14:value[1]|=8;break;case 15:value[4]=DXGI_FORMAT_R32_TYPELESS;break;}
        SrvResult result;result.resourceCookie=777;Require(!DecodeSrv(value,result) && result.resourceCookie==777,"Bad SRV header/width/mapping/unused metadata published output");
    }
    SrvResult srvResult;Require(!DecodeSrv(Srv(nullptr,0),srvResult),"Implicit null SRV accepted");
    auto implicit=Srv(nullptr,19);implicit[4]=DXGI_FORMAT_R32_FLOAT;Require(!DecodeSrv(implicit,srvResult),"Implicit SRV carried an invented view");
    auto overflow=srv;overflow[7]=UINT_MAX;overflow[8]=2;Require(!DecodeSrv(overflow,srvResult),"Finite SRV range wrapped a UINT index");
    auto buffer=SrvView(D3D12_SRV_DIMENSION_BUFFER);auto raw=Srv(&buffer,19);raw[10]=2;Require(!DecodeSrv(raw,srvResult),"Unknown SRV buffer flag accepted");
    raw=Srv(&buffer,19);raw[9]=4;Require(!DecodeSrv(raw,srvResult),"Raw structured SRV accepted");
    raw=Srv(&buffer,19);raw[7]=UINT64_MAX-4;Require(!DecodeSrv(raw,srvResult),"64-bit buffer element range overflow accepted");
    auto metadata=srv;metadata[1]|=12;metadata[13]=D3D12_RESOURCE_DIMENSION_TEXTURE2D;metadata[18]=D3D12_HEAP_TYPE_DEFAULT;
    metadata[19]=256;Require(!DecodeSrv(metadata,srvResult),"Texture metadata became a buffer GPU address");
    metadata[19]=0;metadata[18]|=1ull<<48;Require(!DecodeSrv(metadata,srvResult),"Packed heap metadata narrowing accepted");
    auto unknown=Uav(nullptr,0);UavResult uavResult;Require(!DecodeUav(unknown,uavResult),"Implicit null UAV accepted");
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_UNKNOWN;uav.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer={9,17,12,4096,D3D12_BUFFER_UAV_FLAG_NONE};const auto counter=Uav(&uav,19,23);
    for(UINT which=0;which<11;++which){auto value=counter;
        switch(which){case 0:value[0]=2;break;case 1:value[1]=0;value[2]=0;break;case 2:value[3]=0;break;
        case 3:value[9]=0;break;case 4:value[4]=DXGI_FORMAT_R32_FLOAT;break;case 5:value[10]=4;break;
        case 6:value[11]=1;break;case 7:value[8]=1ull<<32;break;case 8:value[6]=1;break;case 9:value[1]|=4;break;case 10:value[12]=1;break;}
        UavResult result;result.resourceCookie=777;Require(!DecodeUav(value,result) && result.resourceCookie==777,"Malformed UAV/counter/narrowing changed output");
    }
    implicit=Uav(nullptr,19,23);Require(!DecodeUav(implicit,uavResult),"Implicit counter UAV accepted");
    D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_ANISOTROPIC;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy=16;sampler.MaxLOD=D3D12_FLOAT32_MAX;const auto original=Sampler(sampler);
    for(UINT which=0;which<12;++which){auto value=original;
        switch(which){case 0:value[0]=2;break;case 1:value[1]=1;break;case 2:value[4]=0x40;break;case 3:value[5]=0;break;
        case 4:value[8]|=1ull<<32;break;case 5:value[9]=17;break;case 6:value[4]=D3D12_FILTER_COMPARISON_ANISOTROPIC;value[10]=0;break;
        case 7:value[17]=1;break;case 8:value[15]=Bits(1);value[16]=Bits(0);break;case 9:value[10]=(1ull<<32)|1;break;
        case 10:value[5]=D3D12_TEXTURE_ADDRESS_MODE_BORDER;value[11]=Bits(-1);break;
        case 11:value[5]=D3D12_TEXTURE_ADDRESS_MODE_BORDER;value[13]=0x7FC12345;break;}
        SamplerResult result;result.view.MaxAnisotropy=777;Require(!DecodeSampler(value,result) && result.view.MaxAnisotropy==777,"Invalid sampler type/width/filter/range/unused words published output");
    }
}
int main()try{
    static_assert(sizeof(Value)==20*sizeof(uint64_t));
    SrvRoundTrip();UavRoundTrip();SamplerRoundTrip();CbvAndAcceleration();Rejections();
    std::puts("PASS pure canonical 20Q CBV/SRV/UAV/sampler decoders, all recorded unions, exact float bits, implicit/typed-null/counter/metadata separation and rejected malformed inputs");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
