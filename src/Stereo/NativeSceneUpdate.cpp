#include "Render/NativeSceneUpdate.hpp"
#include "Render/NativeSceneScopeLiveness.hpp"
#include "Stereo/DetourRegistry.hpp"
#include <windows.h>
#include <atomic>
#include <mutex>
#include <map>

extern "C" {
void* CvrSceneUpdateQueueOriginal{};void* CvrSceneUpdateProducerOriginal{};
void* CvrSceneUpdateGeneralOriginal{};void* CvrSceneUpdatePreOriginal{};
void* CvrSceneUpdateMembershipOriginal{};void* CvrSceneUpdatePostOriginal{};
void* CvrSceneUpdateGeneralRunnerOriginal{};void* CvrSceneUpdateLaneRunnerOriginal{};
void CvrSceneUpdateQueueDetour();void CvrSceneUpdateProducerDetour();
void CvrSceneUpdateGeneralDetour();void CvrSceneUpdatePreDetour();
void CvrSceneUpdateMembershipDetour();void CvrSceneUpdatePostDetour();
void CvrSceneUpdateGeneralRunnerDetour();void CvrSceneUpdateLaneRunnerDetour();
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SceneUpdateRequest{},CyberpunkVR_SceneUpdateState{},CyberpunkVR_SceneUpdateCompatible{},CyberpunkVR_SceneUpdateSeq{},CyberpunkVR_SceneUpdatePending{};
__declspec(dllexport) uint64_t CyberpunkVR_SceneUpdateStats[16]{},CyberpunkVR_SceneUpdateJobs{},CyberpunkVR_SceneUpdateScenes{},CyberpunkVR_SceneUpdateCommands{},CyberpunkVR_SceneUpdateMutations{};
__declspec(dllexport) uint32_t CyberpunkVR_SceneUpdateJobCount{},CyberpunkVR_SceneUpdateSceneCount{},CyberpunkVR_SceneUpdateCommandCount{},CyberpunkVR_SceneUpdateMutationCount{};
__declspec(dllexport) uint32_t CyberpunkVR_SceneUpdateJobBytes=64,CyberpunkVR_SceneUpdateSceneBytes=sizeof(cvr::stereo::scene_update::SceneInput),
    CyberpunkVR_SceneUpdateCommandBytes=sizeof(cvr::stereo::scene_update::CommandInput),CyberpunkVR_SceneUpdateMutationBytes=sizeof(cvr::stereo::scene_update::MutationEntry);
}

namespace cvr::stereo::scene_update {
namespace {
std::once_flag guardsOnce;bool guardsReady{},guardsValid{};std::atomic<bool> hooksEnabled{};uint64_t imageBase{};
std::mutex mutex;
struct PayloadKey {Receipt* receipt{};uint32_t ordinal{};Kind kind{};};
struct Store {
    uint64_t nextReceipt{},nextScope{},latest{};
    std::vector<std::unique_ptr<Receipt>> pending;
    std::map<uint64_t,PayloadKey> payloads;
    std::vector<std::array<uint64_t,8>> jobs;
    std::vector<SceneInput> scenes;std::vector<CommandInput> commands;std::vector<MutationEntry> mutations;
};
// Failed/unclosed native leases are quarantined through process exit. Do not
// release engine job groups from a CRT destructor after the engine shuts down.
Store& Data(){static auto* value=new Store;return *value;}
struct Scope {uint64_t cookie{},stack{};uint32_t event{},ordinal{};Receipt* receipt{};scope_liveness::Anchor anchor;};
thread_local std::array<Scope,16> scopes{};thread_local uint32_t depth{};
bool HooksReady();
bool ReadNative(uint64_t address,void* out,size_t bytes){
    SIZE_T copied{};return address && address<=UINT64_MAX-bytes &&
        ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(uintptr_t(address)),out,bytes,&copied) && copied==bytes;
}
void Retain(void* group){InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(group)+0x1C));}
void Release(void** group){reinterpret_cast<void(*)(void**)>(uintptr_t(imageBase)+0x1419A4)(group);}
bool Complete(void* group){return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(group)+0x18),0,0)==0;}
const JobLeaseApi jobsApi{Retain,Release,Complete};
uint64_t Rva(uint64_t value){return value>=imageBase && value-imageBase<=UINT32_MAX?value-imageBase:0;}
Scope* ActiveProducer(){for(uint32_t i=depth;i;--i)if(scopes[i-1].event==1)return &scopes[i-1];return nullptr;}
Scope* ActiveCallback(){for(uint32_t i=depth;i;--i)if(scopes[i-1].event>=2 && scopes[i-1].event<=5)return &scopes[i-1];return nullptr;}
void Publish(Receipt&,bool retired=false)noexcept;
uint64_t ThunkFunction(uint32_t event){
    const void* functions[]{reinterpret_cast<void*>(&CvrSceneUpdateQueueDetour),reinterpret_cast<void*>(&CvrSceneUpdateProducerDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralDetour),reinterpret_cast<void*>(&CvrSceneUpdatePreDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateMembershipDetour),reinterpret_cast<void*>(&CvrSceneUpdatePostDetour),
        reinterpret_cast<void*>(&CvrSceneUpdateGeneralRunnerDetour),reinterpret_cast<void*>(&CvrSceneUpdateLaneRunnerDetour)};
    return event<8?reinterpret_cast<uintptr_t>(functions[event]):0;
}
bool ValidateScopes(uint64_t excludeEntry=0){
    if(!depth)return true;
    std::array<scope_liveness::Anchor,16> anchors{};
    for(uint32_t i=0;i<depth;++i)anchors[i]=scopes[i].anchor;
    const auto life=scope_liveness::Inspect({anchors.data(),depth},excludeEntry);
    bool known=true;uint32_t keep{};
    for(uint32_t i=0;i<depth;++i){const auto scope=scopes[i];
        if(life[i]==scope_liveness::Life::Absent){
            if(scope.event==1)scope.receipt->CancelProducer();
            else if(scope.event>=2 && scope.event<=5)scope.receipt->CancelCallback(scope.ordinal);
            else scope.receipt->Reject(Failure::Scope);
            Publish(*scope.receipt);
        }else{
            if(life[i]!=scope_liveness::Life::Live){known=false;scope.receipt->Reject(Failure::Scope);Publish(*scope.receipt);}
            scopes[keep++]=scope;
        }
    }
    depth=keep;return known;
}
uint64_t Push(uint32_t event,const ThunkFrame& frame,Receipt* receipt,uint32_t ordinal=UINT32_MAX){
    if(depth>=scopes.size()){receipt->Reject(Failure::Limit);return 0;}
    const auto cookie=++Data().nextScope;
    scopes[depth++]={cookie,frame.stack,event,ordinal,receipt,scope_liveness::Capture(cookie,frame.stack,ThunkFunction(event))};return cookie;
}
void Publish(Receipt& receipt,bool retired)noexcept try{
    auto& data=Data();if(receipt.Binding().nonce!=data.latest)return;
    uint64_t entered{},returned{},opaque{};std::vector<std::array<uint64_t,8>> nextJobs;
    nextJobs.reserve(receipt.Queues().size());
    for(const auto& row:receipt.Queues()){
        entered+=row.entered;returned+=row.returned;opaque+=row.binding.kind==Kind::Opaque;
        const uint64_t flags=1u|(row.entered?2u:0u)|(row.returned?4u:0u)|(row.inventory?8u:0u)|(row.binding.kind==Kind::Opaque?16u:0u)|(row.cancelled?32u:0u);
        nextJobs.push_back({receipt.Binding().nonce,row.binding.ordinal,uint32_t(row.binding.kind),row.binding.payload,
            row.binding.prerequisite,row.binding.completion,flags,row.binding.caller});
    }
    std::vector<SceneInput> nextScenes(receipt.Scenes().begin(),receipt.Scenes().end());
    std::vector<CommandInput> nextCommands(receipt.Commands().begin(),receipt.Commands().end());
    std::vector<MutationEntry> nextMutations(receipt.Mutations().begin(),receipt.Mutations().end());
    const bool closed=receipt.GroupsClosed();
    const uint64_t stats[]{receipt.Binding().nonce,receipt.Binding().manager,receipt.Binding().thread,receipt.Binding().caller,
        (receipt.ProducerClosed()?1u:0u)|(receipt.ProducerCancelled()?2u:0u),receipt.Queues().size(),entered,returned,opaque,receipt.Scenes().size(),receipt.Commands().size(),receipt.Mutations().size(),
        uint32_t(receipt.Rejection()),closed,0,retired};
    CyberpunkVR_SceneUpdateSeq.fetch_add(1,std::memory_order_acq_rel);
    data.jobs.swap(nextJobs);data.scenes.swap(nextScenes);data.commands.swap(nextCommands);data.mutations.swap(nextMutations);
    std::copy_n(stats,16,CyberpunkVR_SceneUpdateStats);
    CyberpunkVR_SceneUpdateJobs=reinterpret_cast<uintptr_t>(data.jobs.data());CyberpunkVR_SceneUpdateJobCount=uint32_t(data.jobs.size());
    CyberpunkVR_SceneUpdateScenes=reinterpret_cast<uintptr_t>(data.scenes.data());CyberpunkVR_SceneUpdateSceneCount=uint32_t(data.scenes.size());
    CyberpunkVR_SceneUpdateCommands=reinterpret_cast<uintptr_t>(data.commands.data());CyberpunkVR_SceneUpdateCommandCount=uint32_t(data.commands.size());
    CyberpunkVR_SceneUpdateMutations=reinterpret_cast<uintptr_t>(data.mutations.data());CyberpunkVR_SceneUpdateMutationCount=uint32_t(data.mutations.size());
    CyberpunkVR_SceneUpdatePending=uint32_t(data.pending.size());
    CyberpunkVR_SceneUpdateState=receipt.Rejection()!=Failure::None?4:closed?3:2;
    CyberpunkVR_SceneUpdateSeq.fetch_add(1,std::memory_order_release);
}catch(...){receipt.Reject(Failure::Overflow);CyberpunkVR_SceneUpdateState=4;}
bool SourceQueue(uint64_t caller,Kind& kind,uint64_t& callback){
    constexpr uint32_t callers[]{0xC1A168,0x39D680,0x39DF2E,0xAF2818};
    constexpr uint32_t callbacks[]{0xCA0E50,0xAC209C,0x39E73C,0xB7A468};
    for(uint32_t i=0;i<4;++i)if(caller==callers[i]){kind=Kind(i);callback=imageBase+callbacks[i];return true;}
    return false;
}
uint64_t BeforeQueue(const ThunkFrame& frame){
    auto* producer=ActiveProducer();auto* callbackScope=ActiveCallback();
    if(!producer && !callbackScope)return 0;
    std::array<uint64_t,4> job{};
    if(!ReadNative(frame.rdx,job.data(),sizeof(job))){(producer?producer:callbackScope)->receipt->Reject(Failure::Payload);return 0;}
    Kind kind=Kind::Opaque;uint64_t callback{};const auto known=SourceQueue(Rva(frame.caller),kind,callback) && producer;
    auto* scope=known?producer:(callbackScope?callbackScope:producer);if(!scope)return 0;
    auto& receipt=*scope->receipt;
    if(known && job[0]!=callback){receipt.Reject(Failure::Binding);return 0;}
    if(known && Data().payloads.contains(job[1])){receipt.Reject(Failure::Binding);return 0;}
    QueueBinding binding{known?kind:Kind::Opaque,0,Rva(frame.caller),job[0],job[1],frame.r9,frame.arg5};uint32_t ordinal{};
    if(receipt.Register(binding,ReadNative,ordinal) && known)Data().payloads.emplace(job[1],PayloadKey{&receipt,ordinal,kind});
    Publish(receipt);return 0;
}
uint64_t BeforeProducer(const ThunkFrame& frame){
    if(CyberpunkVR_SceneUpdateRequest.load(std::memory_order_relaxed)!=1)return 0;
    const auto caller=Rva(frame.caller);
    if(caller!=0x2938E8 && caller!=0x293B33 && caller!=0x167D76)return 0;
    if(!HooksReady()){CyberpunkVR_SceneUpdateCompatible=2;CyberpunkVR_SceneUpdateState=4;CyberpunkVR_SceneUpdateRequest=0;return 0;}
    auto& data=Data();if(data.pending.size()>=MaxProducers){CyberpunkVR_SceneUpdateState=4;CyberpunkVR_SceneUpdateRequest=0;return 0;}
    if(!CyberpunkVR_SceneUpdateRequest.exchange(0))return 0;
    const auto nonce=++data.nextReceipt;
    auto receipt=std::make_unique<Receipt>(Identity{nonce,frame.rcx,caller,GetCurrentThreadId(),0},jobsApi);
    auto* value=receipt.get();data.pending.push_back(std::move(receipt));data.latest=nonce;
    const auto cookie=Push(1,frame,value);Publish(*value);return cookie;
}
uint64_t BeforeCallback(uint32_t event,const ThunkFrame& frame){
    auto& data=Data();const auto found=data.payloads.find(frame.rcx);if(found==data.payloads.end())return 0;
    const auto key=found->second;data.payloads.erase(found);
    auto& receipt=*key.receipt;const auto kind=Kind(event-2);
    uint64_t parent{},group{};int32_t pending{};
    if(frame.rdx>UINT64_MAX-0x28 || !ReadNative(frame.rdx+0x20,&parent,8) || !ReadNative(parent,&group,8) ||
        group>UINT64_MAX-0x20 || !ReadNative(group+0x18,&pending,4) || pending<=0 ||
        key.kind!=kind || !receipt.Enter(key.ordinal,kind,frame.rcx,group,ReadNative)){
        receipt.Reject(Failure::Binding);Publish(receipt);return 0;
    }
    const auto cookie=Push(event,frame,&receipt,key.ordinal);Publish(receipt);return cookie;
}
uint64_t BeforeRunner(uint32_t event,const ThunkFrame& frame){
    auto* scope=ActiveCallback();if(!scope)return 0;
    const auto caller=Rva(frame.caller);const auto lane=uint32_t(frame.r8);
    const bool expected=event==6?caller==0xCA0E8D:(caller==0xAC20DF && lane==1) || (caller==0xB7A4A8 && lane==0);
    if(!expected){scope->receipt->Reject(Failure::Binding);return 0;}
    scope->receipt->Inventory(scope->ordinal,frame.rcx,frame.rdx,lane,ReadNative);
    const auto cookie=Push(event,frame,scope->receipt,scope->ordinal);Publish(*scope->receipt);return cookie;
}
bool Wanted(){
    wchar_t value[8]{};if(GetEnvironmentVariableW(L"CVR_STEREO_SCENE_UPDATE",value,8)!=1 || value[0]!=L'1')return false;
    hooksEnabled=guardsReady && guardsValid;CyberpunkVR_SceneUpdateCompatible=hooksEnabled?1:2;return hooksEnabled;
}
CVR_DETOUR_IF("[single-pass] scene-update queue payload admission",0x14299C,CvrSceneUpdateQueueDetour,CvrSceneUpdateQueueOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update producer scope",0x39D2FC,CvrSceneUpdateProducerDetour,CvrSceneUpdateProducerOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update ordinary command worker",0xCA0E50,CvrSceneUpdateGeneralDetour,CvrSceneUpdateGeneralOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update pre command worker",0xAC209C,CvrSceneUpdatePreDetour,CvrSceneUpdatePreOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update membership worker",0x39E73C,CvrSceneUpdateMembershipDetour,CvrSceneUpdateMembershipOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update post command worker",0xB7A468,CvrSceneUpdatePostDetour,CvrSceneUpdatePostOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update ordinary virtual input inventory",0x36EEF8,CvrSceneUpdateGeneralRunnerDetour,CvrSceneUpdateGeneralRunnerOriginal,Wanted)
CVR_DETOUR_IF("[single-pass] scene-update lane virtual input inventory",0xAA31C4,CvrSceneUpdateLaneRunnerDetour,CvrSceneUpdateLaneRunnerOriginal,Wanted)
bool HooksReady(){
    return g_cvrDetourReg_CvrSceneUpdateQueueDetour.installed && g_cvrDetourReg_CvrSceneUpdateProducerDetour.installed &&
        g_cvrDetourReg_CvrSceneUpdateGeneralDetour.installed && g_cvrDetourReg_CvrSceneUpdatePreDetour.installed &&
        g_cvrDetourReg_CvrSceneUpdateMembershipDetour.installed && g_cvrDetourReg_CvrSceneUpdatePostDetour.installed &&
        g_cvrDetourReg_CvrSceneUpdateGeneralRunnerDetour.installed && g_cvrDetourReg_CvrSceneUpdateLaneRunnerDetour.installed;
}
}
void PrepareCodeGuards()noexcept{
    try{std::call_once(guardsOnce,[]{
        imageBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));bool valid=imageBase!=0;
        constexpr struct Guard {uint32_t rva,bytes;uint64_t hash;} guards[]{
            {0x39D2FC,0x1F2,0xA0D181E036B9DDA0ull},{0xC1A0CC,0xF9,0xFE2DC72BCF878300ull},
            {0x39D5E4,0xF9,0x2BE2D1F95217B28Aull},{0x39DEB0,0xD6,0x418C5E54DEBC7794ull},
            {0x39DF88,0xA3,0x9F16D55161328E27ull},{0xAF277C,0xF9,0x871E07FA26D91758ull},
            {0x39E02C,0xD4,0x8A6E4118DB7E56ADull},{0x39E58C,0xF3,0xADE3DCDFB62EAD77ull},
            {0xCA0E50,0x6D,0x81FB05338C0A8BE5ull},{0xAC209C,0x73,0x69E9F0C0FFF4E3FEull},
            {0x39E73C,0x9E,0x2038C473ED1117D7ull},{0x39EA48,0x2F7,0xCCC666F4BF24A797ull},
            {0xB7A468,0x70,0x37D86E5EF04C79F3ull},{0x36EEF8,0xD2,0xDBE66A465B147828ull},
            {0xAA31C4,0x108,0xFAFDFADDB39FE16Aull},{0x14299C,0x183,0xFDFF35624D704435ull},
            {0x141B10,0x15A,0x998B02126473A17Eull},{0x1419A4,0x4A,0x35299EDE5507625Eull},
            {0x142634,0x156,0xA28DBE6ABEB41274ull},{0x142F88,0xCA,0x68358540D95CCC78ull}};
        std::array<uint8_t,0x400> bytes{};
        for(const auto& guard:guards){if(!ReadNative(imageBase+guard.rva,bytes.data(),guard.bytes)){valid=false;break;}
            uint64_t hash=14695981039346656037ull;for(uint32_t i=0;i<guard.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=guard.hash){valid=false;break;}}
        guardsValid=valid;guardsReady=true;CyberpunkVR_SceneUpdateCompatible=valid?1:2;
    });}catch(...){CyberpunkVR_SceneUpdateCompatible=2;}
}
uint64_t Before(uint32_t event,const ThunkFrame* frame)noexcept{
    if(!hooksEnabled || !frame)return 0;
    try{
        if(!depth && event==1 && CyberpunkVR_SceneUpdateRequest.load(std::memory_order_relaxed)!=1)return 0;
        std::lock_guard lock(mutex);if(!ValidateScopes(frame->stack))return 0;
        if(event==0)return BeforeQueue(*frame);
        if(event==1)return BeforeProducer(*frame);
        if(event>=2 && event<=5)return BeforeCallback(event,*frame);
        if(event==6 || event==7)return BeforeRunner(event,*frame);
    }catch(...){CyberpunkVR_SceneUpdateState=4;}
    return 0;
}
void After(uint32_t event,uint64_t cookie,const ThunkFrame* frame)noexcept{
    if(!cookie || !frame)return;
    try{std::lock_guard lock(mutex);if(!ValidateScopes())return;
        if(!depth || scopes[depth-1].cookie!=cookie || scopes[depth-1].event!=event || scopes[depth-1].stack!=frame->stack){
            if(depth)scopes[depth-1].receipt->Reject(Failure::Scope);depth=0;CyberpunkVR_SceneUpdateState=4;return;}
        const auto scope=scopes[--depth];auto& receipt=*scope.receipt;
        if(event==1)receipt.CloseProducer();else if(event>=2 && event<=5)receipt.Returned(scope.ordinal);
        Publish(receipt);
    }catch(...){CyberpunkVR_SceneUpdateState=4;}
}
void Poll()noexcept{
    if(!hooksEnabled)return;
    try{std::lock_guard lock(mutex);auto& data=Data();
        for(auto it=data.pending.begin();it!=data.pending.end();){auto& receipt=**it;
            if(receipt.GroupsClosed()){
                Publish(receipt,true);std::erase_if(data.payloads,[&](const auto& row){return row.second.receipt==&receipt;});it=data.pending.erase(it);
            }else{Publish(receipt);++it;}}
        CyberpunkVR_SceneUpdatePending=uint32_t(data.pending.size());
    }catch(...){CyberpunkVR_SceneUpdateState=4;}
}
void ObserveBoundsWrite(uint64_t pool,uint64_t indices,uint64_t caller)noexcept{
    if(!hooksEnabled || !depth)return;
    try{std::lock_guard lock(mutex);if(!ValidateScopes())return;if(auto* scope=ActiveCallback()){
        scope->receipt->Mutation(scope->ordinal,pool,indices,Rva(caller),GetCurrentThreadId());Publish(*scope->receipt);}
    }catch(...){CyberpunkVR_SceneUpdateState=4;}
}
}
extern "C" uint64_t CvrSceneUpdateBefore(uint32_t event,const cvr::stereo::scene_update::ThunkFrame* frame)noexcept{return cvr::stereo::scene_update::Before(event,frame);}
extern "C" void CvrSceneUpdateAfter(uint32_t event,uint64_t cookie,const cvr::stereo::scene_update::ThunkFrame* frame)noexcept{cvr::stereo::scene_update::After(event,cookie,frame);}
