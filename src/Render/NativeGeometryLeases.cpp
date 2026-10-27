#include "Render/NativeGeometryLeases.hpp"
#include "Render/CommandResources.hpp"
#include "Render/RenderProbeScope.hpp"
#include "Render/NativeGeometrySnapshots.hpp"
#include <map>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <algorithm>

extern "C" {
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_GeometryLeaseSeq{},CyberpunkVR_GeometryLeaseCompatible{},CyberpunkVR_GeometryLeaseFailure{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_GeometryLeaseDrawCount{},CyberpunkVR_GeometryLeaseResourceCount{};
__declspec(dllexport) uint64_t CyberpunkVR_GeometryLeaseDraws{},CyberpunkVR_GeometryLeaseResources{};
__declspec(dllexport) uint64_t CyberpunkVR_GeometryLeaseReadStates{};
}
namespace cvr::stereo::geometry {
namespace {
struct State {
    BufferLease index;D3D12_INDEX_BUFFER_VIEW indexView{};
    std::array<BufferLease,16> vertices;std::array<D3D12_VERTEX_BUFFER_VIEW,16> views{};
};
std::mutex mutex;std::atomic<bool> active{},snapshotting{};
std::map<ID3D12GraphicsCommandList*,State> states;
std::vector<DrawLeases> draws;
std::vector<ResourceRecord> resources;
std::vector<BufferLease> owners;
std::vector<native_probe::DrawRecord> nativeDraws;
std::vector<std::vector<uint8_t>> instanceData;
std::vector<scene_state::DrawState> drawStates;
std::vector<DrawReadStates> readStates;
size_t instanceBytes{};
std::map<ID3D12Resource*,UINT> ids;
bool Matches(const BufferLease& lease,uint64_t address,uint64_t bytes) {
    return lease.resource && bytes && address>=lease.gpu && address-lease.gpu<=lease.bytes && bytes<=lease.bytes-(address-lease.gpu);
}
UINT Own(const BufferLease& lease) {
    if(!lease.resource)return 0;
    if(const auto found=ids.find(lease.resource.Get());found!=ids.end())return found->second;
    if(resources.size()>=8192)throw std::length_error("geometry resource lease limit");
    const auto id=UINT(resources.size());owners.push_back(lease);
    resources.push_back({reinterpret_cast<uintptr_t>(lease.resource.Get()),lease.gpu,lease.bytes,lease.heapType,lease.flags});
    ids.emplace(lease.resource.Get(),id);return id;
}
void Publish() {
    CyberpunkVR_GeometryLeaseDraws=reinterpret_cast<uintptr_t>(draws.data());CyberpunkVR_GeometryLeaseDrawCount=UINT(draws.size());
    CyberpunkVR_GeometryLeaseResources=reinterpret_cast<uintptr_t>(resources.data());CyberpunkVR_GeometryLeaseResourceCount=UINT(resources.size());
    CyberpunkVR_GeometryLeaseReadStates=reinterpret_cast<uintptr_t>(readStates.data());
}
}
void Begin(bool enabled) {
    std::lock_guard lock(mutex);CyberpunkVR_GeometryLeaseSeq.fetch_add(1);
    active=enabled;snapshotting=enabled;states.clear();draws.clear();resources.assign(1,{});owners.assign(1,{});ids.clear();CyberpunkVR_GeometryLeaseFailure=0;
    geometry_snapshots::Begin(enabled && CyberpunkVR_GeometryFreezeEnable.load()!=0);
    nativeDraws.clear();instanceData.clear();drawStates.clear();readStates.clear();instanceBytes=0;
    if(enabled)CyberpunkVR_GeometryLeaseCompatible=NativeIaCompatible()?1:2;
    Publish();CyberpunkVR_GeometryLeaseSeq.fetch_add(1);
}
void BeginBindings(){std::lock_guard lock(mutex);snapshotting=false;active=true;states.clear();}
void EndBindings(){std::lock_guard lock(mutex);if(!snapshotting.load()){active=false;states.clear();}}
void End(){active=false;snapshotting=false;ClearBindings();}
void ClearBindings(){std::lock_guard lock(mutex);states.clear();}
void Reset(ID3D12GraphicsCommandList* list){if(!active.load())return;std::lock_guard lock(mutex);states.erase(list);}
void Index(ID3D12GraphicsCommandList* list,const D3D12_INDEX_BUFFER_VIEW* view) {
    if(!active.load(std::memory_order_relaxed) || probe::internalCommands || !list)return;
    IndexLease(list,view,view?BoundIndex(*view):BufferLease{});
}
void IndexLease(ID3D12GraphicsCommandList* list,const D3D12_INDEX_BUFFER_VIEW* view,BufferLease lease) {
    if(!active.load(std::memory_order_relaxed) || probe::internalCommands || !list)return;
    try {
        std::lock_guard lock(mutex);
        if(!active.load(std::memory_order_relaxed))return;
        if(states.size()>=512 && !states.contains(list))throw std::length_error("geometry state limit");
        auto& state=states[list];state.index=std::move(lease);state.indexView=view?*view:D3D12_INDEX_BUFFER_VIEW{};
    }catch(...){CyberpunkVR_GeometryLeaseFailure=1;}
}
void Vertices(ID3D12GraphicsCommandList* list,UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW* views) {
    if(!active.load(std::memory_order_relaxed) || probe::internalCommands || !list)return;
    if(start>32 || count>32-start){CyberpunkVR_GeometryLeaseFailure=2;return;}
    if(start>=16)return;
    VertexLeases(list,start,count,views,BoundVertices(start,count,views));
}
void VertexLeases(ID3D12GraphicsCommandList* list,UINT start,UINT count,const D3D12_VERTEX_BUFFER_VIEW* views,std::array<BufferLease,16> leases) {
    if(!active.load(std::memory_order_relaxed) || probe::internalCommands || !list)return;
    if(start>32 || count>32-start){CyberpunkVR_GeometryLeaseFailure=2;return;}
    if(start>=16)return;
    count=std::min(count,16-start);
    try {
        std::lock_guard lock(mutex);
        if(!active.load(std::memory_order_relaxed))return;
        if(states.size()>=512 && !states.contains(list))throw std::length_error("geometry state limit");
        auto& state=states[list];for(UINT i=0;i<count;++i) {
            state.vertices[start+i]=std::move(leases[i]);state.views[start+i]=views?views[i]:D3D12_VERTEX_BUFFER_VIEW{};
        }
    }catch(...){CyberpunkVR_GeometryLeaseFailure=1;}
}
void Snapshot(const native_probe::DrawRecord& draw,std::span<const uint8_t> instances,const scene_state::DrawState* drawState,bool freeze) {
    if(!snapshotting.load(std::memory_order_relaxed))return;
    std::lock_guard lock(mutex);if(!snapshotting.load(std::memory_order_relaxed))return;
    CyberpunkVR_GeometryLeaseSeq.fetch_add(1);
    try {
        if(draws.size()>=native_probe::DrawCapacity)throw std::length_error("geometry draw limit");
        DrawLeases row;row.sequence=UINT(draw.sequence);row.usedMask=draw.inputSlotMask;row.frame=draw.frame;row.side=draw.side;
        DrawReadStates statesAtDraw;statesAtDraw.sequence=row.sequence;
        const auto found=states.find(reinterpret_cast<ID3D12GraphicsCommandList*>(draw.list));
        if(found!=states.end()) {
            const auto& state=found->second;
            auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(draw.list);
            statesAtDraw.buffers[0]=ReadStateAtDraw(list,state.index);
            for(UINT slot=0;slot<16;++slot)if(row.usedMask&(1u<<slot))statesAtDraw.buffers[slot+1]=ReadStateAtDraw(list,state.vertices[slot]);
            const auto indexSize=draw.index.Format==DXGI_FORMAT_R16_UINT?2u:draw.index.Format==DXGI_FORMAT_R32_UINT?4u:0u;
            if(indexSize && draw.indices && uint64_t(draw.firstIndex)*indexSize<=draw.index.SizeInBytes &&
               uint64_t(draw.indices)*indexSize<=draw.index.SizeInBytes-uint64_t(draw.firstIndex)*indexSize &&
               !std::memcmp(&draw.index,&state.indexView,sizeof(draw.index)) && Matches(state.index,draw.index.BufferLocation,draw.index.SizeInBytes)) {
                row.index=Own(state.index);row.flags|=1;
            }
            if((draw.flags&128) && !(draw.inputSlotMask&0xFFFF0000u))row.flags|=2;
            for(UINT slot=0;slot<16;++slot)if(draw.inputSlotMask&(1u<<slot)) {
                const auto& view=draw.vertices[slot];
                if(!std::memcmp(&view,&state.views[slot],sizeof(view)) && Matches(state.vertices[slot],view.BufferLocation,view.SizeInBytes)) {
                    row.vertices[slot]=Own(state.vertices[slot]);row.ownedMask|=1u<<slot;
                }
            }
            if((row.flags&2) && row.ownedMask==row.usedMask)row.flags|=4;
            if(row.flags==7) {
                bool kept=gpu::KeepCommandResources(reinterpret_cast<ID3D12GraphicsCommandList*>(draw.list),{state.index.resource.Get()});
                for(UINT slot=0;slot<16;++slot)if(row.usedMask&(1u<<slot))kept&=gpu::KeepCommandResources(reinterpret_cast<ID3D12GraphicsCommandList*>(draw.list),{state.vertices[slot].resource.Get()});
                if(kept)row.flags|=8;
            }
        }
        if(instances.size()>16*1024 || instanceBytes+instances.size()>32*1024*1024)throw std::length_error("geometry instance snapshot limit");
        if(drawState && reinterpret_cast<uintptr_t>(drawState->original.Get())==draw.pipeline && drawState->topology && drawState->rate.known)row.flags|=16;
        if(freeze && row.flags==31 && CyberpunkVR_GeometryFreezeEnable.load(std::memory_order_relaxed)) {
            DrawResources source;source.index=owners[row.index];source.draw=draw;source.state=*drawState;
            for(UINT slot=0;slot<16;++slot)if(row.usedMask&(1u<<slot))source.vertices[slot]=owners[row.vertices[slot]];
            geometry_snapshots::Capture(source,statesAtDraw.buffers,true);
        }
        drawStates.push_back((row.flags&16)?*drawState:scene_state::DrawState{});
        readStates.push_back(statesAtDraw);
        instanceData.emplace_back(instances.begin(),instances.end());instanceBytes+=instances.size();nativeDraws.push_back(draw);draws.push_back(row);
    }catch(...){CyberpunkVR_GeometryLeaseFailure=1;}
    Publish();CyberpunkVR_GeometryLeaseSeq.fetch_add(1);
}
bool CopyDraw(uint32_t sequence,DrawResources& output) {
    std::lock_guard lock(mutex);if(CyberpunkVR_GeometryLeaseFailure.load())return false;
    const auto found=std::find_if(draws.begin(),draws.end(),[&](const auto& row){return row.sequence==sequence && (row.flags&15)==15;});
    if(found==draws.end() || !found->index || found->index>=owners.size())return false;
    DrawResources result;result.index=owners[found->index];
    const auto at=size_t(found-draws.begin());result.draw=nativeDraws[at];result.instances=instanceData[at];result.state=drawStates[at];
    for(UINT slot=0;slot<16;++slot)if(found->usedMask&(1u<<slot)) {
        if(!found->vertices[slot] || found->vertices[slot]>=owners.size())return false;result.vertices[slot]=owners[found->vertices[slot]];
    }
    output=std::move(result);return true;
}
bool CopyCurrentBindings(ID3D12GraphicsCommandList* list,const native_probe::DrawRecord& draw,bool indexed,DrawResources& output)try{
    if(!list || draw.list!=reinterpret_cast<uintptr_t>(list) || probe::internalCommands || !active.load(std::memory_order_relaxed) ||
        !(draw.flags&128) || (draw.inputSlotMask&~65535u))return false;
    std::lock_guard lock(mutex);const auto found=states.find(list);
    if(!active.load(std::memory_order_relaxed) || found==states.end())return false;
    const auto& state=found->second;DrawResources pending;pending.draw=draw;
    if(indexed){
        const auto bytes=draw.index.Format==DXGI_FORMAT_R16_UINT?2u:draw.index.Format==DXGI_FORMAT_R32_UINT?4u:0u;
        if(!bytes || draw.index.BufferLocation%bytes || draw.index.SizeInBytes%bytes ||
            std::memcmp(&draw.index,&state.indexView,sizeof(draw.index)) ||
            !Matches(state.index,draw.index.BufferLocation,draw.index.SizeInBytes))return false;
        pending.index=state.index;
    }
    for(UINT slot=0;slot<16;++slot)if(draw.inputSlotMask&(1u<<slot)){
        const auto& view=draw.vertices[slot];
        if(std::memcmp(&view,&state.views[slot],sizeof(view)) || !Matches(state.vertices[slot],view.BufferLocation,view.SizeInBytes))return false;
        pending.vertices[slot]=state.vertices[slot];
    }
    output=std::move(pending);return true;
}catch(...){return false;}
}
