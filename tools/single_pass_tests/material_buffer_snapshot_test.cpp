#include "Render/NativeMaterialBufferSnapshot.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace snapshot=cvr::stereo::material_buffer_snapshot;
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct IdentityResource final:ID3D12Resource {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{return --refs;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,UINT*,void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID,const IUnknown*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Map(UINT,const D3D12_RANGE*,void**)override{return E_NOTIMPL;}
    void STDMETHODCALLTYPE Unmap(UINT,const D3D12_RANGE*)override{}
    D3D12_RESOURCE_DESC STDMETHODCALLTYPE GetDesc()override{return {};}
    D3D12_GPU_VIRTUAL_ADDRESS STDMETHODCALLTYPE GetGPUVirtualAddress()override{return 0;}
    HRESULT STDMETHODCALLTYPE WriteToSubresource(UINT,const D3D12_BOX*,const void*,UINT,UINT)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReadFromSubresource(void*,UINT,UINT,UINT,const D3D12_BOX*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetHeapProperties(D3D12_HEAP_PROPERTIES*,D3D12_HEAP_FLAGS*)override{return E_NOTIMPL;}
};
static void Views(const D3D12_SHADER_RESOURCE_VIEW_DESC& original){
    D3D12_SHADER_RESOURCE_VIEW_DESC padded;std::memset(&padded,0xA5,sizeof(padded));
    padded.Format=original.Format;padded.ViewDimension=original.ViewDimension;padded.Shader4ComponentMapping=original.Shader4ComponentMapping;
    padded.Buffer.FirstElement=original.Buffer.FirstElement;padded.Buffer.NumElements=original.Buffer.NumElements;
    padded.Buffer.StructureByteStride=original.Buffer.StructureByteStride;padded.Buffer.Flags=original.Buffer.Flags;
    Require(std::memcmp(&original,&padded,sizeof(padded)) && snapshot::detail::SameBufferView(original,padded),
        "SRV padding changed material view identity");
    for(UINT field=0;field<7;++field){auto changed=padded;
        if(field==0)changed.Format=DXGI_FORMAT_R32_UINT;if(field==1)changed.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        if(field==2)++changed.Shader4ComponentMapping;if(field==3)++changed.Buffer.FirstElement;
        if(field==4)++changed.Buffer.NumElements;if(field==5)++changed.Buffer.StructureByteStride;
        if(field==6)changed.Buffer.Flags=D3D12_BUFFER_SRV_FLAGS(UINT(changed.Buffer.Flags)^1);
        Require(!snapshot::detail::SameBufferView(original,changed),"Changed material SRV field borrowed the original view");
    }
}
static void Bindings(const D3D12_SHADER_RESOURCE_VIEW_DESC& view,UINT reg){
    using namespace cvr::stereo;
    IdentityResource resource,foreign;source_srv::SourceSrvBacking source;source.shaderRegister=reg;source.binding.resource=&resource;source.binding.view=view;
    auto& value=source.binding.stamp.value;value[0]=2;value[1]=1;value[2]=17;value[4]=view.Format;value[5]=view.ViewDimension;
    value[6]=view.Shader4ComponentMapping;value[7]=view.Buffer.FirstElement;value[8]=view.Buffer.NumElements;
    value[9]=view.Buffer.StructureByteStride;value[10]=view.Buffer.Flags;source.binding.description=value;
    source.binding.stamp.heap=18;source.binding.stamp.gpu=19;
    descriptors::SourceResourceBinding binding;binding.parameter=source_srv::RootParameter;binding.offset=reg;
    binding.type=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;binding.resource=&resource;binding.stamp=source.binding.stamp;
    Require(snapshot::detail::SourceBinding(source,binding),"Exact original material binding was rejected");
    for(UINT field=0;field<7;++field){auto changed=binding;
        if(field==0)++changed.parameter;if(field==1)changed.offset=reg==4?6:4;
        if(field==2)changed.type=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;if(field==3)changed.resource=&foreign;
        if(field==4)changed.counter=&foreign;if(field==5)++changed.stamp.heap;if(field==6)++changed.stamp.gpu;
        Require(!snapshot::detail::SourceBinding(source,changed),"Changed material source identity was accepted");
    }
    for(size_t field=0;field<value.size();++field){auto changed=binding;++changed.stamp.value[field];
        Require(!snapshot::detail::SourceBinding(source,changed),"Changed canonical material descriptor was accepted");
    }
    auto changedSource=source;++changedSource.binding.view.Buffer.FirstElement;
    Require(!snapshot::detail::SourceBinding(changedSource,binding),"Original view disagreed with its canonical descriptor");
    changedSource=source;++changedSource.binding.description[2];
    Require(!snapshot::detail::SourceBinding(changedSource,binding),"Original descriptor identity disagreed with its stamp");
    changedSource=source;changedSource.shaderRegister=5;
    Require(!snapshot::detail::SourceBinding(changedSource,binding),"Material binding acquired the t5 contract");
    binding.resource.Reset();Require(!snapshot::detail::SourceBinding(source,binding),"Absent source resource was accepted");
}
static void Recordings(){
    IdentityResource token,foreign;cvr::stereo::native_recording::Lease recording{&token,21,22,23,24};
    Require(snapshot::detail::SameRecording(recording,recording),"Exact native recording identity was rejected");
    for(UINT field=0;field<6;++field){auto changed=recording;
        if(field==0)changed.token=&foreign;if(field==1)++changed.generation;if(field==2)++changed.context;
        if(field==3)++changed.list;if(field==4)++changed.registry;if(field==5)changed.token.Reset();
        Require(!snapshot::detail::SameRecording(recording,changed),"Foreign or incomplete native recording was accepted");
    }
    Require(!snapshot::detail::SameRecording({},{}),"Empty recording identities created a recording proof");
}
int main()try{
    D3D12_SHADER_RESOURCE_VIEW_DESC raw{},result{};raw.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    raw.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;raw.Format=DXGI_FORMAT_R32_TYPELESS;
    raw.Buffer.FirstElement=2154496;raw.Buffer.NumElements=2378;raw.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    Require(snapshot::detail::PrivateView(raw,4,128,9512,result) && result.Buffer.FirstElement==32 && result.Buffer.NumElements==2378 &&
        result.Format==DXGI_FORMAT_R32_TYPELESS && result.Buffer.Flags==D3D12_BUFFER_SRV_FLAG_RAW,"raw view must preserve byte indexing and exact bounds");
    Require(!snapshot::detail::PrivateView(raw,4,128,9516,result) && !snapshot::detail::PrivateView(raw,4,129,9512,result) &&
        !snapshot::detail::PrivateView(raw,6,128,9512,result),"raw copy may not include padding, misalignment or a different register");
    auto structured=raw;structured.Format=DXGI_FORMAT_UNKNOWN;structured.Buffer.FirstElement=0;structured.Buffer.NumElements=1;
    structured.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_NONE;structured.Buffer.StructureByteStride=64;
    Require(snapshot::detail::PrivateView(structured,6,9600,64,result) && result.Buffer.FirstElement==150 && result.Buffer.NumElements==1 &&
        result.Buffer.StructureByteStride==64,"structured copy preserves its64-byte element");
    Require(!snapshot::detail::PrivateView(structured,6,9616,64,result) && !snapshot::detail::PrivateView(structured,5,9600,64,result),
        "structured material view must not acquire a t5 contract or truncated alignment");
    Views(raw);Views(structured);Bindings(raw,4);Bindings(structured,6);Recordings();
    structured.Buffer.StructureByteStride=128;
    Require(!snapshot::detail::PrivateView(structured,6,9600,64,result),"wrong structured stride rejected");
    std::puts("PASS exact material snapshot views, original bindings and native recordings");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
