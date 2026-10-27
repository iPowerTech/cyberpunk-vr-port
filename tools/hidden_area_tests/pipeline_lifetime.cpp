// Exercises the production shader registry with COM lifetime semantics and deterministic address reuse.
#include "Render/HiddenAreaShaders.hpp"
#include <windows.h>
#include <dxgi.h>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <new>
#include <stdexcept>

extern "C" int GetMenuMode(){return 0;}
extern "C" {std::atomic<uint32_t> CyberpunkVR_HiddenAreaEnabled{0};}
extern "C" std::atomic<uint32_t> CyberpunkVR_ViewDirPatch;
namespace {
int liveTwins=0;
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
class FakePso final:public ID3D12PipelineState {
    std::atomic<ULONG> refs{1};
    bool allocated, twin;
    GUID key{};
    IUnknown* owner{};
public:
    bool rejectPrivateData=false;
    FakePso(bool heap=false,bool replacement=false):allocated(heap),twin(replacement){if(twin)++liveTwins;}
    ~FakePso(){if(owner)owner->Release();if(twin)--liveTwins;}
    ULONG References()const{return refs;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id!=__uuidof(IUnknown)&&id!=__uuidof(ID3D12Object)&&id!=__uuidof(ID3D12DeviceChild)&&id!=__uuidof(ID3D12PipelineState))return E_NOINTERFACE;
        *out=static_cast<ID3D12PipelineState*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto count=--refs;if(!count){if(allocated)delete this;else this->~FakePso();}return count;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid,UINT* bytes,void* data)override{
        if(!bytes)return E_POINTER;if(!owner||guid!=key)return DXGI_ERROR_NOT_FOUND;
        if(!data||*bytes<sizeof(owner)){*bytes=sizeof(owner);return DXGI_ERROR_MORE_DATA;}
        owner->AddRef();*static_cast<IUnknown**>(data)=owner;*bytes=sizeof(owner);return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,UINT,const void*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid,const IUnknown* data)override{
        if(rejectPrivateData)return E_FAIL;
        auto* next=const_cast<IUnknown*>(data);if(next)next->AddRef();
        auto* previous=owner;owner=next;key=guid;if(previous)previous->Release();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID,void** out)override{if(out)*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE GetCachedBlob(ID3DBlob** out)override{if(out)*out=nullptr;return E_NOTIMPL;}
};
uint64_t Hash(const uint8_t* bytes,size_t count){uint64_t h=1469598103934665603ull;for(size_t i=0;i<count;++i){h^=bytes[i];h*=1099511628211ull;}return h;}
}
int main(int argc,char** argv){
    try{
        Require(argc==2,"Expected signed shader fixture path");
        namespace s=cvr::hidden_area::shaders;
        const uint8_t patchedBytes[]{1,2,3,4},nativeBytes[]{5,6,7,8};
        const D3D12_SHADER_BYTECODE patched{patchedBytes,4},native{nativeBytes,4};
        wchar_t exe[32768]{};Require(GetModuleFileNameW(nullptr,exe,32768)!=0,"Executable path");
        auto folder=std::filesystem::path(exe).parent_path()/L"view_dir";std::filesystem::create_directories(folder);
        char name[32];sprintf_s(name,"%016llx.dxil",static_cast<unsigned long long>(Hash(patchedBytes,4)));
        std::filesystem::copy_file(argv[1],folder/name,std::filesystem::copy_options::overwrite_existing);
        Require(s::HasPatch(patched)&&!s::HasPatch(native),"Fixture mapping");
        CyberpunkVR_ViewDirPatch=1;
        alignas(FakePso) std::byte storage[sizeof(FakePso)];
        auto* original=new(storage) FakePso;
        ID3D12PipelineState* twin{};
        unsigned creates=0;
        auto creator=[&](const D3D12_SHADER_BYTECODE&){++creates;twin=new FakePso(true,true);return twin;};
        s::PipelineCreated(original,{},patched,{});
        s::Register(original,patched,creator);
        Require(original->References()==1,"Registration must not retain original PSO");
        Require(s::Bind(original,0)==twin&&s::Bind(original,1)==twin,"Both eyes select current twin");
        s::Register(original,patched,creator);
        Require(creates==1&&liveTwins==1,"Repeated registration must preserve original owner/twin");
        Require(original->Release()==0,"Original destroyed");
        Require(liveTwins==0,"Destroying original must release its twin");

        auto* reused=new(storage) FakePso;
        s::PipelineCreated(reused,{},native,{});
        Require(!s::HasPatch(native),"Unpatched pipeline skips Register in device hook");
        Require(s::Bind(reused,0)==reused&&s::Bind(reused,1)==reused,"Reused address must not inherit another PSO's twin");
        reused->Release();

        reused=new(storage) FakePso;
        // A creation path that does not notify PipelineCreated is also a different lifetime.
        Require(s::Bind(reused,0)==reused,"No dependence on creation notification");
        s::Register(reused,patched,creator);
        Require(liveTwins==1&&creates==2&&s::Bind(reused,0)==twin,"New patched lifetime gets its own owner");
        CyberpunkVR_ViewDirPatch=0;
        Require(s::Bind(reused,0)==reused,"Disabled group retains original");
        CyberpunkVR_ViewDirPatch=1;
        reused->Release();
        Require(liveTwins==0,"Second twin released");

        reused=new(storage) FakePso;reused->rejectPrivateData=true;
        s::Register(reused,patched,creator);
        Require(creates==2&&s::Bind(reused,0)==reused,"Failed attachment must retain original");
        reused->Release();
        reused=new(storage) FakePso;
        s::Register(reused,patched,[](const auto&)->ID3D12PipelineState*{return nullptr;});
        Require(s::Bind(reused,0)==reused,"Failed twin creation must retain original");
        reused->Release();
        Require(liveTwins==0,"No live replacement objects remain");
        puts("PASS: PSO address reuse, repeated registration, owner destruction and failure fallbacks");
        return 0;
    }catch(const std::exception& e){printf("FAIL: %s\n",e.what());return 1;}
}
