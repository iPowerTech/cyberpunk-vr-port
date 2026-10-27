#include "Render/NativeIaBinding.hpp"
#include "Render/NativeIndirectPolicy.hpp"
#include "Render/RenderProbeScope.hpp"
#include <windows.h>
#include <intrin.h>
#include <utility>

namespace cvr::stereo::geometry {
namespace {
thread_local IndirectBindingFailure indirectFailure{};
constexpr GUID initialStateKey{0xb7b50d38,0xf497,0x4fc3,{0xa3,0xc1,0x62,0x40,0x21,0x37,0x63,0x20}};
bool Read(void*,uint64_t address,void* output,size_t bytes) {
    SIZE_T copied{};return address && address<=UINT64_MAX-bytes &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,bytes,&copied) && copied==bytes;
}
bool Hash(uint64_t rva,size_t bytes,uint64_t expected) {
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::array<uint8_t,2048> data{};if(!base || base>UINT64_MAX-rva || bytes>data.size() || !Read(nullptr,base+rva,data.data(),bytes))return false;
    uint64_t value=14695981039346656037ull;for(size_t i=0;i<bytes;++i)value=(value^data[i])*1099511628211ull;return value==expected;
}
bool Native(bool index,UINT start,UINT count,std::array<NativeBuffer,16>& output) {
    if(!NativeIaCompatible())return false;
    __try {
        const auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));CONTEXT context{};RtlCaptureContext(&context);
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        for(UINT depth=0;depth<32 && context.Rsp>=low && context.Rsp<=high-8;++depth) {
            if(context.Rip>=game && context.Rip-game<=UINT32_MAX) {
                IaFrame frame{context.Rip-game,context.Rsp,context.Rbx,context.Rbp,context.Rdi,context.Rsi,context.R12,context.R14,context.R15};
                if(DecodeIa(frame,index,start,count,Read,nullptr,output))return true;
            }
            DWORD64 image{};const auto entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
            if(entry){PVOID handler{};DWORD64 establisher{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&handler,&establisher,nullptr);}
            else {if(!Read(nullptr,context.Rsp,&context.Rip,8))return false;context.Rsp+=8;}
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
bool CurrentIndirectContext(camera_upload::Context& out){
    uint64_t tls{},thread{},context{},state{},list{};uint8_t initialized{};
    tls=__readgsqword(0x58);
    if(!Read(nullptr,tls,&thread,8) || !thread || thread>UINT64_MAX-0x190 || !Read(nullptr,thread+0x14,&initialized,1) || !initialized ||
        !Read(nullptr,thread+0x188,&context,8) || !context || context>UINT64_MAX-0x68 ||
        !Read(nullptr,context+0x60,&state,8) || !Read(nullptr,context+0x30,&list,8))return false;
    out={context,state,list};return bool(out);
}
uint64_t NativeImageBytes(){
    static const uint64_t bytes=[]()->uint64_t{
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
        if(!Read(nullptr,base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>0x100000 ||
            base>UINT64_MAX-UINT(dos.e_lfanew) || !Read(nullptr,base+UINT(dos.e_lfanew),&nt,sizeof(nt)) ||
            nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt.OptionalHeader.SizeOfImage<=0x624830 ||
            nt.OptionalHeader.SizeOfImage>UINT64_MAX-base)return 0;
        return nt.OptionalHeader.SizeOfImage;
    }();return bytes;
}
bool IndirectOrigin(uint64_t game,IndirectFrame& output){
    const auto imageBytes=NativeImageBytes();if(!game || !imageBytes)return false;
    __try{
        CONTEXT context{};RtlCaptureContext(&context);ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        for(UINT depth=0;depth<32 && high>=8 && context.Rsp>=low && context.Rsp<=high-8;++depth){
            if(context.Rip>=game && context.Rip-game<imageBytes){
                const auto rva=context.Rip-game;
                if(rva!=0x623CC1 && rva!=0x62462F && rva!=0x624830)return false;
                output={rva,context.Rbx,context.Rdi};return true;
            }
            DWORD64 image{};const auto entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
            if(entry){PVOID handler{};DWORD64 establisher{};RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&handler,&establisher,nullptr);}
            else {if(!Read(nullptr,context.Rsp,&context.Rip,8))return false;context.Rsp+=8;}
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}
bool IndirectState(ReadState state){
    return (state.status==5 && !state.value) || ((state.status==1 || state.status==2 || state.status==8 || state.status==9) &&
        (state.value&D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) && !(state.value&~UINT(D3D12_RESOURCE_STATE_GENERIC_READ)));
}
bool SameRecording(const native_recording::Lease& a,const native_recording::Lease& b){
    return a.token && a.token.Get()==b.token.Get() && a.generation && a.generation==b.generation &&
        a.context==b.context && a.list==b.list && a.registry==b.registry;
}
bool SourceDevice(ID3D12GraphicsCommandList* list,ID3D12DeviceChild* object){
    if(!list || !object)return false;Microsoft::WRL::ComPtr<ID3D12Device> expected,actual;
    return SUCCEEDED(list->GetDevice(IID_PPV_ARGS(&expected))) && SUCCEEDED(object->GetDevice(IID_PPV_ARGS(&actual))) && expected==actual;
}
bool DescribeIndirectResource(ID3D12GraphicsCommandList* list,const IndirectNativeRecord& native,ID3D12Resource* actual,
    UINT64 offset,UINT64 bytes,BufferLease& lease,bool& fullBacking){
    if(!actual || native.arguments.resource!=reinterpret_cast<uintptr_t>(actual) || !SourceDevice(list,actual))return false;
    BufferLease pending;pending.resource=actual;const auto desc=actual->GetDesc();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || FAILED(actual->GetHeapProperties(&heap,&flags)))return false;
    pending.gpu=actual->GetGPUVirtualAddress();pending.bytes=desc.Width;pending.heapType=UINT(heap.Type);pending.flags=UINT(desc.Flags);pending.native=native.arguments;
    if(!indirect_binding_detail::SourceRange(native.arguments,pending.gpu,pending.bytes,offset,bytes,fullBacking))return false;
    lease=std::move(pending);return true;
}
}
bool NativeIaCompatible() {
    static const bool result=Hash(0x1F6438,0x26B,0xCD785FCA04BE8218ull) &&
        Hash(0x1F2B68,0x52D,0xA9B022DA686733CAull) && Hash(0x1F6CB0,0xE0,0x86A4C79122B00746ull);
    return result;
}
void BufferCreated(ID3D12Resource* resource,D3D12_RESOURCE_STATES state) {
    if(!resource)return;const auto d=resource->GetDesc();
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || (d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))return;
    const uint32_t value=UINT(state);resource->SetPrivateData(initialStateKey,sizeof(value),&value);
}
BufferLease AcquireBuffer(const NativeBuffer& native,uint64_t address,uint64_t bytes) {
    BufferLease result;
    if(!native.resource || !native.gpu || !bytes || address<native.gpu || address-native.gpu>native.bytes || bytes>native.bytes-(address-native.gpu))return result;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource=reinterpret_cast<ID3D12Resource*>(native.resource);
    const auto d=resource->GetDesc();const auto gpu=resource->GetGPUVirtualAddress();D3D12_HEAP_PROPERTIES heap{};D3D12_HEAP_FLAGS flags{};
    if(d.Dimension!=D3D12_RESOURCE_DIMENSION_BUFFER || !gpu || address<gpu || address-gpu>d.Width || bytes>d.Width-(address-gpu) ||
       FAILED(resource->GetHeapProperties(&heap,&flags)))return result;
    result.resource=std::move(resource);result.gpu=gpu;result.bytes=d.Width;result.heapType=UINT(heap.Type);result.flags=UINT(d.Flags);result.native=native;return result;
}
ReadState ReadStateAtDraw(ID3D12GraphicsCommandList* list,const BufferLease& source) {
    if(!list || !source.resource)return {0,3,source.native.kind};
    if(source.heapType==D3D12_HEAP_TYPE_UPLOAD)return {D3D12_RESOURCE_STATE_GENERIC_READ,1,source.native.kind};
    if(source.heapType!=D3D12_HEAP_TYPE_DEFAULT)return {0,6,source.native.kind};
    static const bool compatible=NativeIaCompatible() && Hash(0x1F51C4,0x180,0xADFE5FFA4DFDB190ull) &&
        Hash(0x1F50DC,0xE8,0xD964D2B6849C7BBBull) && Hash(0xFF060,0x154,0x154108C1EC60C930ull) &&
        Hash(0x220110,0x5A4,0x5C372BC97D5DEB01ull);
    if(!compatible)return {0,7,source.native.kind};
    auto state=ReadNativeState(source.native,reinterpret_cast<uintptr_t>(list),Read,nullptr);
    // Native class 1 bypasses both state-map update paths. Direct construction
    // chooses COMMON/GENERIC_READ; both fixed-read pool constructors use GENERIC_READ.
    // Early pools predate our API hooks, so their fallback also checks the full
    // constructor bodies, in addition to the live native binding identity above.
    if(state.status==5 && source.native.kind==1 && !(source.flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
        uint32_t initial=UINT_MAX;UINT bytes=sizeof(initial);
        const auto receipt=source.resource->GetPrivateData(initialStateKey,&bytes,&initial);
        if(SUCCEEDED(receipt)) {
            if(bytes==sizeof(initial) && (initial==D3D12_RESOURCE_STATE_COMMON || initial==D3D12_RESOURCE_STATE_GENERIC_READ))return {D3D12_RESOURCE_STATE_GENERIC_READ,8,1};
        } else if(receipt==DXGI_ERROR_NOT_FOUND) {
            static const bool pools=Hash(0x21DCD4,0x4A4,0xADFEE15F3177A181ull) && Hash(0x21E238,0x39D,0x37C1CF698BAE525Eull);
            if(pools)return {D3D12_RESOURCE_STATE_GENERIC_READ,9,1};
        }
    }
    return state;
}
BufferLease BoundIndex(const D3D12_INDEX_BUFFER_VIEW& view) {
    std::array<NativeBuffer,16> sources;
    if(!view.BufferLocation || (view.Format!=DXGI_FORMAT_R16_UINT && view.Format!=DXGI_FORMAT_R32_UINT) || !Native(true,0,1,sources))return {};
    return AcquireBuffer(sources[0],view.BufferLocation,view.SizeInBytes);
}
std::array<BufferLease,16> BoundVertices(UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW* views) {
    std::array<BufferLease,16> result{};std::array<NativeBuffer,16> sources;
    if(!views || !count || start>16 || count>16-start || !Native(false,start,count,sources))return result;
    for(UINT i=0;i<count;++i)result[i]=AcquireBuffer(sources[i],views[i].BufferLocation,views[i].SizeInBytes);
    return result;
}
bool NativeIndirectCompatible(){
    static const bool result=[](){for(const auto& code:indirect_binding_detail::Contract)if(!Hash(code.rva,code.bytes,code.hash))return false;return true;}();
    return result;
}
IndirectBindingFailure LastIndirectBindingFailure(){return indirectFailure;}
bool BoundIndirect(ID3D12GraphicsCommandList* list,ID3D12CommandSignature* signature,UINT maximum,ID3D12Resource* arguments,UINT64 offset,
    ID3D12Resource* countBuffer,UINT64 countOffset,const native_recording::Lease& recording,IndirectBinding& output) try {
    indirectFailure=IndirectBindingFailure::Arguments;
    if(probe::internalCommands || !list || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || !signature || !arguments ||
        maximum!=1 || countBuffer || countOffset || offset%4)return false;
    indirectFailure=IndirectBindingFailure::Fingerprints;if(!NativeIndirectCompatible())return false;
    indirectFailure=IndirectBindingFailure::Device;if(!SourceDevice(list,signature))return false;
    const auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));IndirectFrame frame;camera_upload::Context current;
    indirectFailure=IndirectBindingFailure::Origin;if(!IndirectOrigin(game,frame))return false;
    indirectFailure=IndirectBindingFailure::CurrentContext;if(!CurrentIndirectContext(current))return false;
    indirectFailure=IndirectBindingFailure::Recording;if(!native_recording::ValidateRecording(recording,list,current))return false;
    const IndirectCall call{reinterpret_cast<uintptr_t>(list),reinterpret_cast<uintptr_t>(signature),reinterpret_cast<uintptr_t>(arguments),
        offset,reinterpret_cast<uintptr_t>(countBuffer),countOffset,maximum};
    IndirectNativeRecord native;
    indirectFailure=IndirectBindingFailure::Decode;
    if(!indirect_binding_detail::Decode(game,frame,current,call,Read,nullptr,native) || native.arguments.registry!=recording.registry)return false;
    const auto receipt=indirect_policy::Describe(signature);
    indirectFailure=IndirectBindingFailure::Signature;
    if(indirect_policy::detail::DrawKind(receipt)!=native.signatureKind || indirect_policy::detail::PureDrawStride(receipt)!=native.signatureStride)return false;
    IndirectBinding pending;pending.signature=signature;pending.context=current;pending.recording=recording;
    pending.argumentOffset=offset;pending.argumentBytes=UINT64(maximum)*native.signatureStride;
    indirectFailure=IndirectBindingFailure::ResourceRange;
    if(!DescribeIndirectResource(list,native,arguments,offset,pending.argumentBytes,pending.arguments,pending.fullBacking))return false;
    indirectFailure=IndirectBindingFailure::State;
    pending.state=ReadStateAtDraw(list,pending.arguments);if(!IndirectState(pending.state))return false;
    pending.rawRecord=native.rawRecord;pending.recordAddress=native.recordAddress;pending.callerRva=native.callerRva;
    pending.maxCount=maximum;pending.signatureKind=native.signatureKind;pending.signatureStride=native.signatureStride;
    if(!RevalidateIndirect(list,recording,pending))return false;output=std::move(pending);indirectFailure=IndirectBindingFailure::None;return true;
}catch(...){indirectFailure=IndirectBindingFailure::Exception;return false;}
bool RevalidateIndirect(ID3D12GraphicsCommandList* list,const native_recording::Lease& recording,const IndirectBinding& saved) try {
    indirectFailure=IndirectBindingFailure::Arguments;
    if(probe::internalCommands || !list || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT || !saved.arguments.resource || !saved.signature)return false;
    indirectFailure=IndirectBindingFailure::Fingerprints;if(!NativeIndirectCompatible())return false;
    indirectFailure=IndirectBindingFailure::Recording;if(!SameRecording(recording,saved.recording))return false;
    indirectFailure=IndirectBindingFailure::Device;if(!SourceDevice(list,saved.signature.Get()))return false;
    const auto game=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));IndirectFrame frame;camera_upload::Context current;
    indirectFailure=IndirectBindingFailure::Origin;if(!IndirectOrigin(game,frame))return false;
    indirectFailure=IndirectBindingFailure::CurrentContext;if(!CurrentIndirectContext(current) || current!=saved.context)return false;
    indirectFailure=IndirectBindingFailure::Origin;if(!indirect_binding_detail::MatchesOrigin(frame,saved.callerRva,current,recording.registry))return false;
    indirectFailure=IndirectBindingFailure::Recording;if(!native_recording::ValidateRecording(recording,list,current))return false;
    const IndirectCall call{reinterpret_cast<uintptr_t>(list),reinterpret_cast<uintptr_t>(saved.signature.Get()),reinterpret_cast<uintptr_t>(saved.arguments.resource.Get()),
        saved.argumentOffset,0,0,saved.maxCount};
    IndirectNativeRecord fresh,expected;
    expected.arguments=saved.arguments.native;expected.context=saved.context;expected.rawRecord=saved.rawRecord;expected.recordAddress=saved.recordAddress;
    expected.signature=call.signature;expected.callerRva=saved.callerRva;expected.signatureKind=saved.signatureKind;expected.signatureStride=saved.signatureStride;
    indirectFailure=IndirectBindingFailure::Decode;
    if(!indirect_binding_detail::Decode(game,frame,current,call,Read,nullptr,fresh) || !indirect_binding_detail::SameRecord(fresh,expected) || fresh.arguments.registry!=recording.registry ||
        saved.argumentBytes!=UINT64(saved.maxCount)*saved.signatureStride)return false;
    const auto receipt=indirect_policy::Describe(saved.signature.Get());
    indirectFailure=IndirectBindingFailure::Signature;
    if(indirect_policy::detail::DrawKind(receipt)!=saved.signatureKind || indirect_policy::detail::PureDrawStride(receipt)!=saved.signatureStride)return false;
    BufferLease resource;bool fullBacking{};
    indirectFailure=IndirectBindingFailure::ResourceRange;
    if(!DescribeIndirectResource(list,fresh,saved.arguments.resource.Get(),saved.argumentOffset,saved.argumentBytes,resource,fullBacking) ||
        resource.gpu!=saved.arguments.gpu || resource.bytes!=saved.arguments.bytes || resource.heapType!=saved.arguments.heapType || resource.flags!=saved.arguments.flags || fullBacking!=saved.fullBacking)return false;
    const auto state=ReadStateAtDraw(list,resource);
    indirectFailure=IndirectBindingFailure::State;
    if(!IndirectState(state) || state.value!=saved.state.value || state.status!=saved.state.status || state.kind!=saved.state.kind)return false;
    camera_upload::Context finalContext;IndirectNativeRecord finalRecord;IndirectFrame finalFrame;
    indirectFailure=IndirectBindingFailure::Revalidation;
    const bool valid=IndirectOrigin(game,finalFrame) && CurrentIndirectContext(finalContext) && finalContext==current &&
        indirect_binding_detail::MatchesOrigin(finalFrame,saved.callerRva,finalContext,recording.registry) &&
        native_recording::ValidateRecording(recording,list,finalContext) &&
        indirect_binding_detail::Decode(game,finalFrame,finalContext,call,Read,nullptr,finalRecord) && indirect_binding_detail::SameRecord(finalRecord,fresh);
    if(valid)indirectFailure=IndirectBindingFailure::None;return valid;
}catch(...){indirectFailure=IndirectBindingFailure::Exception;return false;}
}
