#include "Render/NativeTargetLifetime.hpp"
#include <windows.h>

namespace cvr::stereo::target_lifetime {
namespace detail {
struct CaptureProof {texture_read::WritableTargetIdentity identity;Snapshot source;};
struct BorrowProof {std::shared_ptr<const CaptureProof> captured;queue_residency::Scope scope;Snapshot bound;};
}
namespace {
thread_local uint32_t failure{};
thread_local std::array<uint64_t,12> details{};
thread_local std::array<uint64_t,3> hookFailure{};
void Stage(uint32_t stage){failure=stage;details[0]=stage;}
uint64_t Base(){return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));}
bool Memory(void*,uint64_t address,void* output,size_t bytes){
    SIZE_T copied{};return address && address<=UINT64_MAX-bytes &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,bytes,&copied) && copied==bytes;
}
struct ContractResult {bool accepted{};std::array<uint64_t,3> failed{};};
const ContractResult& Contract(){
    static const auto result=[] {
        if(!native_recording::Compatible())return ContractResult{false,{0,1,native_recording::LastFailure()}};
        if(!texture_read::Compatible())return ContractResult{false,{1,1,texture_read::LastFailure()}};
        const auto base=Base();std::array<uint8_t,2048> bytes{};
        for(const auto code:detail::Contract){
            if(!base || base>UINT64_MAX-code.rva || code.bytes>bytes.size() ||
                !Memory(nullptr,base+code.rva,bytes.data(),code.bytes))return ContractResult{false,{code.rva,code.hash,0}};
            const auto actual=native_recording::detail::Fingerprint({bytes.data(),code.bytes});
            if(actual!=code.hash)return ContractResult{false,{code.rva,code.hash,actual}};
        }
        return ContractResult{true,{}};
    }();return result;
}
void Observed(const detail::Snapshot& value){
    details[2]=value.record;details[3]=value.owner;details[4]=value.allocation.address;details[5]=value.allocation.heap;
    details[6]=uint32_t(value.handleReferences);details[7]=uint32_t(value.ownerReferences);details[8]=value.epoch.bucket;
    details[9]=value.epoch.index;details[10]=value.epoch.count;details[11]=value.queueSlot;
}
bool Read(const detail::Key& key,bool source,detail::Snapshot& output){
    Stage(5);uint32_t rejected{};detail::Snapshot first,second;
    if(!detail::ReadSnapshot(Memory,nullptr,Base(),key,source,first,&rejected)){details[1]=rejected;Observed(first);return false;}
    Observed(first);
    if(!detail::ReadSnapshot(Memory,nullptr,Base(),key,source,second,&rejected)){details[1]=rejected;Observed(second);return false;}
    Observed(second);Stage(6);if(!detail::SameIdentity(first,second))return false;
    output=std::move(second);return true;
}
bool Live(const detail::BorrowProof& proof){
    Stage(2);if(!Compatible())return false;
    Stage(8);if(!queue_residency::Validate(proof.scope))return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(proof.captured->identity))return false;
    detail::Snapshot now;if(!Read(proof.captured->source.key,false,now))return false;
    Stage(6);if(!detail::SameIdentity(proof.captured->source,now) || !detail::SameIdentity(proof.bound,now))return false;
    Stage(8);return queue_residency::Validate(proof.scope);
}
}
bool Compatible(){
    const auto& result=Contract();
    if(!result.accepted){std::copy(result.failed.begin(),result.failed.end(),details.begin()+1);return false;}
    detail::ResizeChain chain;uint32_t rejected{};
    const auto cet=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"cyber_engine_tweaks.asi"));
    const auto reject=[&](uint32_t rva){
        hookFailure={rva,0,rejected};std::copy(hookFailure.begin(),hookFailure.end(),details.begin()+1);
        details[4]=chain.relay;details[5]=chain.cet;details[6]=chain.singleton;details[7]=chain.trampoline;return false;
    };
    if(!detail::ReadResizeChain(Memory,nullptr,Base(),cet,chain,&rejected))return reject(detail::ResizeRva);
    if(!detail::ReadShutdownChain(Memory,nullptr,Base(),cet,chain,&rejected))return reject(detail::ShutdownRva);
    hookFailure={};return true;
}
bool Capture(const texture_read::WritableTargetIdentity& identity,Lease& output)try{
    output={};details={};details[11]=UINT64_MAX;Stage(1);
    if(!identity.creation.resource || !identity.creation.heap || identity.creation.kind!=2 || identity.creation.heapOffset ||
        !identity.creation.allocationBytes || identity.creation.allocationBytes==UINT64_MAX || !identity.recording.token ||
        identity.native.resource!=reinterpret_cast<uintptr_t>(identity.creation.resource.Get()) ||
        identity.native.registry!=identity.recording.registry || !identity.originalContext)return false;
    Stage(2);if(!Compatible())return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(identity))return false;
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(identity.recording.list);
    Stage(4);if(!native_recording::ValidateRecording(identity.recording,list,identity.originalContext))return false;
    detail::Key key{identity.native.registry,identity.recording.context,identity.recording.list,identity.native.resource,
        reinterpret_cast<uintptr_t>(identity.creation.heap.Get()),identity.creation.allocationBytes,
        identity.resourceDesc.SampleDesc.Count>1?0x400000ull:0x10000ull,identity.native.handle};
    auto proof=std::make_shared<detail::CaptureProof>();if(!Read(key,true,proof->source))return false;
    if(proof->source.owner!=identity.allocatorOwnerObservation){Stage(6);return false;}
    Stage(4);if(!native_recording::ValidateRecording(identity.recording,list,identity.originalContext))return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(identity))return false;
    proof->identity=identity;output.proof_=std::move(proof);Stage(0);return true;
}catch(...){output={};Stage(9);return false;}
bool Bind(const Lease& lease,ID3D12CommandQueue* queue,const queue_residency::Scope& scope,BorrowedScope& output)try{
    output={};details={};details[11]=UINT64_MAX;Stage(1);
    if(!lease.proof_ || !queue || !scope)return false;
    Stage(2);if(!Compatible())return false;
    Stage(8);if(!queue_residency::Validate(scope))return false;
    const auto& identity=lease.proof_->identity;
    Stage(7);if(!native_recording::ValidateSubmission(identity.recording,queue))return false;
    Stage(8);if(!queue_residency::ContainsRecording(scope,identity.recording))return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(identity,queue))return false;
    auto proof=std::make_shared<detail::BorrowProof>();proof->captured=lease.proof_;proof->scope=scope;
    if(!Read(lease.proof_->source.key,false,proof->bound))return false;
    Stage(6);if(!detail::SameIdentity(lease.proof_->source,proof->bound))return false;
    Stage(7);if(!native_recording::ValidateSubmission(identity.recording,queue))return false;
    if(!Live(*proof))return false;
    output.proof_=std::move(proof);Stage(0);return true;
}catch(...){output={};Stage(9);return false;}
bool Validate(const BorrowedScope& scope)try{
    details={};details[11]=UINT64_MAX;Stage(1);if(!scope.proof_)return false;
    if(!Live(*scope.proof_))return false;
    Stage(0);return true;
}catch(...){Stage(9);return false;}
bool GetBorrowedPageable(ID3D12Device* device,const texture_read::WritableTargetIdentity& expected,
                         const BorrowedScope& scope,BorrowedPageable& output)try{
    output={};details={};details[11]=UINT64_MAX;Stage(1);
    if(!device || !scope.proof_ || !expected.creation.resource || !expected.creation.heap || !expected.recording.token)return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(expected))return false;
    if(!Live(*scope.proof_))return false;
    const auto& captured=scope.proof_->captured->identity;
    Stage(10);if(!detail::SameBorrowedIdentity(expected,captured))return false;
    Stage(11);const auto& creation=captured.creation;
    if(device->GetNodeCount()!=1 || creation.kind!=2 || !creation.resource || !creation.heap || creation.heapOffset ||
        !creation.allocationBytes || creation.allocationBytes==UINT64_MAX)return false;
    Microsoft::WRL::ComPtr<ID3D12Device> resourceDevice,heapDevice;
    if(FAILED(creation.resource->GetDevice(IID_PPV_ARGS(&resourceDevice))) || resourceDevice.Get()!=device ||
        FAILED(creation.heap->GetDevice(IID_PPV_ARGS(&heapDevice))) || heapDevice.Get()!=device ||
        !texture_read::detail::SameResourceDescription(captured.resourceDesc,creation.resource->GetDesc()) ||
        !texture_read::detail::SameHeapDescription(captured.heapDesc,creation.heap->GetDesc()))return false;
    const auto needed=device->GetResourceAllocationInfo(0,1,&captured.resourceDesc);
    if(!needed.SizeInBytes || needed.SizeInBytes==UINT64_MAX || !needed.Alignment || needed.SizeInBytes>creation.allocationBytes ||
        creation.allocationBytes>captured.heapDesc.SizeInBytes)return false;
    BorrowedPageable value;value.identity=captured;value.lifetime=scope;
    if(FAILED(creation.heap.As(&value.pageable)) || !value.pageable)return false;
    Stage(3);if(!texture_read::ValidateWritableIdentity(expected))return false;
    if(!Live(*scope.proof_))return false;
    output=std::move(value);Stage(0);return true;
}catch(...){output={};Stage(9);return false;}
uint32_t LastFailure(){return failure;}
std::array<uint64_t,12> LastDetails(){return details;}
std::array<uint64_t,3> LastContractFailure(){const auto& value=Contract();return value.accepted?hookFailure:value.failed;}
}
