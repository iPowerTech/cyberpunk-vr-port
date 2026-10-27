#include "Render/NativeIndirectPolicy.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

using namespace cvr::stereo::indirect_policy;
void Require(bool good){if(!good)throw std::runtime_error("native indirect policy assertion failed");}
void PureDraws(){
    D3D12_INDIRECT_ARGUMENT_DESC argument{};argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC desc{16,1,&argument,0};
    for(const UINT mask:{0u,1u})for(UINT stride=0;stride<=1040;++stride){
        desc.ByteStride=stride;desc.NodeMask=mask;
        const bool valid=stride>=16 && stride<=1024 && stride%4==0;
        const detail::Receipt receipt{1,2,stride,mask};
        Require(detail::Classify(desc)==(valid?2u:0u));
        Require(detail::PureDrawStride(receipt)==(valid?stride:0));
        Require(detail::DrawKind(receipt)==(valid?2u:0u));
        Require(!detail::DrawIndexedStride(receipt));
    }
    for(const UINT mask:{2u,3u,4u,0x80000000u,UINT_MAX}){
        desc={16,1,&argument,mask};Require(!detail::Classify(desc));
        Require(!detail::PureDrawStride({1,2,16,mask}) && !detail::DrawKind({1,2,16,mask}));
    }
    desc={16,1,&argument,0};
    std::memset(&argument,0xcd,sizeof(argument));argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    Require(detail::Classify(desc)==2);
    desc.pArgumentDescs=nullptr;Require(!detail::Classify(desc));desc.pArgumentDescs=&argument;
    for(const UINT count:{0u,2u,3u,UINT_MAX}){desc.NumArgumentDescs=count;Require(!detail::Classify(desc));}
    for(const UINT kind:{0u,1u,2u,3u,UINT_MAX})for(const UINT version:{0u,2u,UINT_MAX}){
        const detail::Receipt receipt{version,kind,20,0};
        Require(!detail::DrawKind(receipt) && !detail::PureDrawStride(receipt) && !detail::DrawIndexedStride(receipt));
    }
    for(const UINT kind:{0u,3u,UINT_MAX}){
        Require(!detail::DrawKind({1,kind,20,0}) && !detail::PureDrawStride({1,kind,20,0}));
    }
    Require(!detail::PureDrawStride({1,1,16,0}) && !detail::DrawKind({1,1,16,0}));
    Require(!detail::PureDrawStride({1,2,UINT_MAX,0}) && !detail::PureDrawStride({}));
}
int main()try{
    PureDraws();
    D3D12_INDIRECT_ARGUMENT_DESC argument{};argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC desc{20,1,&argument,0};
    for(const UINT mask:{0u,1u})for(const UINT stride:{20u,24u,32u,256u,1020u,1024u}){
        desc.NodeMask=mask;desc.ByteStride=stride;Require(detail::Classify(desc)==1);
        Require(detail::DrawIndexedStride({1,1,stride,mask})==stride);
        Require(detail::PureDrawStride({1,1,stride,mask})==stride && detail::DrawKind({1,1,stride,mask})==1);
    }
    desc.NodeMask=0;
    for(UINT stride=0;stride<=1040;++stride){
        desc.ByteStride=stride;const bool valid=stride>=20 && stride<=1024 && stride%4==0;
        Require(bool(detail::Classify(desc))==valid);
        Require(detail::DrawIndexedStride({1,1,stride,0})==(valid?stride:0));
    }
    desc.ByteStride=20;
    for(const UINT mask:{2u,3u,4u,0x80000000u,UINT_MAX}){
        desc.NodeMask=mask;Require(!detail::Classify(desc));Require(!detail::DrawIndexedStride({1,1,20,mask}));
    }
    desc.NodeMask=0;
    for(UINT type=0;type<32;++type){
        argument.Type=static_cast<D3D12_INDIRECT_ARGUMENT_TYPE>(type);
        const UINT kind=type==UINT(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED)?1u:type==UINT(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW)?2u:0u;
        Require(detail::Classify(desc)==kind);
    }
    argument.Type=static_cast<D3D12_INDIRECT_ARGUMENT_TYPE>(UINT_MAX);Require(!detail::Classify(desc));
    // DRAW_INDEXED has no union payload. Ignored bytes must not alter meaning.
    std::memset(&argument,0xcd,sizeof(argument));argument.Type=D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    Require(detail::Classify(desc)==1);
    desc.pArgumentDescs=nullptr;Require(!detail::Classify(desc));desc.pArgumentDescs=&argument;
    for(const UINT count:{0u,2u,3u,UINT_MAX}){desc.NumArgumentDescs=count;Require(!detail::Classify(desc));}
    std::array<D3D12_INDIRECT_ARGUMENT_DESC,2> arguments{argument,{}};
    for(const auto type:{D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT,D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW,
        D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW,D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW,
        D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW,D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW}){
        arguments[1].Type=type;desc={20,2,arguments.data(),0};Require(!detail::Classify(desc));
        std::swap(arguments[0],arguments[1]);Require(!detail::Classify(desc));std::swap(arguments[0],arguments[1]);
    }
    desc={UINT_MAX,1,&argument,0};Require(!detail::Classify(desc));
    Require(!detail::DrawIndexedStride({}));
    for(const UINT version:{0u,2u,UINT_MAX})Require(!detail::DrawIndexedStride({version,1,20,0}));
    for(const UINT kind:{0u,2u,UINT_MAX})Require(!detail::DrawIndexedStride({1,kind,20,0}));
    std::puts("PASS native pure-draw indirect signature policy and private receipt validation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
