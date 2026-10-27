#include "Render/PhysicalOpticsShaders.hpp"
#include <windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <cstring>

extern void Log(const char*,...);
namespace cvr::optics {
namespace {
using Microsoft::WRL::ComPtr;
struct Shader {
    uint64_t nativeHash;
    const wchar_t* file;
    std::once_flag once;
    std::vector<uint8_t> bytes;
};
std::array<Shader,2> shaders{{
    {0x011D27FFE5AAD161ull,L"CyberpunkVR_ScopeGlass_011D27FFE5AAD161.dxil"},
    {0x39E6BD24D20C3295ull,L"CyberpunkVR_ScopeGlass_39E6BD24D20C3295.dxil"}
}};

Shader velocityShader{0,L"CyberpunkVR_LensVelocityPs.dxil"};
Shader lensMaskShader{0,L"CyberpunkVR_LensMaskPs.dxil"};
Shader sightMaskShader{0,L"CyberpunkVR_SightMaskPs.dxil"};
Shader resolveShader{0,L"CyberpunkVR_LensSourceResolveCs.dxil"};
Shader cameraResolveShader{0,L"CyberpunkVR_CameraSourceResolveCs.dxil"};
uint64_t Hash(const D3D12_SHADER_BYTECODE& shader) {
    // Same bytecode hash as the existing pipeline inventory.
    uint64_t value=1469598103934665603ull;
    const auto* bytes=static_cast<const uint8_t*>(shader.pShaderBytecode);
    for(size_t i=0;i<shader.BytecodeLength;++i){value^=bytes[i];value*=1099511628211ull;}
    return value;
}
void Load(Shader& shader) {
    HMODULE module{};wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GlassShaderReplacement),&module))return;
    const auto length=GetModuleFileNameW(module,path,MAX_PATH);
    if(!length || length>=MAX_PATH)return;
    auto* slash=wcsrchr(path,L'\\');if(!slash)return;slash[1]=0;
    if(wcscat_s(path,shader.file))return;
    const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){Log("[optics] Missing glass shader %ls\n",shader.file);return;}
    LARGE_INTEGER size{};DWORD read{};
    if(GetFileSizeEx(file,&size) && size.QuadPart>=64 && size.QuadPart<=262144) {
        shader.bytes.resize(static_cast<size_t>(size.QuadPart));
        if(!ReadFile(file,shader.bytes.data(),static_cast<DWORD>(shader.bytes.size()),&read,nullptr) ||
           read!=shader.bytes.size() || std::memcmp(shader.bytes.data(),"DXBC",4))shader.bytes.clear();
    }
    CloseHandle(file);
    if(!shader.bytes.empty())Log("[optics] Loaded glass shader %ls (%zu bytes)\n",shader.file,shader.bytes.size());
}
// Glass pipelines, looked up on every draw without a lock: an open-addressing
// table keyed by the pipeline's address, entries never removed. The game builds
// one per render state the scope material meets -- 48 in one session, where a
// 32-entry list left 16 of them with engine shading rate, no lens velocity and
// no history mask (blocks and blinking in the lens on a turn). A released
// pipeline whose address is reused only loses coarse shading and gets redraws
// from a lens marker it does not carry (discarded).
struct GlassEntry {
    std::atomic<ID3D12PipelineState*> glass{};
    std::atomic<ID3D12PipelineState*> velocity{};
    std::atomic<ID3D12PipelineState*> mask{};
};
std::atomic<uint64_t> glassTick{};
constexpr size_t GlassSlots=1024,GlassLimit=GlassSlots*3/4;   // power of two; probes stay short
std::array<GlassEntry,GlassSlots> glassPipelines{};
std::atomic<unsigned> glassCount{};
std::mutex glassMutex;
size_t GlassSlot(const ID3D12PipelineState* pso) {
    auto v=reinterpret_cast<uintptr_t>(pso);v^=v>>21;v*=0x9E3779B97F4A7C15ull;return size_t(v>>40)&(GlassSlots-1);
}
const GlassEntry* FindGlass(ID3D12PipelineState* pso) {
    if(!pso)return nullptr;
    for(size_t i=GlassSlot(pso),n=0;n<GlassSlots;++n,i=(i+1)&(GlassSlots-1)) {
        const auto* glass=glassPipelines[i].glass.load(std::memory_order_acquire);
        if(glass==pso)return &glassPipelines[i];
        if(!glass)return nullptr;
    }
    return nullptr;
}
struct Rate {
    D3D12_SHADING_RATE value=D3D12_SHADING_RATE_1X1;
    std::array<D3D12_SHADING_RATE_COMBINER,2> combiners{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
};
std::mutex rateMutex;
std::unordered_map<ID3D12GraphicsCommandList*,Rate> rates; // only lists that set a rate

// The engine's velocity target, as Streamline is told about it. Both eyes
// render into the same G-buffer velocity texture, tagged before every DLSS
// evaluation, so the previous tag names the target of the coming glass draw.
struct Velocity {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    bool ready{};
};
std::mutex velocityMutex;
Velocity velocity;

// Render targets as last bound on a list by its recording thread. One list is
// recorded by one thread at a time; a few slots cover interleaved recording.
struct Bound {
    ID3D12GraphicsCommandList* list{};
    BoundTargets targets;
};
thread_local std::array<Bound,4> bound{};
thread_local unsigned boundNext{};
}
void GlassPipelineCreated(ID3D12PipelineState* pso,ID3D12PipelineState* velocityPipeline,ID3D12PipelineState* maskPipeline) {
    {
        std::lock_guard lock(glassMutex);
        if(pso && !FindGlass(pso) && glassCount.load(std::memory_order_relaxed)<GlassLimit) {
            size_t i=GlassSlot(pso);
            while(glassPipelines[i].glass.load(std::memory_order_relaxed))i=(i+1)&(GlassSlots-1);
            glassPipelines[i].velocity.store(velocityPipeline,std::memory_order_release);
            glassPipelines[i].mask.store(maskPipeline,std::memory_order_release);
            glassPipelines[i].glass.store(pso,std::memory_order_release);
            const auto n=glassCount.fetch_add(1,std::memory_order_acq_rel)+1;
            if(n==1 || n%16==0)Log("[optics] Glass pipelines registered: %u\n",n);
            return;
        }
        if(!pso || FindGlass(pso)){if(velocityPipeline)velocityPipeline->Release();if(maskPipeline)maskPipeline->Release();return;}
    }
    if(velocityPipeline)velocityPipeline->Release();
    if(maskPipeline)maskPipeline->Release();
    Log("[optics] Glass pipeline registry full; later glass keeps engine shading rate\n");
}
bool LensSourceResolveShader(D3D12_SHADER_BYTECODE& shader) {
    std::call_once(resolveShader.once,[]{Load(resolveShader);});
    shader={};if(resolveShader.bytes.empty())return false;
    shader={resolveShader.bytes.data(),resolveShader.bytes.size()};return true;
}
bool CameraSourceResolveShader(D3D12_SHADER_BYTECODE& shader) {
    std::call_once(cameraResolveShader.once,[]{Load(cameraResolveShader);});
    shader={};if(cameraResolveShader.bytes.empty())return false;
    shader={cameraResolveShader.bytes.data(),cameraResolveShader.bytes.size()};return true;
}
bool GlassRecent() {
    return GetTickCount64()-glassTick.load(std::memory_order_relaxed)<500;
}
bool LensVelocityShader(D3D12_SHADER_BYTECODE& shader) {
    std::call_once(velocityShader.once,[]{Load(velocityShader);});
    shader={};if(velocityShader.bytes.empty())return false;
    shader={velocityShader.bytes.data(),velocityShader.bytes.size()};return true;
}
bool LensMaskShader(D3D12_SHADER_BYTECODE& shader) {
    std::call_once(lensMaskShader.once,[]{Load(lensMaskShader);});
    shader={};if(lensMaskShader.bytes.empty())return false;
    shader={lensMaskShader.bytes.data(),lensMaskShader.bytes.size()};return true;
}
bool SightMaskShader(D3D12_SHADER_BYTECODE& shader) {
    std::call_once(sightMaskShader.once,[]{Load(sightMaskShader);});
    shader={};if(sightMaskShader.bytes.empty())return false;
    shader={sightMaskShader.bytes.data(),sightMaskShader.bytes.size()};return true;
}
ID3D12PipelineState* GlassMaskPipeline(ID3D12PipelineState* pso) {
    const auto* glass=pso && glassCount.load(std::memory_order_relaxed) ? FindGlass(pso) : nullptr;
    return glass ? glass->mask.load(std::memory_order_acquire) : nullptr;
}
bool BoundTargetsOf(ID3D12GraphicsCommandList* list,BoundTargets& targets) {
    for(const auto& item:bound)if(item.list==list && item.targets.valid){targets=item.targets;return true;}
    return false;
}
void MotionVectorsTagged(ID3D12Resource* resource) {
    if(!resource)return;
    std::lock_guard lock(velocityMutex);
    if(velocity.resource.Get()==resource)return;
    velocity={};
    const auto desc=resource->GetDesc();
    if(desc.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Format!=DXGI_FORMAT_R16G16_FLOAT ||
       !(desc.Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) {
        static std::atomic<bool> logged{};
        if(!logged.exchange(true))Log("[optics] Velocity tag format %u flags %X unsupported; lens velocity off\n",unsigned(desc.Format),unsigned(desc.Flags));
        return;
    }
    ComPtr<ID3D12Device> device;
    if(FAILED(resource->GetDevice(IID_PPV_ARGS(&device))))return;
    D3D12_DESCRIPTOR_HEAP_DESC heap{D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,0};
    if(FAILED(device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&velocity.heap))))return;
    D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=DXGI_FORMAT_R16G16_FLOAT;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
    velocity.rtv=velocity.heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(resource,&view,velocity.rtv);
    velocity.resource=resource;velocity.ready=true;
    Log("[optics] Lens velocity target %p %llux%u\n",static_cast<void*>(resource),static_cast<unsigned long long>(desc.Width),desc.Height);
}
ComPtr<ID3D12Resource> MotionVectors() {
    std::lock_guard lock(velocityMutex);
    return velocity.ready ? velocity.resource : nullptr;
}
void Targets(ID3D12GraphicsCommandList* list,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE* handles,BOOL contiguous,
             const D3D12_CPU_DESCRIPTOR_HANDLE* depth) {
    Bound* entry=nullptr;
    for(auto& item:bound)if(item.list==list){entry=&item;break;}
    if(!entry){entry=&bound[boundNext++%bound.size()];entry->list=list;}
    auto& t=entry->targets;t={};
    if(!handles)count=0;
    if(count>8){entry->list=nullptr;return;} // unsupported: never redraw over it
    t.count=count;t.contiguous=contiguous;
    for(UINT i=0;i<(contiguous?(count?1u:0u):count);++i)t.handles[i]=handles[i];
    if(depth && depth->ptr){t.depth=*depth;t.hasDepth=true;}
    t.valid=true;
}
bool PrepareLensVelocity(ID3D12GraphicsCommandList* list,ID3D12PipelineState* pso,LensVelocityPass& pass) {
    pass={};
    const auto* glass=pso && glassCount.load(std::memory_order_relaxed) ? FindGlass(pso) : nullptr;
    if(glass)glassTick.store(GetTickCount64(),std::memory_order_relaxed);
    if(!glass || !(pass.pipeline=glass->velocity.load(std::memory_order_acquire)))return false;
    for(const auto& item:bound)if(item.list==list && item.targets.valid){pass.targets=item.targets;break;}
    if(!pass.targets.valid)return false;
    std::lock_guard lock(velocityMutex);
    if(!velocity.ready)return false;
    pass.target=velocity.resource;pass.rtv=velocity.rtv;
    return true;
}
void ListReset(ID3D12GraphicsCommandList* list) {
    // Reset and ClearState restore D3D12's default: 1x1, both combiners passthrough.
    for(auto& item:bound)if(item.list==list)item={};
    std::lock_guard lock(rateMutex);if(const auto it=rates.find(list);it!=rates.end())it->second={};
}
void ShadingRateSet(ID3D12GraphicsCommandList* list,D3D12_SHADING_RATE value,const D3D12_SHADING_RATE_COMBINER* combiners) {
    Rate rate{value};if(combiners)rate.combiners={combiners[0],combiners[1]};
    std::lock_guard lock(rateMutex);if(rates.size()<4096 || rates.contains(list))rates[list]=rate;
}
FullRateDraw::FullRateDraw(ID3D12GraphicsCommandList* base,ID3D12PipelineState* pso,RateFn setRate) {
    if(!base || !pso || !setRate || !glassCount.load(std::memory_order_relaxed) || !FindGlass(pso))return;
    Rate engine;{std::lock_guard lock(rateMutex);if(const auto it=rates.find(base);it!=rates.end())engine=it->second;}
    // Base 1x1 with both combiners passthrough ignores per-primitive rates and the rate image.
    static constexpr D3D12_SHADING_RATE_COMBINER passthrough[]{D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
    if(engine.value==D3D12_SHADING_RATE_1X1 && engine.combiners[0]==passthrough[0] && engine.combiners[1]==passthrough[1])return;
    list=static_cast<ID3D12GraphicsCommandList5*>(base);set=setRate;rate=engine.value;combiners=engine.combiners;
    set(list,D3D12_SHADING_RATE_1X1,passthrough);
}
FullRateDraw::~FullRateDraw(){if(list)set(list,rate,combiners.data());}
bool GlassShaderReplacement(const D3D12_SHADER_BYTECODE& native,D3D12_SHADER_BYTECODE& replacement) {
    replacement={};if(!native.pShaderBytecode || !native.BytecodeLength)return false;
    // Reject by measured native lengths before hashing unrelated shaders.
    if(native.BytecodeLength!=32289 && native.BytecodeLength!=32581)return false;
    const auto hash=Hash(native);
    for(auto& shader:shaders)if(shader.nativeHash==hash) {
        std::call_once(shader.once,[&]{Load(shader);});
        if(shader.bytes.empty())return false;
        replacement={shader.bytes.data(),shader.bytes.size()};return true;
    }
    return false;
}
}
