#include "Render/SceneGpuProfile.hpp"
#include "Render/CommandResources.hpp"
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <set>

extern "C" {
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SceneGpuRequest{0},CyberpunkVR_SceneGpuReportSeq{0};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SceneGpuGroupMode{0},CyberpunkVR_SceneGpuNodeFilter{0};
__declspec(dllexport) char CyberpunkVR_SceneGpuReport[262144]="{\"active\":false}";
}
namespace cvr::gpu::scene_profile {
using Microsoft::WRL::ComPtr;
std::atomic<bool> active{false};
constexpr unsigned Capacity=256;
struct Key {
    uint32_t node{};int side{};uint64_t pso{};uint32_t detail{};
    auto Fields() const {return std::tie(node,side,pso,detail);}
    bool operator==(const Key& other) const {return Fields()==other.Fields();}
    bool operator<(const Key& other) const {return Fields()<other.Fields();}
};
struct Batch {
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12Fence> fence;
    uint64_t value{},frequency{};
    struct Range {Key key;uint32_t commands{};};
    std::vector<Range> ranges;
    bool closed{},claimed{},invalid{},rangeOpen{};
};
namespace {
std::mutex mutex;
unsigned remaining{},completed{},discarded{};
std::chrono::steady_clock::time_point deadline;
std::unordered_map<ID3D12GraphicsCommandList*,std::shared_ptr<Batch>> lists;
std::vector<std::shared_ptr<Batch>> pending;
struct Statistic {unsigned ranges{},commands{},lists{};double sumMs{},maxMs{};};
std::map<Key,Statistic> statistics;
void Publish() {
    std::ostringstream out;
    out<<"{\"active\":"<<(active.load()?"true":"false")<<",\"remaining\":"<<remaining
       <<",\"pending\":"<<pending.size()<<",\"recordings\":"<<lists.size()
       <<",\"completed\":"<<completed<<",\"discarded\":"<<discarded<<",\"groups\":[";
    bool first=true;
    for(const auto& [key,value]:statistics) {
        if(!first)out<<',';first=false;
        out<<"{\"node\":"<<key.node<<",\"side\":"<<key.side<<",\"pso\":"<<key.pso
           <<",\"ranges\":"<<value.ranges<<",\"commands\":"<<value.commands<<",\"lists\":"<<value.lists<<",\"detail\":"<<key.detail
           <<",\"sum_ms\":"<<value.sumMs<<",\"max_ms\":"<<value.maxMs<<'}';
    }
    out<<"]}";auto text=out.str();
    if(text.size()>=sizeof(CyberpunkVR_SceneGpuReport))text="{\"error\":\"report overflow\"}";
    CyberpunkVR_SceneGpuReportSeq.fetch_add(1,std::memory_order_acq_rel);
    std::memcpy(CyberpunkVR_SceneGpuReport,text.c_str(),text.size()+1);
    CyberpunkVR_SceneGpuReportSeq.fetch_add(1,std::memory_order_release);
}
std::shared_ptr<Batch> Create(ID3D12GraphicsCommandList* list) {
    const auto type=list->GetType();
    if(type!=D3D12_COMMAND_LIST_TYPE_DIRECT && type!=D3D12_COMMAND_LIST_TYPE_COMPUTE)return {};
    ComPtr<ID3D12Device> device;if(FAILED(list->GetDevice(IID_PPV_ARGS(&device))))return {};
    auto batch=std::make_shared<Batch>();batch->ranges.reserve(Capacity);
    D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=Capacity*2;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=Capacity*16;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;
    if(FAILED(device->CreateQueryHeap(&query,IID_PPV_ARGS(&batch->heap))) ||
       FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&batch->readback))) ||
       !KeepCommandResources(list,{batch->heap.Get(),batch->readback.Get()}))return {};
    return batch;
}
}
void Mark(ID3D12GraphicsCommandList* list,uint32_t node,int side,ID3D12PipelineState* pso,uint32_t detail) {
    if(!active.load(std::memory_order_relaxed) || !list)return;
    const auto filter=CyberpunkVR_SceneGpuNodeFilter.load(std::memory_order_relaxed);
    if(filter && filter!=node){EndRange(list);return;}
    std::lock_guard lock(mutex);
    auto it=lists.find(list);
    if(it==lists.end()) {
        if(!remaining || !node || side<0 || side>2)return;
        --remaining;auto batch=Create(list);if(!batch){++discarded;return;}
        it=lists.emplace(list,std::move(batch)).first;
    }
    auto& batch=*it->second;
    if(batch.closed || batch.invalid)return;
    Key key{node,side,CyberpunkVR_SceneGpuGroupMode.load(std::memory_order_relaxed)?0:reinterpret_cast<uint64_t>(pso),detail};
    if(batch.rangeOpen && batch.ranges.back().key==key){++batch.ranges.back().commands;return;}
    if(batch.rangeOpen)list->EndQuery(batch.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,unsigned(batch.ranges.size()*2-1));
    if(batch.ranges.size()==Capacity){batch.invalid=true;return;}
    list->EndQuery(batch.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,unsigned(batch.ranges.size()*2));
    batch.ranges.push_back({key,1});
    batch.rangeOpen=true;
}
void EndRange(ID3D12GraphicsCommandList* list) {
    if(!active.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);const auto it=lists.find(list);if(it==lists.end())return;auto& batch=*it->second;
    if(batch.closed || batch.invalid || !batch.rangeOpen)return;
    list->EndQuery(batch.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,unsigned(batch.ranges.size()*2-1));batch.rangeOpen=false;
}
void Close(ID3D12GraphicsCommandList* list) {
    if(!active.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);const auto it=lists.find(list);if(it==lists.end())return;
    auto& batch=*it->second;if(batch.closed || batch.invalid || batch.ranges.empty())return;
    if(batch.rangeOpen)list->EndQuery(batch.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,unsigned(batch.ranges.size()*2-1));
    batch.rangeOpen=false;
    list->ResolveQueryData(batch.heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,unsigned(batch.ranges.size()*2),batch.readback.Get(),0);
    batch.closed=true;
}
void Reset(ID3D12GraphicsCommandList* list) {
    if(!active.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);auto it=lists.find(list);if(it==lists.end())return;
    if(!it->second->claimed)++discarded;
    lists.erase(it);
}
Submission BeforeSubmit(UINT count,ID3D12CommandList* const* submitted) {
    Submission result;if(!active.load(std::memory_order_relaxed))return result;
    std::lock_guard lock(mutex);
    for(UINT i=0;i<count;++i) {
        auto it=lists.find(static_cast<ID3D12GraphicsCommandList*>(submitted[i]));if(it==lists.end())continue;
        auto& batch=*it->second;
        if(batch.claimed){batch.invalid=true;continue;}
        if(!batch.closed || batch.invalid){++discarded;lists.erase(it);continue;}
        batch.claimed=true;result.push_back(it->second);
    }
    return result;
}
void Submitted(ID3D12CommandQueue* queue,Submission values) {
    if(values.empty())return;
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;uint64_t frequency{};
    const bool ok=SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&device))) &&
        SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))) &&
        SUCCEEDED(queue->GetTimestampFrequency(&frequency)) && frequency && SUCCEEDED(queue->Signal(fence.Get(),1));
    std::lock_guard lock(mutex);
    for(auto& batch:values) {
        batch->frequency=frequency;batch->fence=fence;batch->value=1;
        if(!ok){batch->invalid=true;batch->fence.Reset();}
        pending.push_back(std::move(batch));
    }
}
void Poll() {
    if(!active.load(std::memory_order_relaxed) && !CyberpunkVR_SceneGpuRequest.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);
    if(!active.load() && pending.empty()) {
        remaining=std::min(CyberpunkVR_SceneGpuRequest.exchange(0),256u);
        if(!remaining)return;
        completed=discarded=0;statistics.clear();lists.clear();active=true;
        deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    }
    if(std::chrono::steady_clock::now()>deadline) {
        remaining=0;
        for(auto it=lists.begin();it!=lists.end();) {
            if(!it->second->claimed){++discarded;it=lists.erase(it);}else ++it;
        }
    }
    for(auto it=pending.begin();it!=pending.end();) {
        auto& batch=**it;
        if(batch.fence && batch.fence->GetCompletedValue()<batch.value){++it;continue;}
        if(!batch.invalid && batch.frequency) {
            void* data{};D3D12_RANGE range{0,batch.ranges.size()*16};
            if(SUCCEEDED(batch.readback->Map(0,&range,&data))) {
                const auto* stamps=static_cast<const uint64_t*>(data);
                for(size_t i=0;i<batch.ranges.size();++i) {
                    if(stamps[2*i+1]<stamps[2*i]){batch.invalid=true;break;}
                }
                std::set<Key> seen;
                if(!batch.invalid)for(size_t i=0;i<batch.ranges.size();++i) {
                    auto& stat=statistics[batch.ranges[i].key];++stat.ranges;stat.commands+=batch.ranges[i].commands;
                    if(seen.insert(batch.ranges[i].key).second)++stat.lists;
                    const auto ms=double(stamps[2*i+1]-stamps[2*i])*1000.0/double(batch.frequency);
                    stat.sumMs+=ms;stat.maxMs=std::max(stat.maxMs,ms);
                }
                D3D12_RANGE none{};batch.readback->Unmap(0,&none);
            } else batch.invalid=true;
        }
        if(batch.invalid)++discarded;else ++completed;
        for(auto mapIt=lists.begin();mapIt!=lists.end();) {
            if(mapIt->second==*it)mapIt=lists.erase(mapIt);else ++mapIt;
        }
        it=pending.erase(it);
    }
    if(!remaining && pending.empty() && lists.empty())active=false;
    Publish();
}
}
