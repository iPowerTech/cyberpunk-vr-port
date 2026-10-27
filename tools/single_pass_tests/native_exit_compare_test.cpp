#include "Render/NativePreparedPair.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cvr::stereo::prepared_pair;
namespace detail=cvr::stereo::prepared_pair::native_exit_detail;
static void Require(bool value,const char* message,const std::source_location at=std::source_location::current()){
    if(!value)throw std::runtime_error(std::string(message)+" at line "+std::to_string(at.line()));
}
static NativeExitPlane Plane(UINT slot,UINT plane,UINT stride=4,UINT64 offset=512){
    NativeExitPlane row{slot,plane,2,stride,slot==4 && plane && stride==8?4u:0u,slot==4 && plane?1u:4u,{}};
    row.layout.Offset=offset;
    const auto format=slot<2?DXGI_FORMAT_R10G10B10A2_UNORM:slot==2?DXGI_FORMAT_R8G8B8A8_UNORM:
        plane?DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    row.layout.Footprint={format,3,2,1,256};return row;
}
static void Pixel(std::vector<uint8_t>& data,const NativeExitPlane& plane,UINT x,UINT y,uint32_t value){
    std::memcpy(data.data()+plane.layout.Offset+size_t(y)*plane.layout.Footprint.RowPitch+size_t(x)*plane.stride+plane.component,&value,plane.bytes);
}
static void CheckShape(){
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=4096;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Require(detail::ReadbackShape(desc),"readback buffer shape");
    auto bad=desc;bad.Width=0;Require(!detail::ReadbackShape(bad),"empty readback");
    bad=desc;bad.Width=detail::MaxReadbackBytes+1;Require(!detail::ReadbackShape(bad),"oversized readback");
    bad=desc;bad.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;Require(!detail::ReadbackShape(bad),"texture rejected as readback");
    bad=desc;bad.MipLevels=2;Require(!detail::ReadbackShape(bad),"multiple readback subresources");
    bad=desc;bad.DepthOrArraySize=2;Require(!detail::ReadbackShape(bad),"readback array");
    bad=desc;bad.Format=DXGI_FORMAT_R8_UINT;Require(!detail::ReadbackShape(bad),"typed readback");
    bad=desc;bad.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;Require(!detail::ReadbackShape(bad),"readback flags");
    const auto row=Plane(0,0);uint64_t end{};
    Require(detail::PlaneBounds(row,3,2,780,&end) && end==780,"last row can end at resource boundary");
    Require(!detail::PlaneBounds(row,3,2,779),"truncated last row");
    Require(detail::ValidatePlane(row,3,2,4096,row.layout.Footprint,2,12),"device footprint matched");
    auto changed=row;changed.layout.Offset=UINT64_MAX-511;
    Require(!detail::PlaneBounds(changed,3,2,4096),"overflowing offset");
    changed=row;changed.layout.Offset=513;Require(!detail::PlaneBounds(changed,3,2,4096),"misaligned offset");
    changed=row;changed.layout.Footprint.RowPitch=255;Require(!detail::PlaneBounds(changed,3,2,4096),"misaligned pitch");
    changed=row;changed.layout.Footprint.Width=4;Require(!detail::PlaneBounds(changed,3,2,4096),"footprint width mismatch");
    changed=row;changed.rows=1;Require(!detail::PlaneBounds(changed,3,2,4096),"row count mismatch");
    changed=row;changed.layout.Footprint.Depth=2;Require(!detail::PlaneBounds(changed,3,2,4096),"3D footprint rejected");
    changed=row;changed.slot=3;Require(!detail::PlaneBounds(changed,3,2,4096),"unselected target rejected");
    changed=row;changed.plane=1;Require(!detail::PlaneBounds(changed,3,2,4096),"color subresource plane rejected");
    changed=row;changed.layout.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    Require(!detail::ValidatePlane(changed,3,2,4096,row.layout.Footprint,2,12),"native format must match device footprint");
    Require(!detail::ValidatePlane(row,3,2,4096,row.layout.Footprint,2,24),"device row bytes must match stride");
    Require(!detail::ValidatePlane(row,3,2,4096,row.layout.Footprint,1,12),"device row count must match");
    Require(!detail::PlaneBounds(row,0,2,4096) && !detail::PlaneBounds(row,4097,2,4096),"dimensions bounded");
    std::array<NativeExitPlane,5> planes{Plane(0,0),Plane(1,0),Plane(2,0),Plane(4,0,8),Plane(4,1,8)};
    Require(detail::CompletePlanes(planes),"three colors plus depth and stencil");
    std::reverse(planes.begin(),planes.end());Require(detail::CompletePlanes(planes),"plane order is irrelevant");
    Require(!detail::CompletePlanes(std::span(planes).first(4)),"missing plane rejected");
    planes[0]=planes[1];Require(!detail::CompletePlanes(planes),"duplicate plane rejected");
}
static void CheckComparison(){
    const auto native=Plane(0,0,4,512),reference=Plane(0,0,4,1024),optimized=Plane(0,0,4,1536);
    std::vector<uint8_t> source(4096,0x11),a(4096,0x22),b(4096,0x33);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<3;++x){
        const auto value=0xA1020300+x+3*y;Pixel(source,native,x,y,value);Pixel(a,reference,x,y,value);Pixel(b,optimized,x,y,value);
    }
    detail::Comparison result;
    Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result),"valid color comparison");
    Require(!result.reference.count && !result.optimized.count && result.reference.x==UINT_MAX,"padding ignored and equality exact");
    Pixel(a,reference,2,1,0x01020304);Pixel(b,optimized,0,0,0x05060708);Pixel(b,optimized,1,0,0x090A0B0C);
    Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result),"mismatched color comparison");
    Require(result.reference.count==1 && result.reference.x==2 && result.reference.y==1 && result.reference.value==0x01020304 &&
        result.reference.nativeValue==0xA1020305,"reference coordinates and complete value");
    Require(result.optimized.count==2 && result.optimized.x==0 && result.optimized.y==0 && result.optimized.value==0x05060708,
        "optimized counts and first sample");
    auto wrong=optimized;wrong.slot=1;
    Require(!detail::ComparePlane(3,2,reference,a,wrong,b,native,source,result),"cross-target comparison rejected");
    Require(!detail::ComparePlane(3,2,reference,a,optimized,b,native,std::span(source).first(779),result),"native span bounds checked");
    Require(!result.reference.count && !result.optimized.count,"invalid comparison has no partial result");
}
static void CheckDepthStencil(){
    for(UINT stride:{1u,4u,8u}){
        const auto native=Plane(4,1,stride,512),reference=Plane(4,1,stride,1024),optimized=Plane(4,1,stride,1536);
        std::vector<uint8_t> source(4096,0x11),a(4096,0x22),b(4096,0x33);
        for(UINT y=0;y<2;++y)for(UINT x=0;x<3;++x){Pixel(source,native,x,y,0xAB);Pixel(a,reference,x,y,0xAB);Pixel(b,optimized,x,y,0xAB);}
        detail::Comparison result;
        Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result) && !result.reference.count && !result.optimized.count,
            "stencil comparison ignores depth and unused bytes");
        Pixel(a,reference,1,1,0xCD);
        Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result) && result.reference.count==1 &&
            result.reference.value==0xCD && result.reference.nativeValue==0xAB,"stencil byte mismatch isolated");
        auto wrong=native;wrong.component=stride==8?0:1;
        Require(!detail::PlaneBounds(wrong,3,2,4096),"stencil component enforced");
    }
    const auto native=Plane(4,0,8,512),reference=Plane(4,0,8,1024),optimized=Plane(4,0,8,1536);
    std::vector<uint8_t> source(4096,0x11),a(4096,0x22),b(4096,0x33);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<3;++x){Pixel(source,native,x,y,0x3F800000);Pixel(a,reference,x,y,0x3F800000);Pixel(b,optimized,x,y,0x3F800000);}
    detail::Comparison result;
    Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result) && !result.reference.count && !result.optimized.count,
        "depth comparison ignores stencil and unused bytes");
    Pixel(b,optimized,2,0,0x3F800001);
    Require(detail::ComparePlane(3,2,reference,a,optimized,b,native,source,result) && result.optimized.count==1,"depth comparison is byte-exact");
}
static void CheckAggregation(){
    std::array<uint64_t,24> stats{};
    for(UINT eye=0;eye<2;++eye)for(const auto& plane:std::array{Plane(0,0),Plane(1,0),Plane(2,0),Plane(4,0,8),Plane(4,1,8)}){
        detail::Comparison result;result.reference.count=eye+1;result.optimized.count=eye+3;
        detail::Accumulate(stats,eye,plane,3,result);
    }
    Require(stats[2]==10 && stats[3]==204,"plane and selected payload byte totals");
    Require(stats[4]==3 && stats[5]==6 && stats[6]==9 && stats[7]==12,"color differences aggregate three targets");
    Require(stats[8]==1 && stats[9]==2 && stats[10]==3 && stats[11]==4,"depth differences preserve eyes and versions");
    Require(stats[12]==1 && stats[13]==2 && stats[14]==3 && stats[15]==4,"stencil differences preserve eyes and versions");
    Require(stats[16]==5 && stats[17]==10 && stats[18]==15 && stats[19]==20,"total mismatch counts include all planes");
    Require(!stats[0] && !stats[1] && !stats[20] && !stats[21] && !stats[22] && !stats[23],"aggregation does not fabricate completion");
}
static void CheckVrsAttribution(){
    const std::array<uint8_t,7> rates{0,1,4,5,6,9,10},finest{};
    Require(detail::AlwaysFiner(finest,rates) && detail::AlwaysFiner(rates,rates),"legal equal and finer rate components");
    Require(!detail::AlwaysFiner(rates,finest),"coarser common rates rejected");
    Require(!detail::AlwaysFiner(std::array<uint8_t,1>{1},std::array<uint8_t,1>{4}),"both rate dimensions must be finer");
    Require(!detail::AlwaysFiner(std::array<uint8_t,1>{2},std::array<uint8_t,1>{10}),"reserved rate rejected");
    Require(!detail::AlwaysFiner(std::span<const uint8_t>{},std::span<const uint8_t>{}),"empty rate map rejected");
    Require(!detail::AlwaysFiner(finest,std::span(rates).first(6)),"rate map sizes must match");
    Require(detail::RateMapShape(3,2,2,2,1,2),"partial final tile is covered");
    Require(!detail::RateMapShape(3,2,0,2,1,2) && !detail::RateMapShape(3,2,1,2,1,2),"tile metadata and image coverage checked");
    Require(!detail::RateMapShape(3,2,2,2,1,3),"rate data size checked");
    const auto native=Plane(0,0,4,512),reference=Plane(0,0,4,1024);
    std::vector<uint8_t> source(4096,0x11),a(4096,0x22);
    for(UINT y=0;y<2;++y)for(UINT x=0;x<3;++x){Pixel(source,native,x,y,0xABCD);Pixel(a,reference,x,y,0xABCD);}
    Pixel(a,reference,0,0,0x1234);Pixel(a,reference,2,0,0x1234);Pixel(a,reference,2,1,0x1234);
    const std::array<uint8_t,2> common{0,0},original{0,5};uint64_t inside{},outside{};
    Require(detail::AttributeColor(3,2,reference,a,native,source,2,2,1,common,original,inside,outside) && inside==2 && outside==1,
        "all differing color pixels attributed by actual tile coordinates");
    const std::array<uint8_t,6> perPixel{0,0,5,0,0,0},allFine{};
    Require(detail::AttributeColor(3,2,reference,a,native,source,1,3,2,allFine,perPixel,inside,outside) && inside==1 && outside==2,
        "row-major map addressing and runtime tile size");
    Require(detail::AttributeColor(3,2,reference,a,native,source,2,2,1,common,common,inside,outside) && !inside && outside==3,
        "mismatches outside changed cells remain visible");
    Require(!detail::AttributeColor(3,2,reference,a,native,source,2,2,1,original,common,inside,outside) && !inside && !outside,
        "invalid finer-rate claim cannot produce attribution");
    auto depth=reference;depth.slot=4;
    Require(!detail::AttributeColor(3,2,depth,a,native,source,2,2,1,common,original,inside,outside),"attribution only accepts color planes");
}
static void CheckAcceptance(){
    std::array<uint64_t,24> native{};native[0]=1;native[1]=2;native[2]=10;native[20]=1;native[23]=1;
    for(UINT index:{4u,6u,16u,18u})native[index]=100;
    for(UINT index:{5u,7u,17u,19u})native[index]=200;
    std::array<uint64_t,16> rate{};rate[0]=1;rate[1]=2;rate[2]=167;rate[3]=10;rate[13]=rate[14]=1;
    std::array<uint64_t,12> attribution{};attribution[0]=16;attribution[1]=attribution[2]=93;
    for(UINT index:{3u,4u,9u,10u,11u})attribution[index]=1;
    attribution[5]=100;attribution[6]=200;
    const std::array<uint64_t,2> same{};
    auto result=detail::Acceptance(true,native,rate,attribution,same);
    Require(result==std::array<uint64_t,8>{1,1,1,0,0,0,0,1},"explicitly approved and fully explained VRS refinement accepted");
    Require(!native[21] && native[16]==100 && native[17]==200,"acceptance preserves raw native differences and equivalence");
    result=detail::Acceptance(false,native,rate,attribution,same);
    Require(!result[0] && !result[7],"strict policy rejects explained native color differences");
    for(UINT index:{0u,1u,2u,20u}){auto changed=native;changed[index]=0;
        Require(!detail::Acceptance(true,changed,rate,attribution,same)[7],"all ten native planes and both eyes must be compared");}
    for(UINT index:{0u,1u,3u,13u,14u}){auto changed=rate;changed[index]=0;
        Require(!detail::Acceptance(true,native,changed,attribution,same)[7],"complete original-rate native equivalence is required");}
    for(UINT index:{11u,12u}){auto changed=rate;changed[index]=1;
        Require(!detail::Acceptance(true,native,changed,attribution,same)[7],"original-rate differences cannot be approved");}
    for(UINT index:{3u,4u,9u,10u,11u}){auto changed=attribution;changed[index]=0;
        Require(!detail::Acceptance(true,native,rate,changed,same)[7],"uniform maps, completed attribution and finer rates are each required");}
    for(UINT eye=0;eye<2;++eye){auto changed=attribution;changed[7+eye]=1;
        result=detail::Acceptance(true,native,rate,changed,same);
        Require(!result[7] && result[3+eye]==1,"outside-tile color mismatch is unexplained");}
    for(UINT index=8;index<16;++index){auto changed=native;changed[index]=1;
        result=detail::Acceptance(true,changed,rate,attribution,same);
        Require(!result[7] && result[3+index%2]==1,"both variants' native depth and stencil must match");}
    for(UINT eye=0;eye<2;++eye){auto different=same;different[eye]=1;
        result=detail::Acceptance(true,native,rate,attribution,different);
        Require(!result[7] && result[5+eye]==1,"reference and optimized outputs must match");}
    auto rejected=native;rejected[22]=1;
    Require(!detail::Acceptance(true,rejected,rate,attribution,same)[7],"GPU input rejection is never accepted");
    auto rejectedRate=rate;rejectedRate[15]=1;
    Require(!detail::Acceptance(true,native,rejectedRate,attribution,same)[7],"calibration input rejection is never accepted");
    const std::array<uint64_t,12> noVrs{};
    Require(!detail::Acceptance(true,native,rate,noVrs,same)[7],"approval does not bypass missing VRS proof");
    auto strict=native;std::fill(strict.begin()+4,strict.begin()+20,0);strict[21]=1;
    const std::array<uint64_t,16> noRate{};
    result=detail::Acceptance(false,strict,noRate,noVrs,same);
    Require(result==std::array<uint64_t,8>{0,0,0,0,0,0,0,1},"strict bitwise native equality works without VRS");
    Require(detail::Acceptance(true,strict,noRate,noVrs,same)[7],"strict equality remains sufficient under approved policy");
    strict[22]=1;Require(!detail::Acceptance(false,strict,noRate,noVrs,same)[7],"strict equality cannot accept a rejected input");
    auto many=native;many[8]=UINT64_MAX;many[10]=1;
    result=detail::Acceptance(true,many,rate,attribution,same);
    Require(!result[7] && result[3]==UINT64_MAX,"unexplained mismatch aggregation cannot wrap to zero");
}
int main()try{
    CheckShape();CheckComparison();CheckDepthStencil();CheckAggregation();CheckVrsAttribution();CheckAcceptance();
    std::puts("native exit footprint, coverage, component, comparison, VRS attribution and acceptance tests passed");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
