#include "Render/SourceSelectorPlan.hpp"
#include <json/json.h>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace selectors=cvr::stereo::source_selectors;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::string Hash(char value){return std::string(64,value);}
static Json::Value Cbv(unsigned visibility){
    Json::Value value;value["root"]=Hash('a');value["visibility"]=visibility;value["register"]=4;value["space"]=0;
    value["parameter"]=visibility==1?1:19;value["tableOffset"]=0;return value;
}
static Json::Value Requirement(unsigned visibility){
    auto value=Cbv(visibility);value["reflectedBytes"]=visibility==1?8:16;value["requiredByteExtent"]=visibility==1?8:16;
    value["dwordOffsets"]=Json::arrayValue;for(unsigned offset=0;offset<(visibility==1?8u:16u);offset+=4)value["dwordOffsets"].append(offset);return value;
}
static Json::Value Site(unsigned visibility,unsigned offset,unsigned identity,unsigned space=1){
    Json::Value value;value["shader"]=Hash(visibility==1?'b':'c');value["createHandleSSA"]="%"+std::to_string(identity);
    value["createHandleLine"]=identity;value["indexSSA"]="%"+std::to_string(identity+1000);value["cbv"]=Cbv(visibility);value["byteOffset"]=offset;
    Json::Value operation;operation["opcode"]="add";operation["operand"]=0;value["operations"]=Json::arrayValue;value["operations"].append(operation);
    auto& table=value["table"];table["root"]=Hash('a');table["visibility"]=visibility;
    table["parameter"]=visibility==1?13:space==1?31:33;table["space"]=space;table["shaderRegisterBase"]=0;
    table["originalTableOffset"]=0;table["declaredExtent"]=32768;return value;
}
static Json::Value Fixture(){
    Json::Value manifest;manifest["schema"]=1;manifest["purpose"]="original-source-selector-evaluation-only";manifest["evidenceSha256"]=Hash('f');
    Json::Value family;family["number"]=1;family["root"]=Hash('a');family["vs"]=Hash('b');family["ps"]=Hash('c');family["supported"]=true;
    family["unsupportedReason"]="";family["unresolvedInputs"]=Json::arrayValue;family["cbvs"]=Json::arrayValue;
    family["cbvs"].append(Requirement(1));family["cbvs"].append(Requirement(5));family["sites"]=Json::arrayValue;
    family["sites"].append(Site(1,0,100));family["sites"].append(Site(1,4,101));
    for(unsigned index=0;index<4;++index)family["sites"].append(Site(5,index*4,200+index));
    family["sites"].append(Site(5,12,204,3));manifest["families"]=Json::arrayValue;manifest["families"].append(family);
    family["number"]=13;family["vs"]=Hash('d');family["ps"]=Hash('e');family["supported"]=false;
    family["unsupportedReason"]="Actual t5 selector inputs are required";family["unresolvedInputs"].append("ps:t5 space0 field100");
    family["cbvs"]=Json::arrayValue;family["sites"]=Json::arrayValue;manifest["families"].append(family);return manifest;
}
static std::string Text(const Json::Value& value){Json::StreamWriterBuilder builder;builder["indentation"]="";return Json::writeString(builder,value);}
static selectors::Catalog Load(const Json::Value& value){
    selectors::Catalog catalog;std::string error;Require(selectors::LoadCatalogJson(Text(value),catalog,error),error.c_str());return catalog;
}
static void Reject(const Json::Value& value){
    auto catalog=Load(Fixture());std::string error;Require(!selectors::LoadCatalogJson(Text(value),catalog,error),"Invalid package admitted");
    Require(catalog.families.empty() && catalog.evidenceSha256.empty() && !error.empty(),"Invalid package published partial output");
}
struct Inputs {
    std::array<uint8_t,8> vs{0,0,0,0,255,127,0,0};
    std::array<uint8_t,16> ps{10,0,0,0,11,0,0,0,13,0,0,0,11,0,0,0};
    std::array<selectors::OwnedCbvBytes,2> Views(const selectors::FamilyPlan& family){return {{{family.cbvs[0].key,vs},{family.cbvs[1].key,ps}}};}
};
static void ExactEvaluation(){
    auto catalog=Load(Fixture());const auto key=selectors::FamilyKey{Hash('a'),Hash('b'),Hash('c')};
    const auto* family=catalog.Find(key);Require(family && family->number==1,"Exact original family missing");
    auto wrong=key;wrong.rootSha256=Hash('d');Require(!catalog.Find(wrong),"Wrong root was treated as an original family");
    wrong=key;wrong.vertexSha256=Hash('d');Require(!catalog.Find(wrong),"Wrong VS was treated as an original family");
    wrong=key;wrong.pixelSha256=Hash('d');Require(!catalog.Find(wrong),"Wrong PS was treated as an original family");
    Inputs inputs;const auto views=inputs.Views(*family);selectors::Result result;std::string error;
    Require(selectors::Evaluate(*family,views,result,error),error.c_str());
    Require(result.selectorEvaluationComplete && result.evaluatedSites==7 && result.selections.size()==7 && result.tables.size()==3,"Selector coverage incomplete");
    Require(result.selections[1].sourceDword==32767 && result.selections[1].shaderIndex==32767,"DWORD was not decoded little endian");
    const auto& vs=result.tables[0];Require(vs.key.visibility==1 && vs.key.parameter==13 && vs.offsets==std::vector<uint32_t>{0,32767} && vs.count==32768,"VS original offsets changed");
    const auto& ps=result.tables[1];Require(ps.key.visibility==5 && ps.key.parameter==31 && ps.offsets==std::vector<uint32_t>{10,11,13} && ps.count==14 &&
        ps.uses==std::vector<selectors::SlotInterval>{{10,2,0},{13,1,0}},"PS sparse uses were not unique/merged");
    Require(result.tables[2].key.parameter==33 && result.tables[2].key.space==3 && result.tables[2].offsets==std::vector<uint32_t>{11},"Space3 table was merged with space1");
    Require(!result.descriptorOwnershipProved && !result.inputContentsFrozen && !result.nativeExecutionPermission &&
        !result.nativeExecutionPermitted && !result.performanceGainProved && !result.nativeCommandsSuppressed,"Selector arithmetic certified execution/contents");
}
static void StrictJson(){
    selectors::Catalog catalog;std::string error;const auto valid=Text(Fixture());
    for(const auto& text:std::vector<std::string>{"",valid+"{}","//comment\n"+valid,valid.substr(0,valid.size()-1),std::string(selectors::MaxCatalogBytes+1,' ')})
        Require(!selectors::LoadCatalogJson(text,catalog,error),"Malformed/oversized JSON admitted");
    auto duplicate=valid;duplicate.insert(1,"\"schema\":1,");Require(!selectors::LoadCatalogJson(duplicate,catalog,error),"Duplicate JSON member admitted");
    auto value=Fixture();value["nativeExecutionPermission"]=true;Reject(value);
    value=Fixture();value["schema"]=true;Reject(value);value=Fixture();value["schema"]=1.0;Reject(value);
    value=Fixture();value["purpose"]="execute";Reject(value);value=Fixture();value["families"]=Json::arrayValue;Reject(value);
}
static void HashesAndAmbiguity(){
    for(const auto* field:{"root","vs","ps"}){auto value=Fixture();value["families"][0][field]=Hash('A');Reject(value);}
    auto value=Fixture();value["evidenceSha256"]=Hash('g');Reject(value);
    value=Fixture();auto duplicate=value["families"][0];duplicate["number"]=2;value["families"].append(duplicate);Reject(value);
    value=Fixture();value["families"][1]["number"]=1;Reject(value);
    value=Fixture();value["families"][0]["sites"].append(value["families"][0]["sites"][0]);Reject(value);
    value=Fixture();value["families"][0]["cbvs"].append(value["families"][0]["cbvs"][0]);Reject(value);
}
static void QualifiedBindings(){
    for(const auto* field:{"visibility","register","space","parameter","tableOffset"}){auto value=Fixture();auto& cbv=value["families"][0]["sites"][0]["cbv"];cbv[field]=cbv[field].asUInt()+1;Reject(value);}
    for(const auto* field:{"visibility","parameter","space","shaderRegisterBase","originalTableOffset","declaredExtent"}){auto value=Fixture();auto& table=value["families"][0]["sites"][0]["table"];table[field]=table[field].asUInt()+1;Reject(value);}
    auto value=Fixture();value["families"][0]["sites"][0]["shader"]=Hash('c');Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["cbv"]["root"]=Hash('d');Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["table"]["root"]=Hash('d');Reject(value);
}
static void OffsetsOperationsAndBounds(){
    for(unsigned offset:{1u,8u,65532u}){auto value=Fixture();value["families"][0]["sites"][0]["byteOffset"]=offset;Reject(value);}
    auto value=Fixture();value["families"][0]["cbvs"][0]["requiredByteExtent"]=7;Reject(value);
    value=Fixture();value["families"][0]["cbvs"][0]["reflectedBytes"]=7;Reject(value);
    value=Fixture();value["families"][0]["cbvs"][0]["dwordOffsets"][1]=0;Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["operations"][0]["operand"]=1;Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["operations"][0]["opcode"]="and";Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["createHandleSSA"]="%named";Reject(value);
    value=Fixture();value["families"][0]["sites"][0]["createHandleLine"]=0;Reject(value);
    value=Fixture();for(unsigned index=0;index<selectors::MaxFamilies;++index)value["families"].append(value["families"][1]);Reject(value);
}
static void ExactByteViews(){
    auto catalog=Load(Fixture());const auto& family=catalog.families[0];Inputs inputs;auto views=inputs.Views(family);selectors::Result result;std::string error;
    for(unsigned index=0;index<2;++index){auto wrong=views;wrong[index].bytes=wrong[index].bytes.first(wrong[index].bytes.size()-1);
        Require(!selectors::Evaluate(family,wrong,result,error),"Short b4 view admitted");}
    auto wrong=views;wrong[0].key.rootSha256=Hash('f');Require(!selectors::Evaluate(family,wrong,result,error),"Foreign b4 root admitted");
    wrong=views;wrong[0]=wrong[1];Require(!selectors::Evaluate(family,wrong,result,error),"Duplicate stage view admitted");
    Require(!selectors::Evaluate(family,std::span<const selectors::OwnedCbvBytes>(views).first(1),result,error),"Missing original b4 view admitted");
    std::array<uint8_t,256> padded{};wrong=views;wrong[1].bytes=padded;Require(!selectors::Evaluate(family,wrong,result,error),"Descriptor padding replaced exact b4 logical bytes");
}
static void FailureAtomicityAndUnsupported(){
    auto catalog=Load(Fixture());auto family=catalog.families[0];Inputs inputs;auto views=inputs.Views(family);selectors::Result result;std::string error;
    Require(selectors::Evaluate(family,views,result,error),"Valid precondition failed");inputs.ps[1]=128;
    Require(!selectors::Evaluate(family,views,result,error) && result.tables.empty() && result.selections.empty() &&
        !result.selectorEvaluationComplete && !error.empty(),"Out-of-range site published partial table results");
    Require(!selectors::Evaluate(catalog.families[1],{},result,error),"Unsupported multilayer family was evaluated");
    family.sites[0].byteOffset=65532;Require(!selectors::Evaluate(family,views,result,error),"Mutated plan bypassed bounds validation");
    auto value=Fixture();value["families"][1]["sites"].append(value["families"][0]["sites"][0]);Reject(value);
}
static void FileFailure(){selectors::Catalog catalog;std::string error;
    Require(!selectors::LoadCatalogFile("__missing_source_selector_catalog__.json",catalog,error) && catalog.families.empty() && !error.empty(),"Missing catalog was accepted");}
int main(){try{ExactEvaluation();StrictJson();HashesAndAmbiguity();QualifiedBindings();OffsetsOperationsAndBounds();ExactByteViews();FailureAtomicityAndUnsupported();FileFailure();
    std::puts("source_selector_plan_test: OK");return 0;}catch(const std::exception& error){std::fprintf(stderr,"source_selector_plan_test: %s\n",error.what());return 1;}}
