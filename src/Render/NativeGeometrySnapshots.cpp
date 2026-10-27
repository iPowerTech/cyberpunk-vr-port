#include "Render/NativeGeometrySnapshots.hpp"
#include "Render/RenderProbeScope.hpp"
#include <map>
#include <mutex>
#include <algorithm>
#include <stdexcept>

extern "C" {
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_GeometryFreezeEnable{},CyberpunkVR_GeometryFreezeSeq{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_GeometryFreezeCount{},CyberpunkVR_GeometryFreezePending{},CyberpunkVR_GeometryFreezeFailure{};
__declspec(dllexport) uint64_t CyberpunkVR_GeometryFreezeRecords{},CyberpunkVR_GeometryFreezeBytes{};
}
namespace cvr::stereo::geometry_snapshots {
using Microsoft::WRL::ComPtr;
struct Entry {
    GeometrySnapshot snapshot;ID3D12CommandList* list{};UINT index{};
    ComPtr<ID3D12Fence> fence;UINT64 value{},generation{};bool submitting{},sealed{};
};
namespace {
std::mutex mutex;bool active{};std::vector<Record> records;std::map<UINT,std::shared_ptr<Entry>> entries;
struct Queue {ComPtr<ID3D12CommandQueue> owner;ComPtr<ID3D12Fence> fence;UINT64 value{};};std::map<ID3D12CommandQueue*,Queue> queues;
UINT64 generation{};
UINT64 charged{};constexpr UINT64 Limit=256*1024*1024;
void Publish() {
    CyberpunkVR_GeometryFreezeRecords=reinterpret_cast<uintptr_t>(records.data());CyberpunkVR_GeometryFreezeCount=UINT(records.size());
    CyberpunkVR_GeometryFreezePending=UINT(std::count_if(records.begin(),records.end(),[](const auto& r){return r.status==1 || r.status==2;}));
}
}
void Begin(bool enabled) {
    std::lock_guard lock(mutex);CyberpunkVR_GeometryFreezeSeq.fetch_add(1);active=enabled;
    ++generation;records.clear();entries.clear();queues.clear();charged=0;CyberpunkVR_GeometryFreezeBytes=0;CyberpunkVR_GeometryFreezeFailure=0;
    Publish();CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
}
void Capture(const geometry::DrawResources& source,const std::array<geometry::ReadState,17>& states,bool rebase) {
    if(!CyberpunkVR_GeometryFreezeEnable.load(std::memory_order_relaxed) || probe::internalCommands)return;
    std::lock_guard lock(mutex);if(!active || CyberpunkVR_GeometryFreezeFailure.load())return;
    CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
    try {
        if(records.size()>=2048 || entries.contains(UINT(source.draw.sequence)))throw std::runtime_error("geometry snapshot capacity/identity");
        auto entry=std::make_shared<Entry>();entry->list=reinterpret_cast<ID3D12CommandList*>(source.draw.list);entry->index=UINT(records.size());
        entry->generation=generation;
        // Reserve a worst-case pair of allocations before recording commands.
        UINT64 logical=source.draw.index.SizeInBytes;
        for(UINT slot=0;slot<16;++slot)if(source.draw.inputSlotMask&(1u<<slot))logical+=slot==7?UINT64(source.draw.instances)*source.draw.vertices[7].StrideInBytes:source.draw.vertices[slot].SizeInBytes;
        const UINT64 charge=((logical+17*16+65535)&~UINT64(65535))*2;
        const auto hr=charge>Limit-charged?E_OUTOFMEMORY:entry->snapshot.Capture(reinterpret_cast<ID3D12GraphicsCommandList*>(source.draw.list),source,states,rebase);
        Record row{UINT(source.draw.sequence),SUCCEEDED(hr)?1u:4u,hr,0,entry->snapshot.Bytes()};
        if(SUCCEEDED(hr)){charged+=charge;CyberpunkVR_GeometryFreezeBytes+=row.bytes;entries.emplace(row.sequence,std::move(entry));}
        records.push_back(row);
    }catch(...){CyberpunkVR_GeometryFreezeFailure=1;}
    Publish();CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
}
Submission BeforeSubmit(UINT count,ID3D12CommandList* const* lists) {
    Submission result;if(!lists || !CyberpunkVR_GeometryFreezePending.load(std::memory_order_acquire))return result;
    std::lock_guard lock(mutex);CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
    try {
        for(auto& [_,entry]:entries) {
            auto& row=records[entry->index];if((row.status!=1 && row.status!=2) || entry->sealed)continue;
            UINT occurrences{};for(UINT i=0;i<count;++i)occurrences+=lists[i]==entry->list;if(!occurrences)continue;
            if(!entry->snapshot.OnRecording()){if(row.status==1 && !entry->submitting){row.status=6;row.result=E_UNEXPECTED;}continue;}
            if(row.status==2 || entry->submitting || occurrences!=1){row.status=5;row.result=E_UNEXPECTED;continue;}
            entry->submitting=true;result.entries.push_back(entry);
        }
    }catch(...){CyberpunkVR_GeometryFreezeFailure=2;}
    Publish();CyberpunkVR_GeometryFreezeSeq.fetch_add(1);return result;
}
void Submitted(ID3D12CommandQueue* queue,Submission submission) {
    if(submission.entries.empty())return;
    std::lock_guard lock(mutex);CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
    HRESULT hr=E_FAIL;
    try {
        std::erase_if(submission.entries,[](const auto& entry){return entry->generation!=generation;});
        if(submission.entries.empty()){CyberpunkVR_GeometryFreezeSeq.fetch_add(1);return;}
        auto& state=queues[queue];
        if(!state.fence){ComPtr<ID3D12Device> device;hr=queue->GetDevice(IID_PPV_ARGS(&device));if(SUCCEEDED(hr))hr=device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&state.fence));if(SUCCEEDED(hr))state.owner=queue;}
        if(state.fence)hr=queue->Signal(state.fence.Get(),state.value+1);
        if(SUCCEEDED(hr))++state.value;
        for(const auto& entry:submission.entries){entry->submitting=false;auto& row=records[entry->index];if(row.status==5)continue;
            if(FAILED(hr)){row.status=7;row.result=hr;}else{row.status=2;entry->fence=state.fence;entry->value=state.value;}}
    }catch(...){CyberpunkVR_GeometryFreezeFailure=3;}
    Publish();CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
}
void Poll() {
    if(!CyberpunkVR_GeometryFreezePending.load(std::memory_order_acquire))return;
    std::lock_guard lock(mutex);CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
    try {
        for(const auto& [_,entry]:entries) {
            auto& row=records[entry->index];
            if(row.status==1 && !entry->submitting && !entry->snapshot.OnRecording()){row.status=6;row.result=E_UNEXPECTED;}
            if(row.status!=2 || entry->submitting)continue;
            if(!entry->sealed)entry->sealed=entry->snapshot.Seal(entry->fence.Get(),entry->value);
            if(!entry->sealed)continue;
            const auto hr=entry->snapshot.Read();if(hr==S_FALSE)continue;row.result=hr;row.status=SUCCEEDED(hr)?3:7;
        }
    }catch(...){CyberpunkVR_GeometryFreezeFailure=4;}
    Publish();CyberpunkVR_GeometryFreezeSeq.fetch_add(1);
}
bool CopyPair(UINT left,UINT right,GeometryCommandPair& output) {
    std::lock_guard lock(mutex);if(CyberpunkVR_GeometryFreezeFailure.load())return false;
    const auto a=entries.find(left),b=entries.find(right);if(a==entries.end() || b==entries.end() || records[a->second->index].status!=3 || records[b->second->index].status!=3)return false;
    return GeometrySnapshot::Pair(a->second->snapshot,b->second->snapshot,output);
}
}
