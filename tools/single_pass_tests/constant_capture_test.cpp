#include "Render/NativeConstantCapture.hpp"
#include "Render/StereoDescriptorAudit.hpp"
#include "Render/NativeTextureRead.hpp"
#include "Render/ConstantUploadRegistry.hpp"
#include "Render/RegisteredConstantUpload.hpp"
#include "Render/CommandResources.hpp"
#include "Render/ConstantTraceReplay.hpp"
#include "Render/ObservationProfile.hpp"
#include "Render/UploadTouchReceipt.hpp"
#include "Render/SourceResourceIntent.hpp"
#include <dxgi1_6.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <mutex>
#include <thread>
using Microsoft::WRL::ComPtr;
using namespace cvr::stereo;
namespace cvr::stereo::native_probe {bool ObserveBindings(){return true;}}
namespace cvr::stereo::native_probe {bool CurrentState(ID3D12GraphicsCommandList*,DrawRecord& output){output={};return false;}}
namespace cvr::stereo::families {
std::shared_ptr<const Family> Bound(ID3D12GraphicsCommandList*){return {};}
std::shared_ptr<const Family> Lookup(ID3D12PipelineState*){return {};}
}
namespace cvr::stereo::camera_upload {Context CurrentContext(uintptr_t){return {};}}
namespace cvr::stereo::scene_state {bool CaptureSourceBindings(ID3D12GraphicsCommandList*,Bindings& output){output={};return false;}}
namespace cvr::stereo::texture_read {
bool PinFixedAsset(ID3D12GraphicsCommandList*,uint64_t,FixedAssetPins& output){output={};return false;}
bool GetFixedPinnedResource(const FixedAssetPins&,ComPtr<ID3D12Resource>& output){output.Reset();return false;}
bool AcquirePinnedImmutable(ID3D12GraphicsCommandList*,const FixedAssetPins&,ImmutableLease& output){output={};return false;}
bool ValidateImmutable(const ImmutableLease&,ID3D12CommandQueue*){return false;}
uint32_t LastFailure(){return 120;}
std::array<uint64_t,8> FixedPinLastDetails(){return {};}
}
namespace cvr::stereo::families {bool Enabled(){return true;}}
extern "C" std::atomic<uint32_t> CyberpunkVR_NativeStereoProbeState{1};
static std::array<std::array<uint8_t,256>,2> memory;
static bool readable=true,torn=false;
static bool Read(ID3D12CommandList*,uint64_t address,void* out,size_t bytes,uint64_t* receipt) {
    if(receipt)*receipt=0;
    if(!readable || address<0x10000 || address>0x10100 || (address&255) || !bytes || bytes>112)return false;
    auto& data=memory[(address-0x10000)/256];std::memcpy(out,data.data(),bytes);if(torn)++data[0];return true;
}
static void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static void Check(HRESULT hr){Require(SUCCEEDED(hr),"D3D12 setup failure");}
static void ResourceIntentTests(ID3D12Device* device,const ComPtr<ID3D12Resource>& source,
    const ComPtr<ID3D12Resource>& destination,const ComPtr<ID3D12Resource>& alias,ID3D12CommandQueue* queue){
    std::array<ComPtr<ID3D12CommandAllocator>,3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>,3> lists;
    for(UINT n=0;n<lists.size();++n){
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[n])));
        Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[n].Get(),nullptr,IID_PPV_ARGS(&lists[n])));
    }
    const auto address=destination->GetGPUVirtualAddress();Require(address==alias->GetGPUVirtualAddress(),"Qualified intent alias fixture must share the GPU address");
    const auto clear=[](){constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=1;};
    const auto write=[&](UINT list,UINT destinationOffset=0,UINT sourceOffset=0,UINT bytes=112){
        constants::ObserveRegisteredCopy(lists[list].Get(),destination.Get(),destinationOffset,source.Get(),sourceOffset,bytes);
    };
    const auto capture=[&](uint64_t position,UINT list,constants::RegisteredResourceIntent& intent,UINT offset=0,UINT bytes=112){
        return constants::CaptureRegisteredResourceIntent(position,lists[list].Get(),destination,address+offset,bytes,intent);
    };
    const auto submit=[&](std::initializer_list<UINT> indices){
        std::vector<ID3D12CommandList*> batch;for(const auto index:indices)batch.push_back(lists[index].Get());
        constants::SubmitRegistered(queue,UINT(batch.size()),batch.data());constants::FreezeRegistered();
    };
    const auto matches=[](const constants::RegisteredResourceIntent& intent,uint8_t byte,size_t count){
        const auto bytes=intent.Bytes();return bytes.size()==count && std::all_of(bytes.begin(),bytes.end(),[=](uint8_t value){return value==byte;});
    };
    constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=0;CyberpunkVR_RegisteredIntentEnable=1;
    Require(constants::RegisteredIntentTracking() && !constants::RegisteredTracking(),"Independent intent mode started unrelated queue tracking");
    constants::PollRegisteredIntent();
    constants::SetReader(constants::ReadRegistered,constants::ValidateRegistered,constants::MarkRegisteredRead,constants::ResolveRegisteredRead);
    constants::Begin();write(0);source_resource_intent::Candidate intentOnly;
    Require(!constants::MarkRegisteredRead(lists[0].Get()) && source_resource_intent::CaptureAtDraw(destination,address,112,intentOnly),
        "Independent current-copy candidate was absent or borrowed the position ledger");
    constants::End();source_resource_intent::Candidate again;
    Require(source_resource_intent::CaptureAtDraw(destination,address,112,again),"Inactive legacy capture discarded the unrelated current-copy candidate");
    CyberpunkVR_RegisteredIntentEnable=0;constants::PollRegisteredIntent();constants::ClearRegistered();source_resource_intent::Candidate stopped;
    Require(!source_resource_intent::CaptureAtDraw(destination,address,112,stopped),"Disabled intent mode retained current candidate ranges");
    Require(!constants::RegisteredTracking() && !constants::MarkRegisteredRead(lists[0].Get()),"Independent tracking leaked after disabling both controls");
    constants::SetReader(Read);
    clear();const auto missing=constants::MarkRegisteredRead(lists[0].Get());constants::RegisteredResourceIntent absent;
    Require(!capture(missing,0,absent) && !absent,"Missing recorded producer manufactured an intent");
    write(0);Require(!capture(missing,0,absent),"Same-recording producer after the original read point was accepted");
    const auto first=constants::MarkRegisteredRead(lists[0].Get());constants::RegisteredResourceIntent original;
    Require(capture(first,0,original,4,32) && matches(original,0x21,32) && original.Offset()==4 && original.Payload() &&
        original.ReadPosition()==first && original.ProducerPosition().recording && original.ProducerPosition().command,
        "Exact destination/range intent lost its typed immutable payload receipt");
    Require(!constants::ValidateRegisteredResourceIntent(original),"Provisional bytes were treated as final before freeze");
    constants::RegisteredResourceIntent wrong;
    Require(!constants::CaptureRegisteredResourceIntent(first,lists[0].Get(),alias,address+4,32,wrong) && !wrong,
        "Same-address alias resource borrowed the exact producer's intent");
    const ComPtr<ID3D12Resource> noResource;
    Require(!constants::CaptureRegisteredResourceIntent(first,lists[0].Get(),noResource,address,112,wrong) &&
        !constants::CaptureRegisteredResourceIntent(first,lists[1].Get(),destination,address,112,wrong),
        "Missing ownership or another consumer list borrowed an original position");
    write(0,0,256);constants::RegisteredResourceIntent oldAgain,newer;
    Require(capture(first,0,oldAgain,4,32) && oldAgain.Payload()==original.Payload() && matches(oldAgain,0x21,32),
        "Latest address-map entry replaced the saved original-point producer");
    const auto second=constants::MarkRegisteredRead(lists[0].Get());
    Require(capture(second,0,newer,4,32) && newer.Payload()!=original.Payload() && matches(newer,0x43,32),
        "Current read point did not select its recorded producer");
    submit({0});constants::ConstantWriteTimeline::Result resolved;
    Require(constants::ValidateRegisteredResourceIntent(original,&resolved) && resolved.payload==original.Payload() && resolved.offset==4 &&
        constants::ValidateRegisteredResourceIntent(newer),"Final ledger did not match both exact original-point receipts");
    constants::RegisteredResourceIntent submitted;
    Require(!capture(second,0,submitted),"Post-submission lookup manufactured an original-callback intent");

    clear();const auto uploadPoint=constants::MarkRegisteredRead(lists[0].Get());constants::RegisteredResourceIntent mapped;
    Require(!constants::CaptureRegisteredResourceIntent(uploadPoint,lists[0].Get(),source,source->GetGPUVirtualAddress(),32,mapped),
        "Resource intent fell back to readable UPLOAD memory without a producer");
    clear();write(0,0,0,4);const auto partialPoint=constants::MarkRegisteredRead(lists[0].Get());
    constants::RegisteredResourceIntent partial;
    Require(!capture(partialPoint,0,partial),"Partial producer range was reconstructed from unrelated historical bytes");
    clear();write(0);write(0,4,256,4);const auto overwritePoint=constants::MarkRegisteredRead(lists[0].Get());
    Require(!capture(overwritePoint,0,partial) && capture(overwritePoint,0,partial,4,4) && matches(partial,0x43,4),
        "Latest partial overwrite was ignored or expanded to a full intent");
    clear();write(0);constants::InvalidateRegistered(lists[0].Get(),destination.Get());
    const auto unknownPoint=constants::MarkRegisteredRead(lists[0].Get());constants::RegisteredResourceIntent unknown;
    Require(!capture(unknownPoint,0,unknown),"Unknown extra writer reused an older known payload");
    clear();write(0);const auto abandonedPoint=constants::MarkRegisteredRead(lists[0].Get());constants::ResetRegistered(lists[0].Get());
    Require(!capture(abandonedPoint,0,unknown),"Abandoned original read position retained an intent");

    clear();const auto reverseRecorded=constants::MarkRegisteredRead(lists[1].Get());write(0);
    constants::RegisteredResourceIntent pending;
    Require(capture(reverseRecorded,1,pending) && matches(pending,0x21,112),"Unique pending producer required guessed cross-list CPU order");
    submit({0,1});Require(constants::ValidateRegisteredResourceIntent(pending),"Ordered producer submitted after consumer recording lost its final proof");
    clear();const auto reversedPosition=constants::MarkRegisteredRead(lists[1].Get());write(0);
    constants::RegisteredResourceIntent reversed;
    Require(capture(reversedPosition,1,reversed),"Provisional reverse-submission fixture failed to capture");
    submit({1,0});Require(!constants::ValidateRegisteredResourceIntent(reversed),"Producer submitted after the consumer acquired a final writer proof");
    clear();write(0);const auto changedPosition=constants::MarkRegisteredRead(lists[1].Get());constants::RegisteredResourceIntent changed;
    Require(capture(changedPosition,1,changed),"Extra-writer fixture failed to capture its initial candidate");
    write(0,0,256);submit({0,1});
    Require(!constants::ValidateRegisteredResourceIntent(changed,&resolved) && resolved.status==constants::ConstantWriteTimeline::Status::Ready &&
        resolved.payload!=changed.Payload() && matches(changed,0x21,112),"Final changed writer was accepted or mutated provisional bytes");
    clear();write(0);write(2,0,256);const auto competingPosition=constants::MarkRegisteredRead(lists[1].Get());
    constants::RegisteredResourceIntent competing;
    Require(!capture(competingPosition,1,competing),"Competing producer recordings were selected by CPU observation order");
    clear();write(0);const auto unstablePosition=constants::MarkRegisteredRead(lists[1].Get());constants::RegisteredResourceIntent unstable;
    Require(capture(unstablePosition,1,unstable),"Staging-change fixture failed to capture");
    void* mappedSource{};Check(source->Map(0,nullptr,&mappedSource));static_cast<uint8_t*>(mappedSource)[0]=0x7f;source->Unmap(0,nullptr);
    submit({0,1});Require(!constants::ValidateRegisteredResourceIntent(unstable) && matches(unstable,0x21,112),
        "Changed staging source passed final proof or changed the owned intent snapshot");
    Check(source->Map(0,nullptr,&mappedSource));static_cast<uint8_t*>(mappedSource)[0]=0x21;source->Unmap(0,nullptr);
    for(auto& command:lists)cvr::gpu::ResetCommandResources(command.Get());constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=0;
}
int main() try {
    constants::UploadTouchReceipt touch;
    Require(touch.Set(0x10000,1024,1) && touch.Take(0x10100,256,1) && !touch.Take(0x10100,256,1),"Touch receipt was not single-use");
    for(const auto address:{uint64_t(0xffff),uint64_t(0x10301),UINT64_MAX}){
        Require(touch.Set(0x10000,1024,1) && !touch.Take(address,256,1) && !touch.Take(0x10000,256,1),"Bad range did not consume receipt");
    }
    Require(touch.Set(0x10000,1024,1) && !touch.Take(0x10000,256,2),"Registry mutation kept an old receipt");
    Require(!touch.Set(UINT64_MAX-10,20,1) && !touch.Take(UINT64_MAX-10,1,1) && !touch.Set(0x10000,1024,0),"Receipt overflow or missing generation accepted");
    Require(touch.Set(0x10000,1024,3) && !touch.Take(0x10000,0,3),"Empty touch accepted");
    {observation_profile::Scope disabled(observation_profile::Phase::Cbv);}
    Require(!CyberpunkVR_ObservationProfileStats[0],"Disabled observer profile recorded calls");
    CyberpunkVR_ObservationProfileEnable=1;
    {observation_profile::Scope enabled(observation_profile::Phase::Cbv);Sleep(1);}
    CyberpunkVR_ObservationProfileEnable=0;
    Require(CyberpunkVR_ObservationProfileStats[0]==1 && CyberpunkVR_ObservationProfileStats[1]>0,"Enabled observer profile lost interval");
    ComPtr<IDXGIFactory4> factory;Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));ComPtr<IDXGIAdapter> adapter;Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;Check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));descriptors::Initialize(device.Get());
    D3D12_RESOURCE_DESC uploadDesc{};uploadDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;uploadDesc.Width=1024;uploadDesc.Height=uploadDesc.DepthOrArraySize=uploadDesc.MipLevels=uploadDesc.SampleDesc.Count=1;
    uploadDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES properties{};properties.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> upload;Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    void* mapped{};Check(upload->Map(0,nullptr,&mapped));std::memset(mapped,0x52,1024);upload->Unmap(0,nullptr);
    const auto uploadAddress=upload->GetGPUVirtualAddress();constants::UploadCreated(upload.Get());constants::UploadCreated(upload.Get());
    constants::ObserveResourceApi(upload.Get());Require(upload->GetGPUVirtualAddress()==uploadAddress && !CyberpunkVR_ConstantUploadApiHits,"Resource API observer changed a foreign caller's result");
    Require(CyberpunkVR_ConstantUploadCount==1 && CyberpunkVR_ConstantUploadBytes==1024,"Duplicate upload retained");
    const D3D12_CONSTANT_BUFFER_VIEW_DESC hotCbv{uploadAddress+256,256};
    constants::UploadAddressObserved(upload.Get(),uploadAddress);constants::TouchUpload(&hotCbv);
    Require(CyberpunkVR_ConstantUploadFastStats[0]==1 && CyberpunkVR_ConstantUploadFastStats[2]==1 &&
        !CyberpunkVR_ConstantUploadFastStats[3],"Live API receipt was not consumed");
    constants::TouchUpload(&hotCbv);Require(CyberpunkVR_ConstantUploadFastStats[2]==1 && CyberpunkVR_ConstantUploadFastStats[4]==1 &&
        !CyberpunkVR_ConstantUploadFastStats[3],"Retained range hint did not replace repeated lookup");
    CyberpunkVR_ConstantUploadFastPath=0;
    constants::UploadAddressObserved(upload.Get(),uploadAddress);constants::TouchUpload(&hotCbv);
    Require(CyberpunkVR_ConstantUploadFastStats[0]==1 && CyberpunkVR_ConstantUploadFastStats[2]==1 &&
        CyberpunkVR_ConstantUploadFastStats[3]==1,"Disabled fast path still bypassed lookup");
    CyberpunkVR_ConstantUploadFastPath=1;
    // A foreign base must not borrow this resource's retained identity.
    constants::UploadAddressObserved(upload.Get(),uploadAddress+256);constants::TouchUpload(&hotCbv);
    Require(CyberpunkVR_ConstantUploadFastStats[2]==1 && CyberpunkVR_ConstantUploadFastStats[3]==2,"Wrong resource/base pair accepted");
    upload.Reset();std::array<uint8_t,256> copied{};
    Require(constants::ReadUpload(uploadAddress+256,copied.data(),256) && copied[0]==0x52 && copied.back()==0x52,"Owned upload mapping lost after native reference release");
    Require(!constants::ReadUpload(uploadAddress+1024,copied.data(),1) && !constants::ReadUpload(uploadAddress-1,copied.data(),256) &&
        !constants::ReadUpload(UINT64_MAX-2,copied.data(),256),"Upload bounds accepted");
    const auto fixedReads=CyberpunkVR_ConstantUploadReads.load(),fixedMisses=CyberpunkVR_ConstantUploadMisses.load();
    Require(constants::ReadFixedUpload(uploadAddress+256,copied.data(),copied.size()) && copied.front()==0x52 && copied.back()==0x52 &&
        CyberpunkVR_ConstantUploadReads==fixedReads+1,"Fixed upload lost owned mapping or read accounting");
    Require(!constants::ReadFixedUpload(uploadAddress+1024,copied.data(),1) && !constants::ReadFixedUpload(uploadAddress-1,copied.data(),256) &&
        !constants::ReadFixedUpload(UINT64_MAX-2,copied.data(),256) && CyberpunkVR_ConstantUploadMisses==fixedMisses+3,"Fixed upload bounds or miss accounting changed");
    Require(!constants::ReadFixedUpload(0,copied.data(),1) && !constants::ReadFixedUpload(uploadAddress,nullptr,1) &&
        !constants::ReadFixedUpload(uploadAddress,copied.data(),0) && !constants::ReadFixedUpload(uploadAddress,copied.data(),65537) &&
        CyberpunkVR_ConstantUploadMisses==fixedMisses+3,"Fixed upload argument validation changed miss accounting");
    for(UINT i=0;i<270;++i) {
        D3D12_CONSTANT_BUFFER_VIEW_DESC hot{uploadAddress,256};constants::TouchUpload(&hot);
        ComPtr<ID3D12Resource> temporary;Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&temporary)));
        constants::UploadCreated(temporary.Get());
    }
    Require(CyberpunkVR_ConstantUploadCount==256 && CyberpunkVR_ConstantUploadEvictions==15 && !CyberpunkVR_ConstantUploadFailures &&
        constants::ReadUpload(uploadAddress,copied.data(),256) && copied[0]==0x52,"Active constant upload evicted during streaming churn");
    {
        ComPtr<ID3D12Resource> owner,changer;
        Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&owner)));
        const auto address=owner->GetGPUVirtualAddress();
        constants::UploadAddressObserved(owner.Get(),address);
        const auto receipts=CyberpunkVR_ConstantUploadFastStats[2].load(),lookups=CyberpunkVR_ConstantUploadFastStats[3].load();
        Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&changer)));
        constants::UploadCreated(changer.Get());
        const D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{address,256};constants::TouchUpload(&cbv);
        Require(CyberpunkVR_ConstantUploadFastStats[2]==receipts && CyberpunkVR_ConstantUploadFastStats[3]==lookups+1,"Changed registry accepted old API receipt");
        Check(owner->Map(0,nullptr,&mapped));std::memset(mapped,0x63,1024);owner->Unmap(0,nullptr);
        std::atomic<bool> failed{};std::vector<std::thread> threads;
        for(unsigned i=0;i<8;++i)threads.emplace_back([&]{
            std::array<uint8_t,256> actual{};
            for(unsigned j=0;j<1000;++j){
                constants::UploadAddressObserved(owner.Get(),address);constants::TouchUpload(&cbv);
                if(!constants::ReadUpload(address,actual.data(),actual.size()) || actual.front()!=0x63 || actual.back()!=0x63)failed=true;
            }
        });
        for(auto& thread:threads)thread.join();
        Require(!failed && CyberpunkVR_ConstantUploadFastStats[2]==receipts+8000 && CyberpunkVR_ConstantUploadFastStats[5]>=7992,
            "Concurrent receipts, weak hints or fresh mapped reads failed");
        std::atomic<bool> startChurn{},churnDone{},churnFailed{};
        std::thread churn([&]{
            while(!startChurn.load())std::this_thread::yield();
            try{for(unsigned i=0;i<400;++i){
                ComPtr<ID3D12Resource> transient;
                Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&transient)));
                constants::UploadCreated(transient.Get());
            }}catch(...){churnFailed=true;}
            churnDone=true;
        });
        startChurn=true;
        do{
            constants::UploadAddressObserved(owner.Get(),address);constants::TouchUpload(&cbv);
            if(constants::ReadUpload(address,copied.data(),copied.size()) && (copied.front()!=0x63 || copied.back()!=0x63))failed=true;
        }while(!churnDone.load());
        churn.join();constants::UploadAddressObserved(owner.Get(),address);
        Require(!failed && !churnFailed && !CyberpunkVR_ConstantUploadFailures && CyberpunkVR_ConstantUploadCount<=256 &&
            constants::ReadUpload(address,copied.data(),copied.size()) && copied.front()==0x63 && copied.back()==0x63,
            "Registry churn retained a stale mapping or exceeded its ownership bound");
        Require(constants::ReadFixedUpload(address,copied.data(),copied.size()) && copied.front()==0x63 && copied.back()==0x63,
            "Fixed upload lost a retained allocation after registry churn");
        Check(owner->Map(0,nullptr,&mapped));std::memset(mapped,0x71,1024);owner->Unmap(0,nullptr);
        constants::UploadAddressObserved(owner.Get(),address);constants::TouchUpload(&cbv);owner.Reset();
        Require(constants::ReadUpload(address,copied.data(),copied.size()) && copied.front()==0x71 && copied.back()==0x71,
            "Receipt cached data or dropped retained mapping ownership");
        Require(constants::ReadFixedUpload(address,copied.data(),copied.size()) && copied.front()==0x71 && copied.back()==0x71,
            "Fixed upload cached stale CPU bytes or dropped retained mapping ownership");
    }
    {
        const auto customProperties=device->GetCustomHeapProperties(0,D3D12_HEAP_TYPE_UPLOAD);
        Require(customProperties.Type==D3D12_HEAP_TYPE_CUSTOM,"Custom upload fixture did not report CUSTOM heap properties");
        ComPtr<ID3D12Resource> custom;
        Check(device->CreateCommittedResource(&customProperties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&custom)));
        Check(custom->Map(0,nullptr,&mapped));std::memset(mapped,0x6d,1024);custom->Unmap(0,nullptr);
        const auto address=custom->GetGPUVirtualAddress();constants::UploadCreated(custom.Get());custom.Reset();
        const auto reads=CyberpunkVR_ConstantUploadReads.load(),misses=CyberpunkVR_ConstantUploadMisses.load();copied.fill(0xab);
        Require(!constants::ReadFixedUpload(address+256,copied.data(),copied.size()) && copied.front()==0xab && copied.back()==0xab &&
            CyberpunkVR_ConstantUploadMisses==misses+1 && CyberpunkVR_ConstantUploadReads==reads,"Fixed upload accepted CPU-accessible CUSTOM memory");
        Require(constants::ReadUpload(address+256,copied.data(),copied.size()) && copied.front()==0x6d && copied.back()==0x6d &&
            CyberpunkVR_ConstantUploadReads==reads+1,"General upload reader lost CUSTOM heap support");
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 uploadOptions{};
    if(SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16,&uploadOptions,sizeof(uploadOptions))) && uploadOptions.GPUUploadHeapSupported){
        auto gpuProperties=properties;gpuProperties.Type=D3D12_HEAP_TYPE_GPU_UPLOAD;ComPtr<ID3D12Resource> gpuUpload;
        Check(device->CreateCommittedResource(&gpuProperties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&gpuUpload)));
        Check(gpuUpload->Map(0,nullptr,&mapped));std::memset(mapped,0x79,1024);gpuUpload->Unmap(0,nullptr);
        const auto address=gpuUpload->GetGPUVirtualAddress();constants::UploadCreated(gpuUpload.Get());gpuUpload.Reset();
        Require(constants::ReadFixedUpload(address+256,copied.data(),copied.size()) && copied.front()==0x79 && copied.back()==0x79,
            "Fixed upload rejected supported GPU_UPLOAD memory");
    }
    ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
    Check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));descriptors::HeapCreated(device.Get(),heap.Get());
    const UINT stride=device->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();auto gpu=heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,1,1,0,0};D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;parameter.DescriptorTable={1,&range};
    D3D12_ROOT_SIGNATURE_DESC rd{1,&parameter,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_NONE};ComPtr<ID3DBlob> bytes,error;Check(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&bytes,&error));
    ComPtr<ID3D12RootSignature> root;Check(device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&root)));
    descriptors::RootCreated(root.Get(),bytes->GetBufferPointer(),bytes->GetBufferSize());
    auto paired=std::make_shared<families::RootVariant>();const PairedBinding binding{D3D12_DESCRIPTOR_RANGE_TYPE_CBV,D3D12_SHADER_VISIBILITY_VERTEX,1,100};
    Check(BuildPairedRoot(bytes->GetBufferPointer(),bytes->GetBufferSize(),{&binding,1},paired->plan));
    Check(device->CreateRootSignature(0,paired->plan.serialized.data(),paired->plan.serialized.size(),IID_PPV_ARGS(&paired->root)));
    auto family=std::make_shared<families::Family>();family->id=7;family->sourceRoot=root;family->pairedRoot=paired;family->uses.push_back({2,1,0,1,1,112});
    auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(uintptr_t(0x123400));ID3D12CommandList* submitted=list;
    constants::SetReader(Read);CyberpunkVR_ConstantCaptureEnable=1;
    auto begin=[&]{readable=true;torn=false;memory[0].fill(10);memory[1].fill(20);descriptors::BeginCapture();constants::Begin();descriptors::Root(list,root.Get());};
    auto capture=[&](UINT sequence,UINT side=1,UINT frame=1){
        auto destination=cpu;destination.ptr+=(side?0:1)*stride;
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{side?0x10000ull:0x10100ull,256};descriptors::Cbv(&cbv,destination);
        auto table=gpu;table.ptr+=(side?0:1)*stride;descriptors::Table(list,0,table);
        native_probe::DrawRecord d{};d.sequence=sequence;d.list=reinterpret_cast<uintptr_t>(list);d.root=reinterpret_cast<uintptr_t>(root.Get());d.side=side;d.frame=frame;
        constants::Snapshot(d,family);
    };
    auto finish=[&]{constants::End();constants::BeforeSubmit(1,&submitted);};
    auto first=[]()->const constants::Binding&{return reinterpret_cast<const constants::Binding*>(CyberpunkVR_ConstantBindings)[0];};
    auto draw=[]()->const constants::Draw&{return reinterpret_cast<const constants::Draw*>(CyberpunkVR_ConstantDraws)[0];};
    constants::PairInput input;
    begin();capture(1);capture(2,0);capture(3);Require(CyberpunkVR_ConstantPayloadCount==3,"Payload deduplication failed");finish();
    Require(draw().flags==7 && first().status==1 && !CyberpunkVR_ConstantPendingLists,"Unchanged submission rejected");
    CyberpunkVR_ConstantBankSequences[0]=1;CyberpunkVR_ConstantBankSequences[1]=2;
    CyberpunkVR_ConstantBankSequences[2]=3;CyberpunkVR_ConstantBankSequences[3]=2;
    CyberpunkVR_ConstantBankPairCount=2;CyberpunkVR_ConstantBankRequest=7;constants::Poll();
    Require(CyberpunkVR_ConstantBankDone==7 && SUCCEEDED(CyberpunkVR_ConstantBankResult) &&
        CyberpunkVR_ConstantBankDraws==2 && CyberpunkVR_ConstantBankBytes==512,"Multi-draw constant bank did not share identical data");
    CyberpunkVR_ConstantBankPairCount=constants::BankPairCapacity+1;CyberpunkVR_ConstantBankRequest=8;constants::Poll();
    Require(CyberpunkVR_ConstantBankDone==8 && FAILED(CyberpunkVR_ConstantBankResult) && !CyberpunkVR_ConstantBankDraws,"Oversized bank request accepted");
    CyberpunkVR_ConstantBankPairCount=0;
    Require(reinterpret_cast<const constants::Payload*>(CyberpunkVR_ConstantPayloads)[first().payload].bytes==112 && first().bytes==256,"CBV alignment padding incorrectly required from native allocation");
    memory[0].fill(99);memory[1].fill(98);
    Require(constants::CopyConstantPair(1,2,input) && input.pairs.size()==1,"Valid pair packaging failed");
    Require(input.pairs[0].eyes[0].constants[0]==10 && input.pairs[0].eyes[1].constants[0]==20,"Pair retained borrowed native data");
    StereoBindingBank bank;Check(bank.Create(device.Get(),paired->root.Get(),paired->plan,input.pairs));Require(bank.ConstantBytes()==512,"Owned bank shape");
    Require(!constants::CopyConstantPair(2,1,input) && !constants::CopyConstantPair(1,3,input),"Eye identity ignored");
    begin();capture(1);memory[0][3]++;finish();Require(first().status==32 && draw().flags!=7,"Changed bytes accepted");
    begin();capture(1);memory[0][200]++;finish();Require(first().status==1 && draw().flags==7,"Unused alignment padding treated as shader data");
    begin();capture(1);D3D12_CONSTANT_BUFFER_VIEW_DESC changed{0x10100,256};descriptors::Cbv(&changed,cpu);finish();Require(first().status==16,"Changed descriptor accepted");
    begin();torn=true;capture(1);finish();Require(first().status==8,"Torn CPU reads accepted");
    begin();readable=false;capture(1);finish();Require(first().status==8,"Unreadable CPU data accepted");
    begin();family->uses[0].bytes=257;capture(1);finish();Require(first().status==4,"Undersized CBV accepted");family->uses[0].bytes=112;
    begin();capture(1);constants::Reset(list);finish();Require(first().status==64 && draw().flags==0,"Abandoned list accepted");
    begin();capture(1);capture(2,0,2);finish();Require(!constants::CopyConstantPair(1,2,input),"Different frames accepted");
    begin();capture(1);capture(2,0);finish();family->uses.push_back({0,5,0,0,1,0});Require(!constants::CopyConstantPair(1,2,input),"Missing SRV ownership accepted");family->uses.pop_back();
    begin();capture(1);constants::PairInput old;Require(!constants::CopyConstantPair(1,2,old),"Missing peer accepted");
    descriptors::ClearBindings(list);native_probe::DrawRecord missing{};missing.sequence=2;missing.list=reinterpret_cast<uintptr_t>(list);missing.root=reinterpret_cast<uintptr_t>(root.Get());constants::Snapshot(missing,family);finish();
    Require(reinterpret_cast<const constants::Binding*>(CyberpunkVR_ConstantBindings)[1].status==2,"Unbound table accepted");
    CyberpunkVR_ConstantCaptureEnable=0;constants::Begin();capture(1);Require(!CyberpunkVR_ConstantDrawCount,"Disabled collector recorded input");
    CyberpunkVR_ConstantCaptureEnable=1;
    ComPtr<ID3D12Resource> source,destination;
    Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&source)));
    properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&destination)));
    constants::RegisteredResourceCreated(destination.Get(),false);
    std::array<uint8_t,112> expectedData;expectedData.fill(0x4b);
    Check(source->Map(0,nullptr,&mapped));std::memcpy(mapped,expectedData.data(),expectedData.size());source->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)));
    ComPtr<ID3D12CommandQueue> queue,otherQueue;D3D12_COMMAND_QUEUE_DESC queueDesc{};
    Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)));
    Check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&otherQueue)));
    auto recordCopy=[&] {
        constants::RegisteredUploadScope scope(destination.Get(),112,expectedData.data());
        constants::ObserveRegisteredCopy(commands.Get(),destination.Get(),0,source.Get(),0,112);
        commands->CopyBufferRegion(destination.Get(),0,source.Get(),0,112);
    };
    std::array<uint8_t,112> actual{};const auto destinationAddress=destination->GetGPUVirtualAddress();
    recordCopy();Require(constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()) && actual==expectedData,"Observed DEFAULT upload not owned");
    D3D12_RESOURCE_BARRIER alias{};alias.Type=D3D12_RESOURCE_BARRIER_TYPE_ALIASING;constants::RegisteredBarriers(commands.Get(),1,&alias);
    Require(constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()),"Unrelated alias barrier erased committed-buffer contents");
    Require(!constants::ReadRegistered(nullptr,destinationAddress,actual.data(),actual.size()),"Unsubmitted other-list upload accepted");
    Check(source->Map(0,nullptr,&mapped));static_cast<uint8_t*>(mapped)[0]=0x7f;source->Unmap(0,nullptr);
    ID3D12CommandList* copyList=commands.Get();constants::SubmitRegistered(queue.Get(),1,&copyList);
    Require(CyberpunkVR_RegisteredCopyChanged==1 && !constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()),"Changed GPU copy source accepted");
    Check(commands->Close());Check(commands->Reset(allocator.Get(),nullptr));cvr::gpu::ResetCommandResources(commands.Get());
    Check(source->Map(0,nullptr,&mapped));std::memcpy(mapped,expectedData.data(),expectedData.size());source->Unmap(0,nullptr);
    recordCopy();constants::SubmitRegistered(queue.Get(),1,&copyList);
    uint64_t receipt{};
    Require(constants::ReadRegistered(nullptr,destinationAddress,actual.data(),actual.size(),&receipt) && actual==expectedData,"Submitted copy payload rejected");
    constants::ObserveRegisteredCopy(commands.Get(),destination.Get(),4,source.Get(),0,4);
    Require(!constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()),"Unknown partial overwrite accepted");
    Require(constants::ValidateRegistered(receipt,commands.Get(),destinationAddress,expectedData.data(),expectedData.size()),"Later recorded overwrite invalidated an earlier draw's copy receipt");
    recordCopy();constants::ResetRegistered(commands.Get());Require(!constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()),"Abandoned registered upload accepted");
    recordCopy();constants::InvalidateRegistered(commands.Get(),destination.Get());Require(!constants::ReadRegistered(commands.Get(),destinationAddress,actual.data(),actual.size()),"Discard/alias invalidation failed");
    const auto unscopedBefore=CyberpunkVR_RegisteredCopyUnscoped.load();
    constants::ObserveRegisteredCopy(commands.Get(),destination.Get(),256,source.Get(),0,112);
    commands->CopyBufferRegion(destination.Get(),256,source.Get(),0,112);
    Require(CyberpunkVR_RegisteredCopyUnscoped==unscopedBefore+1 &&
        constants::ReadRegistered(commands.Get(),destinationAddress+256,actual.data(),actual.size()) && actual==expectedData,
        "Actual suballocated upload outside the native BufUpload wrapper was lost");
    Require(!constants::ReadRegistered(commands.Get(),destinationAddress+255,actual.data(),actual.size()) &&
        !constants::ReadRegistered(commands.Get(),destinationAddress+257,actual.data(),actual.size()),"Copy range was expanded");
    recordCopy();
    auto* consumer=reinterpret_cast<ID3D12CommandList*>(0x9000);
    uint64_t pendingReceipt{};
    Require(constants::ReadRegistered(consumer,destinationAddress,actual.data(),actual.size(),&pendingReceipt) && pendingReceipt &&
        !constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Pending producer was treated as submitted");
    constants::SubmitRegistered(queue.Get(),1,&consumer);
    constants::SubmitRegistered(queue.Get(),1,&copyList);
    Require(!constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Producer submitted after consumer was accepted");
    constants::SubmitRegistered(otherQueue.Get(),1,&consumer);
    Require(!constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Unproven cross-queue dependency accepted");
    constants::SubmitRegistered(queue.Get(),1,&consumer);
    Require(constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Ordered later consumer lost its recorded version");
    recordCopy();ID3D12CommandList* reverseBatch[]={consumer,copyList};constants::SubmitRegistered(queue.Get(),2,reverseBatch);
    Require(constants::ReadRegistered(consumer,destinationAddress,actual.data(),actual.size(),&pendingReceipt) &&
        !constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Reverse order inside a batch accepted");
    recordCopy();ID3D12CommandList* orderedBatch[]={copyList,consumer};constants::SubmitRegistered(queue.Get(),2,orderedBatch);
    Require(constants::ReadRegistered(consumer,destinationAddress,actual.data(),actual.size(),&pendingReceipt) &&
        constants::ValidateRegistered(pendingReceipt,consumer,destinationAddress,actual.data(),actual.size()),"Ordered batch rejected");
    ComPtr<ID3D12Resource> readback;properties.Type=D3D12_HEAP_TYPE_READBACK;
    Check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&uploadDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={destination.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
    commands->CopyBufferRegion(readback.Get(),0,destination.Get(),0,112);
    commands->CopyBufferRegion(readback.Get(),256,destination.Get(),256,112);Check(commands->Close());
    auto flight=cvr::gpu::PrepareCommandResources(1,&copyList);queue->ExecuteCommandLists(1,&copyList);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    ComPtr<ID3D12Fence> fence;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));Check(queue->Signal(fence.Get(),1));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Fence event");Check(fence->SetEventOnCompletion(1,event));auto waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"GPU upload test timeout");
    Check(readback->Map(0,nullptr,&mapped));Require(std::memcmp(mapped,expectedData.data(),112)==0 &&
        std::memcmp(static_cast<uint8_t*>(mapped)+256,expectedData.data(),112)==0,"CPU-owned registered data differs from actual GPU copy");readback->Unmap(0,nullptr);
    cvr::gpu::ResetCommandResources(commands.Get());cvr::gpu::CollectCommandResources();constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=0;
    constants::Poll();CyberpunkVR_ConstantCaptureEnable=1;constants::Poll();
    constants::SetReader(constants::ReadRegistered,constants::ValidateRegistered,constants::MarkRegisteredRead,constants::ResolveRegisteredRead);
    Check(commands->Reset(allocator.Get(),nullptr));constants::ResetRegistered(commands.Get());
    Check(source->Map(0,nullptr,&mapped));std::memset(mapped,0x21,112);std::memset(static_cast<uint8_t*>(mapped)+256,0x43,112);source->Unmap(0,nullptr);
    constants::Begin();descriptors::Root(commands.Get(),root.Get());
    D3D12_CONSTANT_BUFFER_VIEW_DESC realCbv{destinationAddress,256};descriptors::Cbv(&realCbv,cpu);descriptors::Table(commands.Get(),0,gpu);
    auto transition=[&](D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
        barrier.Transition={destination.Get(),0,before,after};commands->ResourceBarrier(1,&barrier);
    };
    std::array<uint64_t,2> qualifiedPositions{};
    for(UINT eye=0;eye<2;++eye) {
        transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        constants::ObserveRegisteredCopy(commands.Get(),destination.Get(),0,source.Get(),eye*256,112);
        commands->CopyBufferRegion(destination.Get(),0,source.Get(),eye*256,112);
        qualifiedPositions[eye]=constants::MarkRegisteredRead(commands.Get());
        native_probe::DrawRecord row{};row.sequence=10+eye;row.list=reinterpret_cast<uintptr_t>(commands.Get());
        row.root=reinterpret_cast<uintptr_t>(root.Get());row.side=1-eye;row.frame=1;constants::Snapshot(row,family);
        transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyBufferRegion(readback.Get(),eye*256,destination.Get(),0,112);
    }
    constants::End();constants::SubmitRegistered(queue.Get(),1,&copyList);constants::BeforeSubmit(1,&copyList);
    Require(CyberpunkVR_ConstantPendingLists!=0,"Deferred resolution was prematurely published as complete");
    constants::Poll();Require(!CyberpunkVR_ConstantPendingLists && constants::CopyConstantPair(10,11,input),"Command-position inputs failed to resolve");
    Require(input.pairs[0].eyes[0].constants[0]==0x21 && input.pairs[0].eyes[1].constants[0]==0x43 && first().reserved==7,
        "Latest CPU copy replaced an earlier draw's constant version");
    for(UINT eye=0;eye<2;++eye){std::array<uint8_t,112> qualified{};
        const auto result=constants::ResolveRegisteredResourceRead(qualifiedPositions[eye],commands.Get(),destination,destinationAddress,qualified.data(),qualified.size());
        Require(result.status==constants::ConstantWriteTimeline::Status::Ready && qualified[0]==(eye?0x43:0x21),"Resource-qualified position lost its original payload");
        qualified.fill(0xA5);
        Require(constants::ResolveRegisteredResourceRead(qualifiedPositions[eye],commands.Get(),source,destinationAddress,qualified.data(),qualified.size()).status==
            constants::ConstantWriteTimeline::Status::UnknownWrite && qualified[0]==0xA5,"Address-only payload accepted for another resource");
        const ComPtr<ID3D12Resource> absent;
        Require(constants::ResolveRegisteredResourceRead(qualifiedPositions[eye],commands.Get(),absent,destinationAddress,qualified.data(),qualified.size()).status==
            constants::ConstantWriteTimeline::Status::UnknownWrite && qualified[0]==0xA5,"Missing destination ownership accepted");
    }
    Check(commands->Close());flight=cvr::gpu::PrepareCommandResources(1,&copyList);
    queue->ExecuteCommandLists(1,&copyList);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));Check(queue->Signal(fence.Get(),2));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Timeline fence event");Check(fence->SetEventOnCompletion(2,event));
    waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Timeline GPU timeout");
    Check(readback->Map(0,nullptr,&mapped));
    Require(!std::memcmp(mapped,input.pairs[0].eyes[0].constants.data(),112) &&
        !std::memcmp(static_cast<uint8_t*>(mapped)+256,input.pairs[0].eyes[1].constants.data(),112),"Timeline-selected payload differs from GPU contents at its read position");
    readback->Unmap(0,nullptr);cvr::gpu::ResetCommandResources(commands.Get());cvr::gpu::CollectCommandResources();
    CyberpunkVR_ConstantCaptureEnable=0;constants::Poll();CyberpunkVR_ConstantCaptureEnable=1;constants::Poll();
    constants::ObserveQueueApi(queue.Get());constants::ObserveQueueApi(otherQueue.Get());
    Check(commands->Reset(allocator.Get(),nullptr));constants::ResetRegistered(commands.Get());
    ComPtr<ID3D12CommandAllocator> consumerAllocator;ComPtr<ID3D12GraphicsCommandList> consumerCommands;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&consumerAllocator)));
    Check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,consumerAllocator.Get(),nullptr,IID_PPV_ARGS(&consumerCommands)));
    constants::Begin();descriptors::Root(consumerCommands.Get(),root.Get());descriptors::Table(consumerCommands.Get(),0,gpu);
    for(UINT eye=0;eye<2;++eye) {
        native_probe::DrawRecord row{};row.sequence=20+eye;row.list=reinterpret_cast<uintptr_t>(consumerCommands.Get());
        row.root=reinterpret_cast<uintptr_t>(root.Get());row.side=1-eye;row.frame=2;constants::Snapshot(row,family);
    }
    Require(first().status==8,"Fixture must start before the producer is recorded");
    transition(D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
    constants::ObserveRegisteredCopy(commands.Get(),destination.Get(),0,source.Get(),256,112);
    commands->CopyBufferRegion(destination.Get(),0,source.Get(),256,112);
    transition(D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);Check(commands->Close());
    barrier.Transition={destination.Get(),0,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};consumerCommands->ResourceBarrier(1,&barrier);
    consumerCommands->CopyBufferRegion(readback.Get(),0,destination.Get(),0,112);Check(consumerCommands->Close());constants::End();
    ComPtr<ID3D12Fence> dependency;Check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&dependency)));
    constants::SubmitRegistered(otherQueue.Get(),1,&copyList);
    flight=cvr::gpu::PrepareCommandResources(1,&copyList);otherQueue->ExecuteCommandLists(1,&copyList);
    cvr::gpu::SubmitCommandResources(otherQueue.Get(),std::move(flight));Check(otherQueue->Signal(dependency.Get(),7));
    Check(queue->Wait(dependency.Get(),7));ID3D12CommandList* consumerList=consumerCommands.Get();
    constants::SubmitRegistered(queue.Get(),1,&consumerList);constants::BeforeSubmit(1,&consumerList);constants::Poll();
    Require(!CyberpunkVR_ConstantPendingLists && constants::CopyConstantPair(20,21,input) && input.pairs[0].eyes[0].constants[0]==0x43,
        "Real queue Signal/Wait failed to resolve a producer recorded after its consumer");
    queue->ExecuteCommandLists(1,&consumerList);Check(queue->Signal(fence.Get(),3));
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Cross-queue fence event");Check(fence->SetEventOnCompletion(3,event));
    waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Cross-queue GPU timeout");
    Check(readback->Map(0,nullptr,&mapped));Require(!std::memcmp(mapped,input.pairs[0].eyes[0].constants.data(),112),"Cross-queue selected bytes differ from GPU");readback->Unmap(0,nullptr);
    Require(CyberpunkVR_RegisteredCopySignals>=2 && CyberpunkVR_RegisteredCopyWaits==1,"Actual queue APIs not observed");
    Check(dependency->Signal(8));Require(CyberpunkVR_RegisteredCopyCpuSignals==1,"CPU fence signal was not invalidated");
    cvr::gpu::ResetCommandResources(commands.Get());cvr::gpu::CollectCommandResources();
    CyberpunkVR_ConstantCaptureEnable=0;constants::Poll();constants::SetReader(Read);
    Require(!CyberpunkVR_RegisteredCopyTraceOverflow && CyberpunkVR_RegisteredCopyTraceCount,"Immutable timeline journal missing");
    const auto replay=constants::ReplayConstantTrace({reinterpret_cast<const constants::TraceEvent*>(CyberpunkVR_RegisteredCopyTraceData),CyberpunkVR_RegisteredCopyTraceCount.load()});
    Require(replay.queries && !replay.mismatches,"Offline timeline replay differs from observed resolution");
    constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=1;
    Check(commands->Reset(allocator.Get(),nullptr));constants::ResetRegistered(commands.Get());
    const auto allocation=device->GetResourceAllocationInfo(0,1,&uploadDesc);
    D3D12_HEAP_DESC heapDesc{};heapDesc.SizeInBytes=allocation.SizeInBytes*2;heapDesc.Properties.Type=D3D12_HEAP_TYPE_DEFAULT;
    heapDesc.Flags=D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;ComPtr<ID3D12Heap> placedHeap;
    Check(device->CreateHeap(&heapDesc,IID_PPV_ARGS(&placedHeap)));
    ComPtr<ID3D12Resource> placedA,placedB,overlapping;
    Check(device->CreatePlacedResource(placedHeap.Get(),0,&uploadDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&placedA)));
    Check(device->CreatePlacedResource(placedHeap.Get(),allocation.SizeInBytes,&uploadDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&placedB)));
    Check(device->CreatePlacedResource(placedHeap.Get(),0,&uploadDesc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&overlapping)));
    constants::RegisteredResourceCreated(placedA.Get(),true,placedHeap.Get(),0,allocation.SizeInBytes);
    constants::RegisteredResourceCreated(placedB.Get(),true,placedHeap.Get(),allocation.SizeInBytes,allocation.SizeInBytes);
    constants::RegisteredResourceCreated(overlapping.Get(),true,placedHeap.Get(),0,allocation.SizeInBytes);placedHeap.Reset();
    alias.Aliasing={nullptr,placedA.Get()};commands->ResourceBarrier(1,&alias);
    constants::ObserveRegisteredCopy(commands.Get(),placedA.Get(),0,source.Get(),0,112);commands->CopyBufferRegion(placedA.Get(),0,source.Get(),0,112);
    alias.Aliasing={nullptr,placedB.Get()};constants::RegisteredBarriers(commands.Get(),1,&alias);commands->ResourceBarrier(1,&alias);
    Require(constants::ReadRegistered(commands.Get(),placedA->GetGPUVirtualAddress(),actual.data(),actual.size()) && actual[0]==0x21,
        "Disjoint placed-resource activation erased a live constant buffer");
    barrier.Transition={placedA.Get(),0,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE};commands->ResourceBarrier(1,&barrier);
    commands->CopyBufferRegion(readback.Get(),0,placedA.Get(),0,112);
    alias.Aliasing={nullptr,overlapping.Get()};constants::RegisteredBarriers(commands.Get(),1,&alias);commands->ResourceBarrier(1,&alias);
    Require(!constants::ReadRegistered(commands.Get(),placedA->GetGPUVirtualAddress(),actual.data(),actual.size()),"Overlapping alias retained stale constant contents");
    Check(commands->Close());constants::SubmitRegistered(queue.Get(),1,&copyList);
    flight=cvr::gpu::PrepareCommandResources(1,&copyList);queue->ExecuteCommandLists(1,&copyList);cvr::gpu::SubmitCommandResources(queue.Get(),std::move(flight));
    Check(queue->Signal(fence.Get(),4));event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Require(event!=nullptr,"Placed-heap fence event");
    Check(fence->SetEventOnCompletion(4,event));waited=WaitForSingleObject(event,5000);CloseHandle(event);Require(waited==WAIT_OBJECT_0,"Placed-heap GPU timeout");
    Check(readback->Map(0,nullptr,&mapped));Require(static_cast<uint8_t*>(mapped)[0]==0x21 && static_cast<uint8_t*>(mapped)[111]==0x21,
        "Disjoint activation changed actual GPU buffer data");readback->Unmap(0,nullptr);
    cvr::gpu::ResetCommandResources(commands.Get());cvr::gpu::CollectCommandResources();constants::ClearRegistered();CyberpunkVR_ConstantCaptureEnable=0;
    ResourceIntentTests(device.Get(),source,placedA,overlapping,queue.Get());
    constants::SetReader(Read,nullptr,[](ID3D12CommandList*) -> uint64_t{return 0;});
    CyberpunkVR_ConstantCaptureEnable=1;begin();capture(1);
    Require(CyberpunkVR_ConstantCaptureFailure && !CyberpunkVR_ConstantDrawCount,"Failed timeline silently fell back to weaker validation");
    constants::SetReader(Read);CyberpunkVR_ConstantCaptureEnable=0;constants::Begin();
    Require(!CyberpunkVR_ConstantCaptureFailure && !(CyberpunkVR_ConstantCaptureSeq.load()&1),"Bad capture publication");
    std::puts("PASS owned native CBVs, full-byte deduplication, submit revalidation, torn reads, Reset, per-eye/frame pairing and real bank construction");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
