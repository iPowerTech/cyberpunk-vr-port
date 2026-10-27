#include "Render/SourceTablePlan.hpp"
#include <cstdio>
#include <stdexcept>

using namespace cvr::stereo;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
    std::array<D3D12_ROOT_PARAMETER1,36> parameters{};
    std::array<D3D12_DESCRIPTOR_RANGE1,36> ranges{};
    for(UINT n=0;n<36;++n){ranges[n]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,n,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
        parameters[n].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[n].DescriptorTable={1,&ranges[n]};parameters[n].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
    ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,4,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    ranges[2]={D3D12_DESCRIPTOR_RANGE_TYPE_CBV,2,5,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    parameters[2].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    ranges[26]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,39,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    ranges[31]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,1,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    ranges[33]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,3,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    D3D12_ROOT_SIGNATURE_DESC1 root{36,parameters.data(),0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    std::vector<families::ResourceUse> uses{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,0,1,1,848},
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,0,5,1,256},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,0,13,1,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,1,0,32768,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV,D3D12_SHADER_VISIBILITY_PIXEL,3,0,32768,0}};
    source_tables::Plan plan;Require(source_tables::Build(root,uses,plan),"Original 36-table plan rejected");
    Require(plan.finite.size()==3 && plan.deferred.size()==2 && !plan.Complete(),"Bindless domain was treated as owned finite slots");
    Require(plan.finite[0].parameter==0 && plan.finite[0].count==4 && plan.finite[0].uses[0].offset==1,"Camera table compacted or truncated");
    Require(plan.finite[1].parameter==2 && plan.finite[1].count==2 && plan.finite[1].uses[0].offset==0,"VS b5 original table lost");
    Require(plan.finite[2].parameter==26 && plan.finite[2].count==39 && plan.finite[2].uses[0].offset==13,"Finite SRV table extent lost");
    Require(plan.deferred[1].parameter==33,"High original parameter lost");
    auto finite=uses;finite.resize(3);Require(source_tables::Build(root,finite,plan) && plan.Complete(),"Pure finite plan not complete");
    finite[2].count=27;Require(!source_tables::Build(root,finite,plan) && plan.finite.empty(),"Shader range overflow admitted");
    finite=uses;finite[0].visibility=D3D12_SHADER_VISIBILITY_PIXEL;Require(!source_tables::Build(root,finite,plan),"Wrong shader visibility admitted");
    finite=uses;finite[0].space=9;Require(!source_tables::Build(root,finite,plan),"Unmapped CBV admitted");
    root.Flags=D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    Require(!source_tables::Build(root,uses,plan),"Direct heap indexing admitted");root.Flags=D3D12_ROOT_SIGNATURE_FLAG_NONE;
    root.NumParameters=65;Require(!source_tables::Build(root,uses,plan),"Unsupported parameter domain admitted");root.NumParameters=36;
    D3D12_DESCRIPTOR_RANGE1 aliased[2]{{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,2,0,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,1,0,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,1}};
    parameters[0].DescriptorTable={2,aliased};
    Require(!source_tables::Build(root,uses,plan),"Ambiguous original resource mapping admitted");
    parameters[0].DescriptorTable={1,&ranges[0]};
    aliased[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,UINT_MAX,0,1,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,0};
    aliased[1]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,5,1,D3D12_DESCRIPTOR_RANGE_FLAG_NONE,20};
    parameters[31].DescriptorTable={2,aliased};Require(!source_tables::Build(root,uses,plan),"Nonfinal unbounded range admitted");
    std::puts("PASS pure finite source table domains and deferred bindless selections");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
