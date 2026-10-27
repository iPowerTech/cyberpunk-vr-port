#include "Render/MultilayerSelectorPlan.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace multilayer=cvr::stereo::multilayer_selectors;
namespace selectors=cvr::stereo::source_selectors;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static selectors::FamilyKey Family(){return {std::string(multilayer::RootSha256),std::string(multilayer::VertexSha256),std::string(multilayer::PixelSha256)};}
static selectors::CbvKey B4Key(){return {std::string(multilayer::RootSha256),5,4,0,19,0};}
static multilayer::StructuredView View(){multilayer::StructuredView view;
    view.key={std::string(multilayer::RootSha256),5,5,0,23,5};view.numElements=8192;view.resourceWidth=uint64_t(view.numElements)*128;
    view.structureByteStride=128;view.dimension=1;view.componentMapping=5768;view.explicitDescription=true;return view;}
static void Store(std::span<uint8_t> bytes,size_t offset,uint32_t value){for(unsigned n=0;n<4;++n)bytes[offset+n]=uint8_t(value>>(n*8));}
static std::array<uint8_t,228> B4(float base=3412.0f,uint32_t mask=65535){std::array<uint8_t,228> bytes{};
    Store(bytes,128,std::bit_cast<uint32_t>(base));Store(bytes,224,mask);return bytes;}
static multilayer::ReadPlan Plan(float base=3412.0f,uint32_t mask=65535,multilayer::StructuredView view=View()){
    const auto b4=B4(base,mask);multilayer::ReadPlan plan;std::string error;Require(multilayer::PlanRead(Family(),{B4Key(),b4},view,plan,error),error.c_str());return plan;}
static void RejectPlan(float base,uint32_t mask,multilayer::StructuredView view=View()){
    const auto b4=B4(base,mask);auto plan=Plan();std::string error;Require(!multilayer::PlanRead(Family(),{B4Key(),b4},view,plan,error),"Invalid multilayer plan admitted");
    Require(plan.records.empty() && !plan.byteExtent && !plan.originalSiteCoverageComplete && !error.empty(),"Failed plan published partial metadata");}
static std::vector<uint8_t> Records(const multilayer::ReadPlan& plan){
    std::vector<uint8_t> bytes(plan.byteExtent,255);
    for(const auto record:plan.records){const size_t offset=size_t(record-plan.firstRecord)*128;
        Store(bytes,offset+100,10);Store(bytes,offset+104,(14u<<16)|13u);Store(bytes,offset+108,(11u<<16)|12u);}
    return bytes;
}
static selectors::Result Evaluate(const multilayer::ReadPlan& plan,std::span<const uint8_t> bytes){selectors::Result result;std::string error;
    Require(multilayer::Enumerate(plan,{plan.view,plan.resourceOffset,bytes},result,error),error.c_str());return result;}
static void RejectSnapshot(const multilayer::ReadPlan& plan,const multilayer::RecordSnapshot& snapshot){selectors::Result result;result.selectorEvaluationComplete=true;std::string error;
    Require(!multilayer::Enumerate(plan,snapshot,result,error),"Invalid multilayer record snapshot admitted");
    Require(result.tables.empty() && result.selections.empty() && !result.selectorEvaluationComplete && !error.empty(),"Failed enumeration published partial selector results");}
static void ActualBoundAndCoverage(){
    const auto plan=Plan();Require(plan.baseRecord==3412 && plan.mask==65535 && plan.records.size()==16 && plan.firstRecord==3412 &&
        plan.records.back()==3427 && plan.recordSpan==16 && plan.resourceOffset==436736 && plan.byteExtent==2048,"Actual source b4 record bound changed");
    Require(plan.conservativeRecordSuperset && plan.originalSiteCoverageComplete && !plan.captureEpochVerified && !plan.copyPermission &&
        !plan.inputContentsFrozen && !plan.nativeExecutionPermission && !plan.nativeExecutionPermitted && !plan.performanceGainProved && !plan.nativeCommandsSuppressed,
        "Read bound certified capture, contents or execution");
    const auto sites=multilayer::AuditedSites();Require(sites.size()==10,"Original PS site coverage incomplete");
    const std::array<std::string_view,10> handles{"%520","%624","%642","%660","%718","%991","%1093","%1111","%1129","%1187"};
    const std::array<uint32_t,10> fields{100,108,108,104,104,100,108,108,104,104};
    for(size_t n=0;n<sites.size();++n)Require(sites[n].createHandleSSA==handles[n] && sites[n].fieldOffset==fields[n] &&
        sites[n].recordIndexSSA==(n<5?"%306":"%777"),"Original site/record path identity changed");
}
static void ExactOriginalKeysAndViews(){
    const auto b4=B4();auto family=Family();multilayer::ReadPlan output;std::string error;
    family.vertexSha256[0]='0';Require(!multilayer::PlanRead(family,{B4Key(),b4},View(),output,error),"Foreign original VS admitted");
    family=Family();family.pixelSha256[0]='0';Require(!multilayer::PlanRead(family,{B4Key(),b4},View(),output,error),"Foreign original PS admitted");
    family=Family();family.rootSha256[0]='1';Require(!multilayer::PlanRead(family,{B4Key(),b4},View(),output,error),"Foreign original root admitted");
    for(unsigned field=0;field<6;++field){auto key=B4Key();switch(field){case 0:key.rootSha256[0]='1';break;case 1:key.visibility=1;break;
        case 2:key.shaderRegister=5;break;case 3:key.space=1;break;case 4:key.parameter=20;break;case 5:key.tableOffset=1;break;}
        Require(!multilayer::PlanRead(Family(),{key,b4},View(),output,error),"Foreign PS b4 root/stage/register/space/slot admitted");}
    for(size_t count:{size_t(0),size_t(227),size_t(229),size_t(256)}){std::vector<uint8_t> bytes(count);
        Require(!multilayer::PlanRead(Family(),{B4Key(),bytes},View(),output,error),"Non-exact original b4 logical view admitted");}
    for(unsigned field=0;field<12;++field){auto view=View();switch(field){case 0:view.key.visibility=1;break;case 1:view.key.shaderRegister=4;break;
        case 2:view.key.space=1;break;case 3:view.key.parameter=24;break;case 4:view.key.tableOffset=6;break;case 5:view.key.rootSha256[0]='1';break;
        case 6:view.format=41;break;case 7:view.dimension=4;break;case 8:view.componentMapping=0;break;case 9:view.flags=1;break;
        case 10:view.structureByteStride=64;break;case 11:view.explicitDescription=false;break;}
        RejectPlan(3412,65535,view);}
}
static void FloatConversionAndMaskZero(){
    Require(Plan(3412.75f).baseRecord==3412,"FPToui positive fraction did not truncate toward zero");
    Require(Plan(-0.0f,1).baseRecord==0,"Negative zero was not a valid zero base");
    for(float value:{-1.0f,-0.5f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),4294967296.0f})RejectPlan(value,1);
    const auto empty=Plan(9000.0f,0);Require(empty.records.empty() && !empty.recordSpan && !empty.resourceOffset && !empty.byteExtent,"Zero mask scheduled t5 reads");
    const auto result=Evaluate(empty,{});Require(result.selectorEvaluationComplete && result.tables.empty() && result.selections.empty() && !result.evaluatedSites,
        "Zero wave mask fabricated dynamic selector uses");
    auto view=View();view.numElements=UINT32_MAX;view.resourceWidth=uint64_t(view.numElements)*128;
    const auto largest=Plan(std::nextafter(4294967296.0f,0.0f),UINT32_MAX,view);
    Require(largest.records.size()==32 && largest.byteExtent==4096 && largest.records.back()<UINT32_MAX,"Largest representable valid f32 base narrowed or overflowed");
}
static void SparseMasksAndSubviewBounds(){
    auto view=View();view.firstElement=7;view.resourceWidth+=7*128;
    const auto sparse=Plan(100.0f,0x80000008u,view);Require(sparse.records==std::vector<uint32_t>{103,131} && sparse.firstRecord==103 &&
        sparse.recordSpan==29 && sparse.resourceOffset==110*128 && sparse.byteExtent==29*128,"Sparse bit mask was shifted to wrong COM subview");
    const auto result=Evaluate(sparse,Records(sparse));Require(result.evaluatedSites==20 && result.tables[0].offsets==std::vector<uint32_t>{10,11,12,13,14},
        "Inactive copied gap records were evaluated");
    const auto last=Plan(100.0f,0x80000000u);Require(last.firstRecord==131 && last.records.size()==1 && last.byteExtent==128,"Single high bit expanded to an unnecessary whole prefix");
    view=View();view.numElements=3427;RejectPlan(3412,65535,view);
    view=View();view.resourceWidth-=1;RejectPlan(3412,65535,view);
    view=View();view.numElements=0;RejectPlan(0,0,view);
    view=View();view.firstElement=UINT64_MAX/128+1;view.resourceWidth=UINT64_MAX;RejectPlan(0,1,view);
    view=View();view.firstElement=UINT64_MAX/128;view.resourceWidth=UINT64_MAX;RejectPlan(0,1,view);
}
static void AllTenExactTransformsAndSparseUses(){
    const auto plan=Plan(100.0f,1);const auto bytes=Records(plan);const auto result=Evaluate(plan,bytes);
    const std::array<uint32_t,10> indices{10,11,12,13,14,10,11,12,13,14};
    Require(result.selections.size()==10 && result.evaluatedSites==10 && result.tables.size()==1,"Actual original selector sites were omitted");
    for(size_t n=0;n<indices.size();++n)Require(result.selections[n].siteIndex==n && result.selections[n].shaderIndex==indices[n] &&
        result.selections[n].descriptorOffset==indices[n],"Raw/low16/high16 original selector expression changed");
    const auto& table=result.tables[0];Require(table.key.rootSha256==multilayer::RootSha256 && table.key.visibility==5 && table.key.parameter==31 &&
        table.key.space==1 && !table.key.shaderRegisterBase && !table.key.originalTableOffset && table.key.declaredExtent==32768 &&
        table.offsets==std::vector<uint32_t>{10,11,12,13,14} && table.count==15 && table.uses==std::vector<selectors::SlotInterval>{{10,5,0}},
        "Original PS bindless offset coordinates were changed");
    Require(!result.descriptorOwnershipProved && !result.inputContentsFrozen && !result.nativeExecutionPermission &&
        !result.nativeExecutionPermitted && !result.performanceGainProved && !result.nativeCommandsSuppressed,"Enumeration certified inputs or execution");
}
static void SnapshotExactExtentAndPlanIntegrity(){
    const auto plan=Plan();auto bytes=Records(plan);RejectSnapshot(plan,{plan.view,plan.resourceOffset,std::span<const uint8_t>(bytes).first(bytes.size()-1)});
    bytes.push_back(0);RejectSnapshot(plan,{plan.view,plan.resourceOffset,bytes});bytes.pop_back();
    RejectSnapshot(plan,{plan.view,plan.resourceOffset+1,bytes});auto foreign=plan.view;foreign.firstElement=1;
    RejectSnapshot(plan,{foreign,plan.resourceOffset,bytes});foreign=plan.view;foreign.key.parameter=5;RejectSnapshot(plan,{foreign,plan.resourceOffset,bytes});
    std::vector<uint8_t> whole(size_t(plan.view.resourceWidth));std::copy(bytes.begin(),bytes.end(),whole.begin()+plan.resourceOffset);
    RejectSnapshot(plan,{plan.view,0,whole});Require(Evaluate(plan,std::span<const uint8_t>(whole).subspan(size_t(plan.resourceOffset),plan.byteExtent)).evaluatedSites==160,
        "Exact subspan of a larger caller-owned byte snapshot was rejected");
    auto torn=plan;torn.records.pop_back();RejectSnapshot(torn,{plan.view,plan.resourceOffset,bytes});
    torn=plan;++torn.resourceOffset;RejectSnapshot(torn,{plan.view,plan.resourceOffset,bytes});
    torn=plan;++torn.baseRecord;RejectSnapshot(torn,{plan.view,plan.resourceOffset,bytes});
}
static void PackedAndRawOutOfRangeFailAtomic(){
    const auto plan=Plan(100.0f,1);
    for(const auto [field,value]:std::array<std::pair<uint32_t,uint32_t>,6>{{{100,32768},{100,UINT32_MAX},{104,0x00008000},
        {104,0x80000000},{108,0x0000ffff},{108,0xffff0000}}}){auto bytes=Records(plan);Store(bytes,field,value);RejectSnapshot(plan,{plan.view,plan.resourceOffset,bytes});}
    auto bytes=Records(plan);Store(bytes,100,32767);Store(bytes,104,0x7fff7fff);Store(bytes,108,0x7fff7fff);
    const auto result=Evaluate(plan,bytes);Require(result.tables[0].offsets==std::vector<uint32_t>{32767} && result.tables[0].count==32768,
        "Valid packed selectors were incorrectly checked as a raw DWORD");
}
static void MaxBoundAndDeterministicUnion(){
    const auto plan=Plan(100.0f,UINT32_MAX);auto bytes=Records(plan);const auto result=Evaluate(plan,bytes);
    Require(plan.records.size()==32 && plan.recordSpan==32 && plan.byteExtent==4096 && result.selections.size()==320,
        "Conservative planner exceeded or omitted maximum32 records");
    for(size_t row=0;row<32;++row){Store(bytes,row*128+100,uint32_t(100+row));Store(bytes,row*128+104,(300u<<16)|200u);Store(bytes,row*128+108,(301u<<16)|201u);}
    const auto unionResult=Evaluate(plan,bytes);Require(unionResult.tables[0].offsets.size()==36 && unionResult.tables[0].count==302 &&
        unionResult.tables[0].uses==std::vector<selectors::SlotInterval>{{100,32,0},{200,2,0},{300,2,0}},"Conservative record union was not unique/sorted/sparse");
}
int main(){try{ActualBoundAndCoverage();ExactOriginalKeysAndViews();FloatConversionAndMaskZero();SparseMasksAndSubviewBounds();
    AllTenExactTransformsAndSparseUses();SnapshotExactExtentAndPlanIntegrity();PackedAndRawOutOfRangeFailAtomic();MaxBoundAndDeterministicUnion();
    std::puts("multilayer_selector_plan_test: OK");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"multilayer_selector_plan_test: %s\n",error.what());return 1;}}
