#include "Render/NativeDefinitionTracking.hpp"
#include "Render/NativeResourceReplay.hpp"
#include "Render/NativeGraphDefinitionSource.hpp"
#include "Render/NativeGraphConsumerIdentity.hpp"
#include "Render/NativeGraphTaskCompletion.hpp"
#include "Render/NativeGraphDeclarationCompletion.hpp"
#include "Render/NativeGraphCompilerIdentity.hpp"
#include "Render/NativeDefinitionGenerations.hpp"
#include "Render/NativeDefinitionTree.hpp"
#include "Render/NativeStageDefinition.hpp"
#include "Render/NativeSceneSections.hpp"
#include "Render/BorrowedSceneSections.hpp"
#include "Render/SceneGraphExpansion.hpp"
#include "Render/BothEyesSceneExpansion.hpp"
#include "Render/OwnedGraphExecution.hpp"
#include "Render/OwnedGraphExecutionState.hpp"
#include "Render/OwnedNativeGraph.hpp"
#include "Render/PairedCpuFrontier.hpp"
#include "Render/NativeContextOrder.hpp"
#include "Render/NativeRetirementDeferral.hpp"
#include "Render/NativeAllocatorBinding.hpp"
#include "Render/NativeResourceUses.hpp"
#include "Render/NativePreparation.hpp"
#include "Render/NativeSceneChild.hpp"
#include "Render/NativeGraphParent.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Stereo/EngineRvas.hpp"
#include "Stereo/DetourRegistry.hpp"
#include "Stereo/RenderParity.hpp"
#include <windows.h>
#include <intrin.h>
#include <map>
#include <mutex>

extern "C" {
void CvrRenderGraphCacheResultDetour();
void* CvrRenderGraphCacheResultOriginal{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_PreparationDefinitionMode{},CyberpunkVR_PreparationDefinitionRequest{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_PreparationDefinitionCompatible{},CyberpunkVR_PreparationDefinitionSeq{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionStats[16]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionTicket[8]{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_PreparationNativeConsumers[10]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionAggregate[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationNativeConsumerReject[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionOwners{},CyberpunkVR_PreparationDefinitionMappings{},CyberpunkVR_PreparationDefinitionEntries{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_PreparationDefinitionMutationCounters[4]{};
__declspec(dllexport) std::atomic<uint64_t> CyberpunkVR_PreparationGenerationStats[16]{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_PreparationGenerationSeq{},CyberpunkVR_PreparationGenerationCount{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_PreparationGenerationCoverageLost{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationGenerationTicket[4]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationGenerationReject[8]{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_PreparationGenerationRejectPhase{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationGenerationRows[128][12]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionTreeStats[12]{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionTreeRoots{},CyberpunkVR_PreparationDefinitionTreeNodes{},CyberpunkVR_PreparationDefinitionTreeHandles{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionTreeArrays{},CyberpunkVR_PreparationDefinitionTreeAllocators{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionTreeStrings{};
__declspec(dllexport) uint64_t CyberpunkVR_PreparationDefinitionDeclaration[8]{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_OwnedExecutionMode{},CyberpunkVR_OwnedExecutionRequest{},CyberpunkVR_OwnedExecutionClosing{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_OwnedExecutionSeq{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionStats[24]{},CyberpunkVR_OwnedExecutionResources[12]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionCompiler[16]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionSchedule[16]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionScheduleNodes{},CyberpunkVR_OwnedExecutionScheduleEdges{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionScheduleBindings{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedStageStats[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedStageDefinitions{},CyberpunkVR_OwnedStageEntries{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedSceneStats[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedSceneRoots{},CyberpunkVR_OwnedSceneChildren{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_OwnedSceneSplitEnable{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_SceneChildEnable{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedSceneSplitStats[24]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedSceneSplitSections[12]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedSceneSplitSourceNodes{},CyberpunkVR_OwnedSceneSplitSourceEdges{},CyberpunkVR_OwnedSceneSplitMappingRows{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionRetirement[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionRetirementFailure[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionAllocator[24]{},CyberpunkVR_OwnedExecutionRetirementTotals[16]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionRetirementEventSeq{},CyberpunkVR_OwnedExecutionRetirementEventCount{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionRetirementEvents[32][24]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionCache[16]{},CyberpunkVR_OwnedExecutionCacheRows[64][8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedExecutionAdmission[6]{};
__declspec(dllexport) std::atomic<uint32_t> CyberpunkVR_OwnedResourceReplayEnable{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayStats[16]{},CyberpunkVR_OwnedResourceReplayRows{};
__declspec(dllexport) uint32_t CyberpunkVR_OwnedResourceReplayRowBytes=32;
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayProof[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayRegistration[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayReset[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayAsync[8]{};
__declspec(dllexport) uint64_t CyberpunkVR_OwnedResourceReplayMismatch[16]{};
void* CvrResourceContextOriginal{};
void CvrResourceContextDetour();
extern std::atomic<uint32_t> CyberpunkVR_PreparationGraphJobsEpoch;
extern uint64_t CyberpunkVR_PreparationGraphJobsCompatibility[8];
}

namespace cvr::stereo::definition_tracking {
namespace {
constexpr size_t MaxTrackedOwners=4096;
struct RetainedOwner;
struct Stamp {uint64_t epoch{},generation{},storage{},serviceEpoch{},serviceStorage{};uint32_t inFlight{},kind{},builders{},operations{},slot{};bool builderClosed{},storageAlive{};RetainedOwner* serviceOwner{};};
struct BuilderReservation {uint64_t address{},header{},epoch{},thread{};uint32_t endReturn{};};
std::array<BuilderReservation,MaxTrackedOwners> builders;
std::atomic<bool> writerCoverageLost{};
uint64_t nextStorage{},storageSlots{};
constexpr struct BuilderSite {uint32_t begin,end;} builderSites[]{
    {0x982CA0,0x982EE6},{0x982F25,0x9837A4},{0x1D43190,0x1D47594},{0x1D475FF,0x1D47FC8},
    {0x1D4804A,0x1D492BC},{0x28E5EA1,0x28E66E3},{0x28E672E,0x28E6AF7},{0x28E6B47,0x28E6EF9}};
struct OwnerRow {
    uint64_t graph{},header{},data{},allocator{},epoch{},entryStart{};
    uint32_t capacity{},count{},inFlight{},kind{};
};
static_assert(sizeof(OwnerRow)==64 && sizeof(graph_definition::Mapping)==24);
std::mutex mutex;
std::map<uint64_t,Stamp> mutations;
std::vector<OwnerRow> owners;
std::vector<graph_definition::Mapping> mappings;
std::vector<uint64_t> entries;
graph_definition::SourceReceipt source;
definition_tree::Snapshot definitionTree;
uint64_t nextMutation{},captureEpoch{},captureGeneration{},captureOwner{};
uint64_t nextConsumer{};
definition_generations::Ledger generationLedger;
std::vector<std::pair<uint64_t,uint64_t>> closedGenerations;
struct RootAdmission {
    uint64_t id{},epoch{},pairGeneration{},renderer{},prerequisiteGroup{};
    uint64_t cache{},key{},missed{};
    uint32_t use{},views{};
    bool cachePending{};
    bool registered{},constructed{};
};
thread_local std::array<RootAdmission,32> rootAdmissions;
thread_local uint32_t rootAdmissionDepth{};
auto& sourceTicket=*new std::shared_ptr<graph_jobs::NativeRootTicket>;
struct DefinitionBinding {uint64_t generation{},bornEpoch{};};
void RetainGroup(void* group);
void ReleaseGroup(void** group);
struct RetainedOwner {
    retirement_deferral::HeaderIdentity identity;
    retirement_deferral::State* state{};
    definition_tree::Snapshot tree;
    allocator_binding::Receipt allocatorBinding;
    std::vector<uint64_t> definitions;
    bool ready{},emergencyDetached{},destroyInFlight{};
    uint64_t lastConsumerId{},lastEpoch{},lastPairGeneration{};
    bool protectedAttached{};
    uint64_t completionFlags{},declarationGroup{},executionGroup{},compilerGroup{},completedReadersReceipt{};
    uint64_t serviceHeader{},serviceStorage{},serviceEpoch{},serviceCaller{};
};
struct ExtraReader {
    RetainedOwner* owner{};
    retirement_deferral::ExtraReaderIdentity identity;
};
struct EarlyTerminalPin {
    explicit EarlyTerminalPin(void* value):group(value){RetainGroup(group);}
    ~EarlyTerminalPin(){if(group)ReleaseGroup(&group);}
    void* group{};
};
struct Consumer {
    std::shared_ptr<graph_jobs::NativeRootTicket> ticket;
    std::shared_ptr<graph_jobs::TaskCompletionLease> completion;
    std::shared_ptr<graph_jobs::DeclarationCompletionLease> declaration;
    std::shared_ptr<graph_jobs::CompilerIdentityLease> compiler;
    std::shared_ptr<graph_jobs::NativeRootTicket> fallbackTicket;
    std::shared_ptr<graph_jobs::DeclarationCompletionLease> fallbackDeclaration;
    bool callbackEntered{},callbackReturned{},rejected{};
    bool sourceVerified{},executionVerified{},generationRejected{};
    uint64_t renderer{},rejectionDetail{},rejectionHeader{};
    std::map<uint64_t,DefinitionBinding> definitions;
    std::vector<DefinitionBinding> generations;
    std::unique_ptr<OwnedNativeGraph> ownedGraph;
    graph_snapshot::Snapshot originalGraph,executionGraph;
    graph_definition::SourceReceipt originalSource;
    definition_tree::Snapshot originalTree;
    std::array<scene_sections::Snapshot,2> sceneSections;
    scene_sections::Snapshot sceneSplitSource;
    borrowed_scene_sections::Sections sceneViews;
    borrowed_scene_sections::CallerLifetime sceneLifetime;
    std::unique_ptr<scene_expansion::Plan> sceneExpansion;
    std::array<scene_sections::Snapshot,2> sceneBothSources;
    std::array<borrowed_scene_sections::Sections,2> sceneBothViews;
    std::array<borrowed_scene_sections::CallerLifetime,2> sceneBothLifetimes;
    std::unique_ptr<scene_expansion::BothEyesPlan> sceneBothExpansion;
    uint32_t sceneChildScopes{};
    graph_snapshot::Snapshot coupledGraph;
    owned_graph_execution::State* ownedState{};
    std::vector<ExtraReader> extraReaders;
    std::unique_ptr<EarlyTerminalPin> executionTerminal;
    uint64_t producerScope{},declarationContext{},resourcePool{},resourceWrapper{},compilerProducerIdentity{};
    bool extraReadersCompleted{},resourceCompiled{},declarationSnapshotReady{};
    std::unique_ptr<paired_cpu_frontier::Plan> coupledSchedule;
    resource_uses::Snapshot resourceUses;
    std::unique_ptr<resource_replay::Receipt> resourceReplay;
    std::vector<std::array<uint32_t,8>> resourceReplayRows;
    uint32_t resourceReplayFailure{},resourceReplaySlot{},resourceReplayCursor{},resourceReplayMisses{};
    bool resourceReplayCompilerChecked{},resourceReplayTerminalChecked{};
    bool resourceReplayResetSealed{},resourceReplayResetReturned{};
};
auto& publishedReplayRows=*new std::vector<std::array<uint32_t,8>>;
auto& publishedScheduleSource=*new graph_snapshot::Snapshot;
auto& publishedScheduleBindings=*new std::vector<graph_snapshot::Node>;
struct StageDefinitionRow {
    uint64_t object{},rootHandle{},view{},first{},count{},vtable{},work{},rootObject{};
    std::array<uint8_t,64> header{};
};
static_assert(sizeof(StageDefinitionRow)==128);
auto& publishedStageDefinitions=*new std::vector<StageDefinitionRow>;
auto& publishedStageEntries=*new std::vector<std::array<uint8_t,48>>;
struct SceneRootRow {
    uint64_t object{},handle{},view{},first{},count{},split{},reserved{};
    scene_sections::Header header{};
};
static_assert(sizeof(SceneRootRow)==112);
auto& publishedSceneRoots=*new std::vector<SceneRootRow>;
auto& publishedSceneChildren=*new std::vector<scene_sections::Child>;
auto& publishedSplitSource=*new graph_snapshot::Snapshot;
auto& publishedSplitMapping=*new std::vector<scene_expansion::Mapping>;
static_assert(sizeof(scene_expansion::Mapping)==12);
struct ReplayNode {
    std::shared_ptr<Consumer> consumer;
    resource_replay::Identity identity;
    resource_replay::NodeBinding node;
};
thread_local std::array<ReplayNode,32> replayNodes;
thread_local uint32_t replayNodeDepth{};
thread_local std::array<std::shared_ptr<Consumer>,32> replayQueues;
thread_local uint32_t replayQueueDepth{};
struct ReplayPayload {ReplayNode node;std::array<uint8_t,40> bytes{};};
auto& replayPayloads=*new std::map<uint64_t,ReplayPayload>;
struct AsyncReplay {ReplayNode node;std::array<uint8_t,72> context{};uint32_t kind{};};
auto& asyncReplayPayloads=*new std::map<uint64_t,AsyncReplay>;
constexpr size_t MaxConsumers=256;
// Intentional non-destructing quarantine for uncertain native pins at CRT close.
auto& consumers=*new std::map<uint64_t,std::shared_ptr<Consumer>>;
std::atomic<bool> mutationOverflow{};
auto& ownedExecutionStates=*new owned_graph_execution::Ledger;
auto& retirementStates=*new retirement_deferral::Ledger;
auto& retainedOwners=*new std::array<std::unique_ptr<RetainedOwner>,retirement_deferral::MaxStates>;
uint64_t nextExtraReader{};
std::atomic<uint64_t> protectedAttachedOwners{};
std::atomic<uint64_t> ownedAdmissionWindows{};
std::atomic<uint64_t> allocatorServicesActive{};
std::atomic<bool> allocatorReadyObserved{};
std::array<std::atomic<uint64_t>,6> lastProtectionIdentity{};
std::atomic<uint64_t> lastProtectionSeq{};
struct OwnedAdmissionWindow {
    OwnedAdmissionWindow():active(CyberpunkVR_OwnedExecutionMode.load()==1 && CyberpunkVR_OwnedExecutionRequest.load()!=0){if(active)ownedAdmissionWindows.fetch_add(1);}
    ~OwnedAdmissionWindow(){if(active)ownedAdmissionWindows.fetch_sub(1);}
    bool active{};
};
thread_local uint32_t retirementBypass{};
thread_local RetainedOwner* retirementCallOwner{};
thread_local void* retirementCallHeader{};
thread_local uint64_t retirementCallScope{};
thread_local uint32_t ownedTelemetryDepth{};
SRWLOCK ownedTelemetryLock=SRWLOCK_INIT;
struct OwnedTelemetryWrite {
    OwnedTelemetryWrite(){if(!ownedTelemetryDepth++){AcquireSRWLockExclusive(&ownedTelemetryLock);CyberpunkVR_OwnedExecutionSeq.fetch_add(1,std::memory_order_acq_rel);}}
    ~OwnedTelemetryWrite(){if(!--ownedTelemetryDepth){CyberpunkVR_OwnedExecutionSeq.fetch_add(1,std::memory_order_release);ReleaseSRWLockExclusive(&ownedTelemetryLock);}}
};
enum class OwnedFailure:uint32_t {
    None,Mode,Admission,Capacity,WriterCoverage,Code,Stack,Source,Tree,Sort,Owners,Reservation,
    Ticket,Declaration,Terminal,Publication,Rollback,Construct,Queue,Execution,Producer,Completion,Resource,RetirementInvariant,AllocatorBinding,SceneExpansion
};
struct AllocatorDomain {
    uint64_t incarnation{},resetEpoch{},servicesActive{},closeResetEvents{};
    uint32_t inFlight{},lastEntry{};
    bool ready{},closing{};
    bool serviceCertificateObserved{};
    allocator_binding::Receipt binding;
};
auto& allocatorDomain=*new AllocatorDomain;
bool AllocatorCoverageReady();
bool CaptureAllocatorLocked(allocator_binding::Receipt&);
void PublishAllocatorReceipt(const allocator_binding::Receipt* receipt=nullptr,uint64_t treeNodes=0);
enum class RetireEvent:uint64_t {Reserve=1,Admit,Complete,Cancel,Intent,Detach,DestroyBegin,DestroyReturn,Recycle,Abandon,Quarantine,AllocatorBound,AllocatorClose,AllocatorReset};
void RetirementEvent(RetireEvent kind,const RetainedOwner* owner=nullptr,uint64_t failure=0,uint64_t currentHeader=0,
    uint64_t currentStorage=0,uint64_t currentEpoch=0,uint64_t caller=0,uint64_t consumer=0,uint64_t epoch=0,uint64_t pair=0){
    OwnedTelemetryWrite write;const auto sequence=++CyberpunkVR_OwnedExecutionRetirementEventSeq;
    auto* row=CyberpunkVR_OwnedExecutionRetirementEvents[(sequence-1)%32];std::fill_n(row,24,0ull);row[0]=sequence;row[1]=uint64_t(kind);
    if(owner){const auto& id=owner->identity;row[2]=id.ownerHeader;row[3]=id.generation;row[4]=id.bornEpoch;
        row[5]=id.savedNative16B.data;row[6]=id.savedNative16B.capacity;row[7]=id.savedNative16B.count;
        row[8]=owner->lastConsumerId;row[9]=owner->lastEpoch;row[10]=owner->lastPairGeneration;
        row[16]=owner->declarationGroup;row[17]=owner->executionGroup;row[18]=owner->compilerGroup;
        row[19]=owner->completedReadersReceipt;row[20]=owner->state?owner->state->PendingReaders():0;
        row[21]=owner->completionFlags<<32;row[23]=owner->allocatorBinding.Allocations().size();}
    if(consumer){row[8]=consumer;row[9]=epoch;row[10]=pair;}
    row[11]=allocatorDomain.incarnation;row[12]=allocatorDomain.resetEpoch;row[13]=currentHeader;row[14]=currentStorage;
    row[15]=currentEpoch;row[21]|=failure;row[22]=caller;
    if(owner && !currentHeader && kind==RetireEvent::Recycle){row[13]=owner->serviceHeader;row[14]=owner->serviceStorage;
        row[15]=owner->serviceEpoch;row[22]=owner->serviceCaller;}
    CyberpunkVR_OwnedExecutionRetirementEventCount=std::min(sequence,uint64_t(32));
}
bool Read(uint64_t address,void* out,size_t size);
bool SceneCodeCompatible();
void OwnedReject(OwnedFailure reason,uint64_t detail=0){
    OwnedTelemetryWrite write;
    auto* stats=CyberpunkVR_OwnedExecutionStats;stats[0]=5;stats[22]=uint32_t(reason);stats[23]=detail;
}
void PublishCompilerReceipt(const Consumer& consumer){
    if(!consumer.compiler)return;OwnedTelemetryWrite write;const auto& lease=*consumer.compiler;const auto& binding=lease.Binding();
    const uint64_t fields[]{binding.consumerId,lease.Group(),lease.Captured(),lease.Matched(),lease.OriginalDispatchReturned(),
        lease.OriginalRootReturned(),lease.OriginalCompilerReturned(),!CyberpunkVR_OwnedExecutionClosing.load() && lease.Complete(),uint32_t(lease.Rejection()),
        binding.renderer,binding.wrapper,binding.pool,binding.frameOwner,binding.executionGraph,binding.epoch,binding.generation};
    std::copy_n(fields,16,CyberpunkVR_OwnedExecutionCompiler);
}
void PublishRetirementReceipt(){
    OwnedTelemetryWrite write;auto* fields=CyberpunkVR_OwnedExecutionRetirement;
    std::fill_n(fields,8,0ull);fields[0]=allocatorDomain.serviceCertificateObserved && allocatorDomain.ready && !allocatorDomain.closing &&
        !CyberpunkVR_OwnedExecutionClosing.load() && AllocatorCoverageReady();
    fields[1]=retirementStates.Size();fields[2]=retirementStates.OwnerCount();
    for(const auto& owner:retainedOwners)if(owner && owner->state){fields[3]+=owner->state->PendingReaders();
        fields[4]+=owner->state->ExactHeaderDetached() || owner->emergencyDetached;
        fields[5]+=owner->state->DestroyAttemptConsumed();fields[6]+=owner->state->DestroyReturned();}
    fields[7]=CyberpunkVR_OwnedExecutionClosing.load();
}
bool OwnedWriterCoverageReady();
bool NativeReferenceSort(void* graph){
    __try{reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x985D0C)(graph);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CompareGraphSlot(uint64_t slot,uint64_t expected,uint64_t desired,uint64_t& observed){
    __try{observed=uint64_t(InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(uintptr_t(slot)),LONG64(desired),LONG64(expected)));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uint64_t EarlyExecutionGroup(uint64_t graphSlot){
    uint64_t group{},again{};int32_t refs{},pending{};
    if(graphSlot>UINT64_MAX-0xF8 || !Read(graphSlot+0xF0,&group,8) || !Read(graphSlot+0xF0,&again,8) ||
        !group || group!=again || (group&7) || group>UINT64_MAX-0x20 ||
        !Read(group+0x1c,&refs,4) || !Read(group+0x18,&pending,4) || refs<=0 || refs==INT32_MAX || pending<0)return 0;
    return group;
}
bool TopVectorMatches(const graph_definition::Owner& owner){
    const auto& native=owner.move.SavedHeader();const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    uint64_t allocator{},callback{},again{};
    if(owner.move.EmptyHeader().data!=base+0x2B39FA0 || !Read(base+0x2B39FC0,&callback,8) || callback!=base+0x985ABC)return false;
    if(native.capacity && (!Read(owner.move.TrailerAddress(),&allocator,8) || !Read(owner.move.TrailerAddress(),&again,8) ||
        allocator!=owner.move.EmptyHeader().data || allocator!=again))return false;
    return owner.move.MatchesBefore(Read);
}
RetainedOwner* FindRetainedOwner(const retirement_deferral::HeaderIdentity& identity){
    for(const auto& owner:retainedOwners)if(owner && owner->identity==identity)return owner.get();return nullptr;
}
void CancelExtraReaders(Consumer& consumer,bool restored){
    std::erase_if(consumer.extraReaders,[&](const ExtraReader& reader){
        const bool canceled=reader.owner->state->CancelUnescapedExtraReader({reader.identity,true,restored,true});
        if(canceled){OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[3];RetirementEvent(RetireEvent::Cancel,reader.owner);}
        return canceled;});
}
bool ConsumerReferencesOwner(const RetainedOwner* owner){
    for(const auto& [group,consumer]:consumers)for(const auto& reader:consumer->extraReaders)if(reader.owner==owner)return true;
    return false;
}
void RecycleRetainedMetadata(){
    for(auto& owner:retainedOwners)if(owner && owner->state && !owner->destroyInFlight && !ConsumerReferencesOwner(owner.get())){
        const auto identity=owner->identity;
        const bool destroyed=owner->state->CanRetireMetadata();
        if(!destroyed && !owner->state->CanAbandonUnusedReservation())continue;
        const bool retired=destroyed?retirementStates.RetireMetadata(identity.ownerHeader,identity.generation,identity.bornEpoch):
            retirementStates.AbandonUnusedReservation(identity.ownerHeader,identity.generation,identity.bornEpoch);
        if(retired){OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[destroyed?8:9];
            owner->state=nullptr;RetirementEvent(destroyed?RetireEvent::Recycle:RetireEvent::Abandon,owner.get());
            if(owner->protectedAttached)protectedAttachedOwners.fetch_sub(1);owner.reset();}
    }
}
bool HasSceneExpansion(const Consumer& consumer){return bool(consumer.sceneExpansion) || bool(consumer.sceneBothExpansion);}
bool JoinSceneReader(Consumer& consumer,const scene_sections::Snapshot& source,borrowed_scene_sections::CallerLifetime& binding){
    binding={};if(!HasSceneExpansion(consumer) || !source.Ready())return false;
    const auto& sourceRoot=source.RootIdentity();
    const auto object=graph_snapshot::Field<uint64_t>(sourceRoot.bytes,16);
    const auto mapped=std::find_if(consumer.originalSource.Mappings().begin(),consumer.originalSource.Mappings().end(),[&](const auto& value){
        return value.nodeHandle==sourceRoot.handle && value.object==object;
    });
    if(mapped==consumer.originalSource.Mappings().end() || mapped->ownerIndex>=consumer.originalSource.Owners().size())return false;
    const auto header=consumer.originalSource.Owners()[mapped->ownerIndex].move.OwnerAddress();
    uint32_t matches{};
    for(const auto& reader:consumer.extraReaders){const auto& owner=*reader.owner;
        if(owner.identity.ownerHeader!=header)continue;
        if(!owner.state || !owner.state->HasPendingExtraReader(reader.identity) ||
            reader.identity.consumerId!=consumer.ticket->Id() || reader.identity.epoch!=consumer.ticket->Epoch() ||
            reader.identity.pairGeneration!=consumer.ticket->Generation() || reader.identity.sourceGraph!=consumer.ticket->SourceGraphAddress() ||
            reader.identity.executionGraph!=consumer.ticket->GraphAddress() ||
            std::find(owner.definitions.begin(),owner.definitions.end(),object)==owner.definitions.end())return false;
        binding={reader.identity.consumerId,reader.identity.epoch,reader.identity.pairGeneration,object,
            owner.identity.ownerHeader,owner.identity.generation,owner.identity.bornEpoch,reader.identity.leaseId};++matches;
    }
    return matches==1;
}
bool ValidateSceneViews(Consumer& consumer){
    if(!HasSceneExpansion(consumer))return true;
    if(!SceneCodeCompatible())return false;
    const auto validate=[&](const auto& source,const auto& views,const auto& lifetime){
        borrowed_scene_sections::CallerLifetime current;
        return JoinSceneReader(consumer,source,current) && current==lifetime && views.Matches(source,current) && source.MatchesBefore(Read);
    };
    if(consumer.sceneBothExpansion){
        for(uint32_t side=0;side<2;++side)
            if(!validate(consumer.sceneBothSources[side],consumer.sceneBothViews[side],consumer.sceneBothLifetimes[side]))return false;
        return true;
    }
    return validate(consumer.sceneSplitSource,consumer.sceneViews,consumer.sceneLifetime);
}
bool ReserveExtraOwners(Consumer& consumer,uint64_t graphSlot){
    RecycleRetainedMetadata();
    if(CyberpunkVR_OwnedExecutionCache[6] && CyberpunkVR_OwnedExecutionCache[6]!=consumer.originalSource.CacheBase()){
        OwnedReject(OwnedFailure::Admission,consumer.originalSource.CacheBase());return false;}
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    std::set<uint32_t> referenced;for(const auto& mapping:consumer.originalSource.Mappings())referenced.insert(mapping.ownerIndex);
    std::vector<RetainedOwner*> pending;pending.reserve(referenced.size());
    for(const auto index:referenced){
        const auto& sourceOwner=consumer.originalSource.Owners().at(index);const auto header=sourceOwner.move.OwnerAddress();
        const auto found=mutations.find(header);
        if(found==mutations.end() || !found->second.storageAlive || !found->second.storage || !found->second.generation || !found->second.builderClosed || found->second.inFlight ||
            found->second.builders || found->second.operations || !TopVectorMatches(sourceOwner)){OwnedReject(OwnedFailure::Owners,header);return false;}
        const auto& saved=sourceOwner.move.SavedHeader();const auto& empty=sourceOwner.move.EmptyHeader();
        const retirement_deferral::HeaderIdentity identity{header,found->second.generation,found->second.epoch,
            {saved.data,saved.capacity,saved.count},{empty.data,empty.capacity,empty.count}};
        auto* retained=FindRetainedOwner(identity);
        if(!retained){
            auto target=std::find_if(retainedOwners.begin(),retainedOwners.end(),[](const auto& owner){return !owner;});
            if(target==retainedOwners.end()){OwnedReject(OwnedFailure::Reservation);return false;}
            auto created=std::make_unique<RetainedOwner>();created->identity=identity;created->definitions=sourceOwner.move.Definitions();
            if(!created->tree.Capture(created->definitions,base,Read,true) || !owned_execution::TreeCertified(created->tree) ||
                !created->tree.MatchesBefore(Read)){OwnedReject(OwnedFailure::Tree,header);return false;}
            if(!CaptureAllocatorLocked(created->allocatorBinding) || !created->allocatorBinding.BindSaved({saved.data,saved.capacity,saved.count},created->tree)){
                PublishAllocatorReceipt(&created->allocatorBinding,created->tree.Nodes().size());OwnedReject(OwnedFailure::AllocatorBinding,uint32_t(created->allocatorBinding.Rejection()));return false;}
            created->state=retirementStates.Reserve(identity,{true,true,true},{{empty.data,1},true});
            if(!created->state){OwnedReject(OwnedFailure::Reservation,uint32_t(retirementStates.Rejection()));return false;}
            retained=created.get();*target=std::move(created);{OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[0];
                RetirementEvent(RetireEvent::Reserve,retained,0,0,0,0,0,consumer.ticket->Id(),consumer.ticket->Epoch(),consumer.ticket->Generation());}
        }
        if(!retained->state->AcquisitionsOpen() || retained->state->PendingReaders()>=retirement_deferral::MaxExtraReaders ||
            !retained->tree.MatchesBefore(Read)){OwnedReject(OwnedFailure::Reservation,header);return false;}
        pending.push_back(retained);
    }
    consumer.extraReaders.reserve(pending.size());
    for(auto* owner:pending){
        if(nextExtraReader==UINT64_MAX){CancelExtraReaders(consumer,true);OwnedReject(OwnedFailure::Reservation);return false;}
        retirement_deferral::ExtraReaderIdentity reader{++nextExtraReader,consumer.ticket->Id(),consumer.ticket->Epoch(),consumer.ticket->Generation(),
            consumer.ticket->SourceGraphAddress(),consumer.ticket->GraphAddress()};
        if(!owner->state->AdmitExtraReader(reader)){CancelExtraReaders(consumer,true);OwnedReject(OwnedFailure::Reservation);return false;}
        owner->lastConsumerId=reader.consumerId;owner->lastEpoch=reader.epoch;owner->lastPairGeneration=reader.pairGeneration;
        owner->completionFlags=owner->declarationGroup=owner->executionGroup=owner->compilerGroup=0;
        {OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[1];RetirementEvent(RetireEvent::Admit,owner);}
        if(!owner->protectedAttached){owner->protectedAttached=true;protectedAttachedOwners.fetch_add(1);}
        const uint64_t protection[]{owner->identity.ownerHeader,owner->identity.generation,owner->identity.bornEpoch,
            reader.consumerId,reader.epoch,reader.pairGeneration};
        lastProtectionSeq.fetch_add(1);for(size_t i=0;i<std::size(protection);++i)lastProtectionIdentity[i].store(protection[i]);lastProtectionSeq.fetch_add(1);
        consumer.extraReaders.push_back({owner,reader});
    }
    std::vector<owned_graph_execution::OwnerGenerationBinding> bindings;bindings.reserve(consumer.extraReaders.size());
    for(const auto& reader:consumer.extraReaders)bindings.push_back({reader.owner->identity.ownerHeader,reader.owner->identity.generation,
        reader.owner->identity.bornEpoch,reader.identity.leaseId});
    const owned_graph_execution::Identity identity{consumer.ticket->Id(),consumer.ticket->Epoch(),consumer.ticket->Generation(),
        consumer.ticket->SourceGraphAddress(),consumer.ticket->GraphAddress(),consumer.ticket->FrameOwner(),consumer.ticket->Group()};
    if(HasSceneExpansion(consumer)){
        const bool code=SceneCodeCompatible();
        const bool contextReady=context_order::Analyze(consumer.executionGraph,base,Read).PrefixReady();
        bool bound=code && contextReady;
        if(bound && consumer.sceneBothExpansion){
            for(uint32_t side=0;side<2 && bound;++side)
                bound=JoinSceneReader(consumer,consumer.sceneBothSources[side],consumer.sceneBothLifetimes[side]) &&
                    consumer.sceneBothViews[side].Bind(consumer.sceneBothSources[side],consumer.sceneBothLifetimes[side]);
        }else if(bound)bound=JoinSceneReader(consumer,consumer.sceneSplitSource,consumer.sceneLifetime) &&
            consumer.sceneViews.Bind(consumer.sceneSplitSource,consumer.sceneLifetime);
        if(!bound || !ValidateSceneViews(consumer)){
            CyberpunkVR_OwnedSceneSplitStats[22]=7;CancelExtraReaders(consumer,true);OwnedReject(OwnedFailure::SceneExpansion,7);return false;
        }
        CyberpunkVR_OwnedSceneSplitStats[16]=1;
        const bool protectedDefinitions=!consumer.extraReaders.empty() && std::all_of(consumer.extraReaders.begin(),consumer.extraReaders.end(),[](const auto& reader){
            return reader.owner && reader.owner->state && reader.owner->state->HasPendingExtraReader(reader.identity);
        });
        const owned_graph_execution::ExpansionAdmissionFacts facts{
            consumer.sourceVerified && !consumer.generationRejected,protectedDefinitions,code,contextReady};
        if(consumer.sceneBothExpansion)consumer.ownedState=ownedExecutionStates.AdmitExpanded(identity,bindings,*consumer.sceneBothExpansion,
            consumer.sceneBothViews,consumer.sceneBothSources,consumer.sceneBothLifetimes,consumer.executionGraph,base,facts,consumer.producerScope,graphSlot);
        else consumer.ownedState=ownedExecutionStates.AdmitExpanded(identity,bindings,*consumer.sceneExpansion,consumer.sceneViews,
            consumer.sceneSplitSource,consumer.sceneLifetime,consumer.executionGraph,base,facts,consumer.producerScope,graphSlot);
    }else consumer.ownedState=ownedExecutionStates.Admit(identity,bindings,{true,true,true},consumer.producerScope,graphSlot);
    if(!consumer.ownedState){CancelExtraReaders(consumer,true);OwnedReject(OwnedFailure::Reservation);return false;}
    {OwnedTelemetryWrite write;if(!CyberpunkVR_OwnedExecutionCache[6])CyberpunkVR_OwnedExecutionCache[6]=consumer.originalSource.CacheBase();
        const uint64_t binding[]{consumer.ticket->Id(),consumer.ticket->Epoch(),consumer.ticket->Generation(),consumer.ticket->SourceGraphAddress(),
            consumer.originalSource.CacheBase(),consumer.renderer};std::copy_n(binding,6,CyberpunkVR_OwnedExecutionAdmission);}
    PublishRetirementReceipt();return true;
}
void CaptureSceneSections(Consumer& consumer,const RootAdmission& admission,const paired_frontier::Plan& frontier)noexcept{
    auto* stats=CyberpunkVR_OwnedSceneStats;
    stats[0]=1;stats[1]=2;stats[2]=admission.id;stats[3]=admission.epoch;stats[4]=admission.pairGeneration;stats[7]=1;
    try{
        std::vector<SceneRootRow> roots;
        std::vector<scene_sections::Child> children;
        std::array<scene_sections::Snapshot,2> snapshots;
        for(uint32_t side=0;side<2;++side){
            const auto id=frontier.EyeRoles()[side].discard;
            const auto found=std::find_if(consumer.originalGraph.nodes.begin(),consumer.originalGraph.nodes.end(),[&](const auto& node){
                return (node.handle&0xDFFFFFFFu)==id && graph_snapshot::Field<uint32_t>(node.bytes,24)==side;
            });
            if(found==consumer.originalGraph.nodes.end()){stats[7]=2;return;}
            auto& snapshot=snapshots[side];
            if(!snapshot.Capture(*found,consumer.originalTree,Read)){stats[7]=0x100+uint32_t(snapshot.Rejection());return;}
            roots.push_back({graph_snapshot::Field<uint64_t>(found->bytes,16),found->handle,side,children.size(),
                snapshot.Children().size(),snapshot.SplitIndex(),0,snapshot.RawHeader()});
            children.insert(children.end(),snapshot.Children().begin(),snapshot.Children().end());
        }
        consumer.sceneSections=std::move(snapshots);
        publishedSceneRoots=std::move(roots);publishedSceneChildren=std::move(children);
        CyberpunkVR_OwnedSceneRoots=reinterpret_cast<uintptr_t>(publishedSceneRoots.data());
        CyberpunkVR_OwnedSceneChildren=reinterpret_cast<uintptr_t>(publishedSceneChildren.data());
        stats[5]=publishedSceneRoots.size();stats[6]=publishedSceneChildren.size();stats[7]=0;stats[1]=1;
    }catch(...){stats[7]=3;}
}
void CaptureDiscardStages(const Consumer& consumer,const RootAdmission& admission,const paired_frontier::Plan& frontier)noexcept{
    // Numeric observation only. No cloned definitions or execution permissions.
    auto* stats=CyberpunkVR_OwnedStageStats;
    stats[0]=1;stats[1]=2;stats[2]=admission.id;stats[3]=admission.epoch;stats[4]=admission.pairGeneration;
    stats[7]=1;
    try{
        std::vector<StageDefinitionRow> definitions;
        std::vector<std::array<uint8_t,48>> stageEntries;
        for(uint32_t side=0;side<2;++side){
            const auto id=frontier.EyeRoles()[side].discard;
            const auto found=std::find_if(consumer.originalGraph.nodes.begin(),consumer.originalGraph.nodes.end(),[&](const auto& node){
                return (node.handle&0xDFFFFFFFu)==id && graph_snapshot::Field<uint32_t>(node.bytes,24)==side;
            });
            if(found==consumer.originalGraph.nodes.end()){stats[7]=2;return;}
            const auto before=definitions.size();
            for(uint32_t index=0;index<consumer.originalTree.Nodes().size();++index){
                const auto& child=consumer.originalTree.Nodes()[index];
                if(child.kind!=definition_tree::Kind::RenderElements ||
                    !stage_definition::Snapshot::Reachable(*found,consumer.originalTree,index))continue;
                stage_definition::Snapshot snapshot;
                if(!snapshot.Capture(*found,consumer.originalTree,index,Read)){
                    stats[7]=0x100+uint32_t(snapshot.Rejection());return;
                }
                if(definitions.size()>=220 || snapshot.Stages().size()>8192-stageEntries.size()){stats[7]=3;return;}
                definitions.push_back({child.object,found->handle,side,stageEntries.size(),snapshot.Stages().size(),
                    child.vtable,child.work,graph_snapshot::Field<uint64_t>(found->bytes,16),snapshot.RawHeader()});
                stageEntries.insert(stageEntries.end(),snapshot.Stages().begin(),snapshot.Stages().end());
            }
            if(definitions.size()==before){stats[7]=4;return;}
        }
        publishedStageDefinitions=std::move(definitions);publishedStageEntries=std::move(stageEntries);
        CyberpunkVR_OwnedStageDefinitions=reinterpret_cast<uintptr_t>(publishedStageDefinitions.data());
        CyberpunkVR_OwnedStageEntries=reinterpret_cast<uintptr_t>(publishedStageEntries.data());
        stats[5]=publishedStageDefinitions.size();stats[6]=publishedStageEntries.size();stats[7]=0;stats[1]=1;
    }catch(...){stats[7]=5;}
}
bool PrepareSceneExpansion(Consumer& consumer,const RootAdmission& admission,uint32_t request,uint32_t mode){
    auto* split=CyberpunkVR_OwnedSceneSplitStats;
    split[0]=mode;split[1]=1;split[2]=admission.id;split[3]=admission.epoch;split[4]=admission.pairGeneration;split[5]=mode;
    const auto reject=[&](uint64_t reason){split[22]=reason;OwnedReject(OwnedFailure::SceneExpansion,reason);return false;};
    if(request!=2 || !consumer.coupledSchedule || (mode!=1 && mode!=2))return reject(1);
    if(!SceneCodeCompatible())return reject(2);split[17]=1;
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    consumer.coupledGraph=consumer.executionGraph;
    const auto capture=[&](uint32_t side,auto& source,auto& views){
        const auto found=std::find_if(consumer.coupledGraph.nodes.begin(),consumer.coupledGraph.nodes.end(),[&](const auto& node){
            return graph_snapshot::Field<uint32_t>(node.bytes,24)==side && node.work==base+0x1EC1D0 &&
                graph_snapshot::Field<uint64_t>(node.definition,24)==paired_frontier::Name("GBuffer_Discard");
        });
        return found!=consumer.coupledGraph.nodes.end() && source.Capture(*found,consumer.originalTree,Read) && views.Prepare(source);
    };
    if(mode==2){
        std::array<std::array<scene_expansion::Definition,2>,2> definitions;
        for(uint32_t side=0;side<2;++side){
            if(!capture(side,consumer.sceneBothSources[side],consumer.sceneBothViews[side]))return reject(3);
            definitions[side]=consumer.sceneBothViews[side].Definitions();
        }
        consumer.sceneBothExpansion=std::make_unique<scene_expansion::BothEyesPlan>();
        if(!consumer.sceneBothExpansion->Build(consumer.coupledGraph,base,definitions))return reject(4);
        for(uint32_t row=0;row<2;++row){const auto side=1-row;
            const uint64_t fields[]{side,consumer.sceneBothExpansion->PrefixHandle(side),consumer.sceneBothExpansion->TailHandle(side),
                graph_snapshot::Field<uint64_t>(consumer.sceneBothSources[side].RootIdentity().bytes,16),definitions[side][0].object,definitions[side][1].object};
            std::copy_n(fields,6,CyberpunkVR_OwnedSceneSplitSections+row*6);
        }
    }else{
        if(!capture(1,consumer.sceneSplitSource,consumer.sceneViews))return reject(3);
        consumer.sceneExpansion=std::make_unique<scene_expansion::Plan>();
        const auto definitions=consumer.sceneViews.Definitions();
        if(!consumer.sceneExpansion->Build(consumer.coupledGraph,base,consumer.sceneSplitSource.RootIdentity().handle,definitions))return reject(4);
        split[10]=consumer.sceneExpansion->PrefixHandle();split[11]=consumer.sceneExpansion->TailHandle();
        split[12]=graph_snapshot::Field<uint64_t>(consumer.sceneSplitSource.RootIdentity().bytes,16);
        split[13]=definitions[0].object;split[14]=definitions[1].object;
    }
    auto expanded=std::make_unique<OwnedNativeGraph>();
    const auto compile=[&](const auto& plan){
        return expanded->Create(plan.Expanded(),plan.Edges()) && NativeReferenceSort(expanded->SortableData()) &&
            expanded->ValidateCompiled() && plan.MatchesCompiled(expanded->Snapshot());
    };
    if(!(mode==2?compile(*consumer.sceneBothExpansion):compile(*consumer.sceneExpansion)))return reject(5);
    const auto compiled=expanded->Snapshot();const auto context=context_order::Analyze(compiled,base,Read);
    if(!context.PrefixReady())return reject(6);
    if(mode==2){for(const auto& source:consumer.sceneBothSources)if(!source.MatchesBefore(Read))return reject(6);}
    else if(!consumer.sceneSplitSource.MatchesBefore(Read))return reject(6);
    consumer.executionGraph=compiled;consumer.ownedGraph=std::move(expanded);
    split[6]=consumer.coupledGraph.nodes.size();split[7]=consumer.coupledGraph.edges.size();
    split[8]=compiled.nodes.size();split[9]=compiled.edges.size();
    split[15]=1;split[18]=context.requiredPairs;split[19]=context.provedPairs;
    publishedSplitSource=consumer.coupledGraph;
    publishedSplitMapping=mode==2?consumer.sceneBothExpansion->Mappings():consumer.sceneExpansion->Mappings();
    CyberpunkVR_OwnedSceneSplitSourceNodes=reinterpret_cast<uintptr_t>(publishedSplitSource.nodes.data());
    CyberpunkVR_OwnedSceneSplitSourceEdges=reinterpret_cast<uintptr_t>(publishedSplitSource.edges.data());
    CyberpunkVR_OwnedSceneSplitMappingRows=reinterpret_cast<uintptr_t>(publishedSplitMapping.data());
    auto* schedule=CyberpunkVR_OwnedExecutionSchedule;
    schedule[3]=paired_frontier::detail::Fingerprint(compiled,base);schedule[6]=compiled.edges.size();schedule[7]=schedule[8]=0;
    for(size_t i=0;i<consumer.originalGraph.nodes.size();++i)for(UINT phase=0;phase<2;++phase)
        schedule[7+phase]+=consumer.originalGraph.nodes[i].bytes[28+phase]!=compiled.nodes[i].bytes[28+phase];
    schedule[9]=context.requiredPairs;schedule[10]=context.provedPairs;schedule[11]=context.missing.size();
    return true;
}
bool PrepareOwnedReference(Consumer& consumer,RootAdmission& admission,uint64_t destination,uint64_t ownerAddress,
                           uint64_t graphSlot,uint64_t completionAddress,uint64_t returnSlot,uint64_t caller){
    if(CyberpunkVR_OwnedExecutionMode.load()!=1 || !preparation::Active() || CyberpunkVR_PreparationPairState.load()!=2 ||
        !admission.registered || !admission.epoch || !admission.pairGeneration)return false;
    const auto request=CyberpunkVR_OwnedExecutionRequest.exchange(0);if(request!=1 && request!=2)return false;
    OwnedTelemetryWrite write;
    auto* stats=CyberpunkVR_OwnedExecutionStats;std::fill_n(stats,24,0ull);std::fill_n(CyberpunkVR_OwnedExecutionResources,12,0ull);
    std::fill_n(CyberpunkVR_OwnedExecutionCompiler,16,0ull);
    std::fill_n(CyberpunkVR_OwnedExecutionSchedule,16,0ull);CyberpunkVR_OwnedExecutionSchedule[0]=request;
    publishedScheduleSource={};CyberpunkVR_OwnedExecutionScheduleNodes=CyberpunkVR_OwnedExecutionScheduleEdges=0;
    publishedScheduleBindings.clear();CyberpunkVR_OwnedExecutionScheduleBindings=0;
    publishedStageDefinitions.clear();publishedStageEntries.clear();
    CyberpunkVR_OwnedStageDefinitions=CyberpunkVR_OwnedStageEntries=0;
    std::fill_n(CyberpunkVR_OwnedStageStats,8,0ull);
    publishedSceneRoots.clear();publishedSceneChildren.clear();
    CyberpunkVR_OwnedSceneRoots=CyberpunkVR_OwnedSceneChildren=0;
    std::fill_n(CyberpunkVR_OwnedSceneStats,8,0ull);
    std::fill_n(CyberpunkVR_OwnedSceneSplitStats,24,0ull);CyberpunkVR_OwnedSceneSplitStats[0]=1;
    std::fill_n(CyberpunkVR_OwnedSceneSplitSections,12,0ull);
    publishedSplitSource={};publishedSplitMapping.clear();
    CyberpunkVR_OwnedSceneSplitSourceNodes=CyberpunkVR_OwnedSceneSplitSourceEdges=CyberpunkVR_OwnedSceneSplitMappingRows=0;
    stats[0]=1;stats[1]=admission.id;stats[2]=admission.epoch;stats[3]=admission.pairGeneration;
    if(CyberpunkVR_OwnedExecutionClosing.load() || !admission.registered || !admission.epoch || !admission.pairGeneration){OwnedReject(OwnedFailure::Admission);return false;}
    if(!OwnedWriterCoverageReady()){OwnedReject(OwnedFailure::WriterCoverage);return false;}
    for(const auto& [group,pending]:consumers)if(pending->ownedState){
        OwnedReject(OwnedFailure::Capacity);return false;}
    if(returnSlot>UINT64_MAX-0x60 || graphSlot!=returnSlot+0x60 || destination!=graphSlot+0x28 ||
        completionAddress!=graphSlot+0x180 || (graphSlot&7)){OwnedReject(OwnedFailure::Stack);return false;}
    uint64_t sourceGraph{},cache{},frame{};const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    if(!Read(graphSlot,&sourceGraph,8) || !Read(ownerAddress,&frame,8) || !Read(admission.renderer+0x4620,&cache,8) ||
        !consumer.originalGraph.Capture(sourceGraph,Read) ||
        !consumer.originalSource.Capture(cache,sourceGraph,consumer.originalGraph,base,Read,
            graph_definition::CacheUsePolicy::AllowConcurrentHits,graph_definition::DefinitionContentPolicy::OwnerMembership)){
        OwnedReject(OwnedFailure::Source);return false;}
    paired_frontier::Plan frontier;
    if(frontier.BuildCandidate(consumer.originalGraph,base,request==2?paired_frontier::Boundary::Discard:paired_frontier::Boundary::Solid)!=
        paired_frontier::Result::CandidateNeedsCpuAndResourceCompile){OwnedReject(OwnedFailure::Source);return false;}
    auto edges=frontier.Original();
    if(request==2){consumer.coupledSchedule=std::make_unique<paired_cpu_frontier::Plan>();
        if(consumer.coupledSchedule->BuildCandidate(frontier)!=paired_cpu_frontier::Result::CandidateNeedsOwnershipAndResourceCompile ||
            !consumer.coupledSchedule->Proofs().sharedOwnerOrderPreserved){OwnedReject(OwnedFailure::Sort);return false;}
        edges=consumer.coupledSchedule->Proposed();}
    consumer.ownedGraph=std::make_unique<OwnedNativeGraph>();
    if(!consumer.ownedGraph->Create(consumer.originalGraph,edges) || !NativeReferenceSort(consumer.ownedGraph->SortableData()) ||
        !consumer.ownedGraph->ValidateCompiled() || (request==1 && !consumer.ownedGraph->SameSourceOrders())){OwnedReject(OwnedFailure::Sort);return false;}
    consumer.executionGraph=consumer.ownedGraph->Snapshot();
    const auto context=context_order::Analyze(consumer.executionGraph,base,Read);
    if(!context.PrefixReady()){OwnedReject(OwnedFailure::Sort);return false;}
    auto* schedule=CyberpunkVR_OwnedExecutionSchedule;
    schedule[1]=admission.id;schedule[2]=frontier.SourceFingerprint();schedule[3]=paired_frontier::detail::Fingerprint(consumer.executionGraph,base);
    schedule[4]=consumer.originalGraph.nodes.size();schedule[5]=consumer.originalGraph.edges.size();schedule[6]=consumer.executionGraph.edges.size();
    for(size_t i=0;i<consumer.originalGraph.nodes.size();++i)for(UINT phase=0;phase<2;++phase)schedule[7+phase]+=consumer.originalGraph.nodes[i].bytes[28+phase]!=consumer.executionGraph.nodes[i].bytes[28+phase];
    schedule[9]=context.requiredPairs;schedule[10]=context.provedPairs;schedule[11]=context.missing.size();
    schedule[12]=consumer.coupledSchedule?consumer.coupledSchedule->SharedOwnerOrders().size():0;
    schedule[13]=consumer.coupledSchedule?consumer.coupledSchedule->ReversedOwnerOrders().size():0;
    schedule[14]=1;
    publishedScheduleSource=consumer.originalGraph;CyberpunkVR_OwnedExecutionScheduleNodes=reinterpret_cast<uintptr_t>(publishedScheduleSource.nodes.data());
    CyberpunkVR_OwnedExecutionScheduleEdges=reinterpret_cast<uintptr_t>(publishedScheduleSource.edges.data());
    std::vector<uint64_t> roots;for(const auto& owner:consumer.originalSource.Owners())
        roots.insert(roots.end(),owner.move.Definitions().begin(),owner.move.Definitions().end());
    if(!consumer.originalTree.Capture(roots,base,Read,true) || !owned_execution::TreeCertified(consumer.originalTree) ||
        !consumer.originalTree.MatchesBefore(Read)){OwnedReject(OwnedFailure::Tree);return false;}
    CaptureDiscardStages(consumer,admission,frontier);
    CaptureSceneSections(consumer,admission,frontier);
    const auto splitMode=CyberpunkVR_OwnedSceneSplitEnable.load();
    if(splitMode>2){CyberpunkVR_OwnedSceneSplitStats[22]=1;OwnedReject(OwnedFailure::SceneExpansion,1);return false;}
    if(splitMode && !PrepareSceneExpansion(consumer,admission,request,splitMode))return false;
    const auto early=EarlyExecutionGroup(graphSlot);
    if(!early){OwnedReject(OwnedFailure::Terminal);return false;}
    consumer.executionTerminal=std::make_unique<EarlyTerminalPin>(reinterpret_cast<void*>(uintptr_t(early)));
    consumer.producerScope=rootAdmissionDepth;
    stats[4]=sourceGraph;stats[5]=reinterpret_cast<uintptr_t>(consumer.ownedGraph->SortableData());stats[6]=frame;stats[7]=early;
    stats[8]=consumer.originalGraph.nodes.size();stats[9]=consumer.originalGraph.edges.size();stats[10]=consumer.originalTree.Nodes().size();
    stats[11]=consumer.originalTree.Arrays().size();stats[12]=consumer.originalTree.Strings().size();stats[13]=frontier.SourceFingerprint();
    return true;
}
bool Read(uint64_t address,void* out,size_t size){SIZE_T done{};return address && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&done) && done==size;}
bool SceneCodeCompatible(){
    static const bool compatible=[] {
        const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
        if(!base || base>UINT64_MAX-0x3438A28)return false;
        std::array<uint8_t,1024> bytes{};
        for(const auto& code:borrowed_scene_sections::Contract){
            if(!code.bytes || code.bytes>bytes.size() || !Read(base+code.rva,bytes.data(),code.bytes))return false;
            uint64_t hash=14695981039346656037ull;
            for(uint32_t i=0;i<code.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=code.hash)return false;
        }
        for(size_t i=0;i<borrowed_scene_sections::VtableTargets.size();++i){uint64_t target{};
            if(!Read(base+scene_sections::VtableRva+i*8,&target,8) || target!=base+borrowed_scene_sections::VtableTargets[i])return false;}
        return true;
    }();
    return compatible;
}
void RetainFrame(void* owner){InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(owner)+8));}
void ReleaseFrame(void* owner){if(!CyberpunkVR_OwnedExecutionClosing.load())reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x1E6EB0)(owner);}
void RetainGroup(void* group){InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(group)+0x1C));}
void ReleaseGroup(void** group){if(!CyberpunkVR_OwnedExecutionClosing.load())reinterpret_cast<void(*)(void**)>(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x1419A4)(group);}
bool CompleteGroup(void* group){return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(group)+0x18),0,0)==0;}
bool CodeCompatible(){
    if(!GetEnvironmentVariableW(L"CVR_STEREO_PAIRED_ROOT",nullptr,0))return false;
    static const bool valid=[] {
        constexpr struct Code {uint32_t rva,size;uint64_t hash;} code[]{
            {0x983C80,0x1C7,0x2D15DBDC6AEC2159ull},
            {0x983F1C,0xBB,0x37FA227C59B2B6E6ull},{0x986E74,0x31,0x97E67EC8501772A4ull},
            {0x985294,0x5F,0x30C633724C0CACD1ull},{0xB56968,0x2D,0x3E08167273A02564ull},
            {0x1E6EB0,0x15,0xE9AA994824C7FD8Bull},{0x1419A4,0x4A,0x35299EDE5507625Eull},
            {0x234D4C,0x1A,0x2D5D4E933D9DF384ull},{0x142B20,0x17C,0x7ED17F4BA04AA282ull},
            {0xAC4A04,0xC5,0xC6D75E25462ED0F9ull},{0x14B164,0xE1,0x08C7CEEA0571E59Cull},
            {0x14C200,0x19A,0x58B9989622F4403Dull},{0x142634,0x156,0xA28DBE6ABEB41274ull},
            {0x142F88,0xCA,0x68358540D95CCC78ull},{0x293D08,0x2F,0xEF9AE3998E59246Aull},
            {0x289C78,0x199,0x65674C7AC3740415ull},{0xA9BA28,0xAA,0xD7BA2CFEEBCE7FD6ull},
            {0x143060,0x577,0x91777795D38E54D9ull},
            {0x20E570,0x48,0x97804F443A91066Bull},{0x13B0690,0x18,0x1C1460B6CC2AE388ull},
            {0x986590,0x39,0xFB001360F09560ECull},{0x9865CC,0x31,0x26D91F5F2A4E039Bull},
            {0x98598C,0x36,0xC96F84703458F656ull},{0x9859C4,0x41,0x310DA3FE98BC96DEull},
            {0x1444C4,0x29,0x86F34059C9A7E048ull},{0x1444F0,0x55,0xA9D2097373E5F0CBull},
            {0x986600,0x39,0x3A77F58202C021F7ull},{0x1036E4,0x1D,0xFB0346F745D483DFull},
            {0xAA3904,0xB4,0xF6FFB5E0F783252Cull},{0xAA39B8,0x46,0x34E91BFADF4F52EBull},
            {0x141B10,0x15A,0x998B02126473A17Eull},{0xA86438,0x5F,0xB21BDD71777C2FE4ull},
            {0xA86498,0xED,0x45C2D2934A4AE9AAull},{0xA86588,0xA1,0xAD4B532E1E453B70ull},
            {0xA7D164,0x8E,0xE6112646279C40C3ull},{0x1EC9B8,0xD8,0xBBF5A342038F09A4ull},
            {0xAB80EC,0x2E,0x146DDD6F36100D37ull},{0xAB811C,0xF9,0xF569927FF6AE7B16ull},
            {0xB2AB60,0x5D,0x33F87F5B0F18B0BFull},{0x142190,0x149,0x408AE8818BEC8697ull},
            {0x56D868,0xB8,0x70476F8900F18FCDull},{0x1E28056,0x2C,0xA15ACDB4F5FE02B4ull},
            {0x56D830,0x36,0x4CFE823BE4F752E8ull},{0x1EC404,0x1C8,0x8DDE20AE171C4940ull},
            {0x234EA8,0x9,0x4529125B2233CD6Dull},{0x14A700,0x3,0x69BC481C23DE2D4Dull},
            {0x14285C,0x140,0x7F8F1FE12076C7A3ull},{0x1422DC,0x2EA,0xF90676684D4EAB45ull},
            {0x986EDC,0x39,0x2F5F47BEC17078C6ull},{0x98663C,0x4B,0xD20EA8891D3F8FD2ull},
            {0x143FE0,0xAC,0x74C634BA05E90A46ull},{0x14408C,0xA4,0xA58F85C60C5EAD67ull},
            {0x1443C8,0xE7,0x86AE7EE6DB2874B6ull},{0x1444B0,0x13,0x02534795DF9B17BFull},
            {0x14B4C4,0x6B,0xF3B7741721FCEFF4ull},{0x985ABC,0x8,0xDFA2790998C4EDA5ull},
            {0x985B40,0x13,0x8F862A64B8F542B6ull},
            {0x983FD8,0x25,0x7263AB5E3AE722F6ull},{0x986798,0x25,0xF3D4BB04296C0210ull},
            {0x18AD4C8,0x25,0xFB8502015CB51927ull},{0x18B8500,0x25,0x6C638E587E8660E7ull},
            {0x1D42FE0,0x2B,0x80584DF1CB80C9FBull},{0x1D43010,0x2B,0x36C401F355ADFFC5ull},
            {0x14455C,0x1F2,0x1D6977D9CF9AD7B9ull},{0x14B9D0,0x1CA,0xBBA2A694C7BC9BC6ull},
            {0x98524C,0x48,0x69BAD7F99D97F30Aull},{0x9864E8,0xA7,0xE64E810FEFAEB70Cull},
            {0x985D0C,0x1A2,0x8EBC867BF465596Cull},{0x9861D0,0xDE,0x7EDD805AFDD84978ull},
            {0xA9B980,0xA6,0xACADB4B6DEC789BFull},{0x982C14,0x23,0xF8A16F44C7C70663ull},
            {0x983E48,0x93,0xEFCEFF872BEC9DDDull},{0x985560,0xDD,0xF960D8F7BE321116ull},
            {0x985640,0xED,0x6BA34E828DBDFDABull},{0x98585C,0xC6,0xEE145BA2F6272D64ull},
            {0x985BD8,0x54,0x49EE40A267D5EA17ull},
            {0xABC574,0x2E,0x18CDCD58C4E7496Bull},{0xABC5A4,0xF8,0x7311207FB27A10D1ull},
            {0xAF59C0,0x92,0x7E637A5AAE0DD739ull},{0x1EDA84,0x1FF,0xEBAB3552ECE2A243ull},
            {0x24CAC8,0xA0E,0xBBDE080A046F1FEFull},
            {0x1F0F80,0x288,0x9FF86B9ABBC51087ull},{0x1F0ED0,0xAE,0xF3B76F14B0B8A294ull},
            {0x1F02E4,0x8B,0x0605544E1B4E4D3Aull},{0xC54494,0x11D,0x893FF043112658D8ull},
            {0x24D58C,0x24B,0x29724E512136D3FFull},{0x1F83F0,0x113,0xFB6C489C9F68B68Full},
            {0x1F7F24,0x4C9,0xC733E768CC279CF2ull},{0xCB11B4,0x89,0xB1F54B7537D15E0Eull},
            {0x146C338,0xA3,0x8EAB21BC33BCD6D4ull},{0x875E04,0x90,0x9FAAB9AD8D195724ull},
            {0x875E94,0x1D3,0x3B4E4CA60342C413ull},{0x220110,0x5A4,0x5C372BC97D5DEB01ull},
            {0x876400,0x7D,0xB4E734D0DF358236ull},{0xDC6764,0xA6,0x48F24C13A625A3BAull},
            {0x1F85E4,0xE2,0x97E3E68E2846D216ull},{0x809F50,0x167,0x2D9ADF77C91A7BB9ull},
            {0x2045810,0x2D,0xD3F788B5BAFC6F89ull},{0x876480,0x138,0x85D391ACD843346Full},
            {0x146C29C,0x9A,0x1DBD26F06DABFCA7ull},{0x875D98,0x6C,0xD41625E67CEC1B7Eull},
            {0x2029EC0,0x26,0x048607F43E65FCEAull},{0x2231F0,0x19C,0xAA482F73BCA5DE0Cull},
            {0x8769E0,0x137,0xAF2EE82E684BC10Aull},{0x221930,0x303,0x51BA1810B87B452Eull},
            {0x767428,0x82,0xD7C15508BE8BA61Dull},{0x1F5ABC,0x68,0x80B206C7C5A53CA0ull},
            {0x24D7D8,0x88,0x31AF60A93F259D41ull},
            {0x1E440E0,0x12,0xDAC37C1BCADE8167ull},
            {0x1E3D2F8,0x4D,0xF26DE1D202211CC4ull},{0x1ECF2C,0x82,0x177EF1EE2846E667ull},
            {0x1EC7EC,0x98,0x6B7C1293E9814833ull},
            {0x1E623C,0x146,0x32BE79519B601E82ull},{0x985AC4,0x79,0xF0E0CCA45E45A368ull},
            {0x987240,0xD1,0x526184D0ABB5D347ull},{0x1ECA90,0x10E,0x9F61D4C82C297FEFull},
            {0xB79578,0x4A,0x6C05F5B0EABED305ull},{0x204568C,0x8,0xDCB561855989746Bull},
            {0xA4F930,0x10C,0x3C72EC320264A11Dull},{0xA96BFC,0x85,0x005F3EE65FC09DF2ull},
            {0xAA3FA8,0x106,0xD33F30E55D8D742Cull},{0xAC22B4,0x84,0xB433C4F5ECBBD66Full},
            {0xAA02E0,0xA8,0xAE83FAF375EBBE6Bull},{0xA98398,0x7A,0xFC489A11F1BDB31Full},
            {0xF220,0x19,0x2E9252B3644BE73Eull},{0x1D947C0,0x146,0x86B5E06D48B6F47Eull},
            {0x20505F4,0xA3,0x900F58A0583D49EEull},{0x2050974,0x170,0xDBE41953B109AE7Full},
            {0x2050698,0x81,0x5D1A0D721FA2008Dull},{0x205071C,0x32,0x9EB796BC221863D0ull},
            {0x2050750,0x34,0xFB17817E56B9F068ull},{0x16ABEC,0xD0,0xB1826DB9D149A16Full},
            {0x16AE58,0x37,0x429627E310828B9Aull},{0x16AE90,0xD7,0xB7A66C9FA081929Aull},
            {0x2050D5C,0x4C,0x4327F9790B8B14F3ull},{0x2050E6C,0x1A0,0x55FA4FF938CE497Cull},
            {0x12811F8,0x54,0x51228871BDBF4503ull},{0x128124C,0x5D,0x75487B166218AB52ull},
            {0x12812D0,0x74,0x7C168B63B2122BFEull},{0x16D834,0x27,0x5F5B23BD40E1E424ull},
            {0x16D85C,0x5E,0xFCDB2CD96C5A8904ull},{0x53F9B4,0x13B,0x1FCE5F96A6AB6255ull},
            {0x1EA660A,0x17,0x3FD8D19A378707C8ull},{0x53FAF0,0x7E,0x2F34986361C7A46Aull},
            {0x144D20,0x5F,0xF995D3B0B9F53A11ull},{0x53F37C,0x51,0x135AA3347C1DE1C9ull}};
        std::array<uint8_t,0xB00> bytes{};const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
        for(const auto& row:code){if(!Read(base+row.rva,bytes.data(),row.size))return false;
            uint64_t hash=14695981039346656037ull;for(uint32_t i=0;i<row.size;++i)hash=(hash^bytes[i])*1099511628211ull;
            if(hash!=row.hash)return false;}
        uint64_t initializer{},poolFree{},poolDelete{},flsAlloc{},flsSet{};
        const auto kernel=GetModuleHandleW(L"kernel32.dll");
        if(!kernel || !Read(base+0x2A8E628,&initializer,8) || initializer!=base+0xF220 ||
            !Read(base+0x2FF9C00,&poolFree,8) || poolFree!=base+0x53F37C ||
            !Read(base+0x2FF9C08,&poolDelete,8) || poolDelete!=base+0x2050750 ||
            !Read(base+0x2A8D5A0,&flsAlloc,8) || flsAlloc!=reinterpret_cast<uintptr_t>(GetProcAddress(kernel,"FlsAlloc")) ||
            !Read(base+0x2A8D5A8,&flsSet,8) || flsSet!=reinterpret_cast<uintptr_t>(GetProcAddress(kernel,"FlsSetValue")))return false;
        return true;
    }();CyberpunkVR_PreparationDefinitionCompatible=valid?1:2;return valid;
}
bool ObserveAllocatorLifecycle(void*,allocator_binding::Lifecycle& value){
    value={allocatorDomain.incarnation,allocatorDomain.resetEpoch,allocatorDomain.inFlight,
        allocatorDomain.closing || CyberpunkVR_OwnedExecutionClosing.load()!=0,AllocatorCoverageReady()};return true;
}
void PublishAllocatorReceipt(const allocator_binding::Receipt* receipt,uint64_t treeNodes){
    OwnedTelemetryWrite write;auto* fields=CyberpunkVR_OwnedExecutionAllocator;std::fill_n(fields,24,0ull);
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),common=base+allocator_binding::CommonRva;
    fields[0]=allocatorDomain.closing || CyberpunkVR_OwnedExecutionClosing.load()?2:
        receipt && receipt->Rejection()!=allocator_binding::Failure::None?3:allocatorDomain.ready?1:0;
    fields[1]=allocatorDomain.incarnation;fields[2]=allocatorDomain.resetEpoch;fields[3]=allocatorDomain.inFlight;
    fields[4]=allocatorDomain.servicesActive;fields[5]=AllocatorCoverageReady();fields[6]=common;
    Read(base+definition_tree::ObjectAllocatorRva,fields+7,8);Read(base+definition_tree::ControlAllocatorRva,fields+8,8);
    Read(base+definition_tree::StringAllocatorRva,fields+9,8);Read(common+allocator_binding::PoolOffset,fields+10,8);
    Read(common+allocator_binding::SmallBeginOffset,fields+11,8);Read(common+allocator_binding::SmallEndOffset,fields+12,8);
    Read(common+allocator_binding::MiddleBeginOffset,fields+13,8);Read(common+allocator_binding::MiddleEndOffset,fields+14,8);
    if(receipt){fields[15]=receipt->Allocations().size();for(const auto& allocation:receipt->Allocations())
        ++fields[allocation.region==allocator_binding::Region::Small?16:allocation.region==allocator_binding::Region::Middle?17:18];
        fields[20]=receipt->AllExactExtents();fields[21]=uint32_t(receipt->Rejection());}
    fields[19]=treeNodes;fields[22]=allocatorDomain.closeResetEvents;fields[23]=allocatorDomain.lastEntry;
}
bool CaptureAllocatorLocked(allocator_binding::Receipt& receipt){
    if(allocatorDomain.closing || CyberpunkVR_OwnedExecutionClosing.load() || allocatorDomain.inFlight || !AllocatorCoverageReady())return false;
    // This is a local identity episode anchored by fresh constructed-state
    // observations, not an invented native constructor or liveness event.
    if(!allocatorDomain.incarnation)allocatorDomain.incarnation=1;
    allocator_binding::Lifecycle tracking;ObserveAllocatorLifecycle(nullptr,tracking);
    if(!receipt.Capture(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),{true,true,false},tracking,Read,ObserveAllocatorLifecycle,nullptr)){
        PublishAllocatorReceipt(&receipt);return false;}
    if(allocatorDomain.ready && (allocatorDomain.binding.Globals()!=receipt.Globals() || allocatorDomain.binding.Ranges()!=receipt.Ranges() ||
        allocatorDomain.binding.ComponentVptr()!=receipt.ComponentVptr() || allocatorDomain.binding.Tracking()!=receipt.Tracking())){
        allocatorDomain.closing=true;CyberpunkVR_OwnedExecutionClosing=1;retirementStates.Shutdown();
        {OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[12];RetirementEvent(RetireEvent::AllocatorClose);}
        PublishAllocatorReceipt(&receipt);return false;}
    if(!allocatorDomain.ready){allocatorDomain.ready=true;allocatorDomain.binding=receipt;
        allocatorReadyObserved=true;
        OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[11];RetirementEvent(RetireEvent::AllocatorBound);}
    return true;
}
void PublishGenerationStats(){
    auto* stats=CyberpunkVR_PreparationGenerationStats;
    stats[2]=generationLedger.CancelledConsumers();stats[7]=generationLedger.CompletedConsumers();
    stats[8]=generationLedger.ForgottenObservations();stats[9]=generationLedger.UnsupportedConsumers();
    stats[11]=generationLedger.ActiveConsumers();stats[12]=generationLedger.ActiveGenerations();
    stats[14]=generationLedger.RetiredGenerations();
}
void LoseGenerationCoverage(definition_generations::Failure reason){
    CyberpunkVR_PreparationGenerationCoverageLost=1;
    for(const auto& [header,stamp]:mutations)if(stamp.generation)
        generationLedger.UnsupportedGeneration(stamp.generation,stamp.epoch,reason);
    for(const auto& [id,epoch]:closedGenerations)generationLedger.UnsupportedGeneration(id,epoch,reason);
}
void RejectGenerations(Consumer& consumer,definition_generations::Failure reason){
    if(consumer.generationRejected)return;
    consumer.generationRejected=true;++CyberpunkVR_PreparationGenerationStats[10];
    CyberpunkVR_PreparationGenerationStats[15]=uint32_t(reason);
    const auto& ticket=*consumer.ticket;
    const uint64_t diagnostic[]{uint32_t(reason),ticket.Id(),ticket.Epoch(),ticket.Generation(),ticket.GraphAddress(),consumer.renderer,consumer.rejectionDetail,consumer.rejectionHeader};
    std::copy_n(diagnostic,8,CyberpunkVR_PreparationGenerationReject);
    CyberpunkVR_PreparationGenerationRejectPhase=consumer.callbackEntered?1:0;
    if(generationLedger.HasConsumer(ticket.Id()))generationLedger.UnsupportedConsumer(ticket.Id(),ticket.Epoch(),ticket.Generation(),reason);
    else LoseGenerationCoverage(reason);
    PublishGenerationStats();
}
void PublishSelectedGenerations(const Consumer& consumer,bool complete=false){
    CyberpunkVR_PreparationGenerationSeq.fetch_add(1);
    const auto& ticket=*consumer.ticket;
    const uint64_t identity[]{ticket.Id(),ticket.Epoch(),ticket.Generation(),uint64_t(consumer.sourceVerified)|(uint64_t(consumer.executionVerified)<<1)|(uint64_t(complete)<<2)};
    std::copy_n(identity,4,CyberpunkVR_PreparationGenerationTicket);
    const auto count=std::min(consumer.generations.size(),size_t(128));
    CyberpunkVR_PreparationGenerationCount=uint32_t(count);
    for(size_t i=0;i<count;++i){const auto& binding=consumer.generations[i];const auto* value=generationLedger.GetGeneration(binding.generation);
        auto* row=CyberpunkVR_PreparationGenerationRows[i];std::fill_n(row,12,0ull);row[0]=binding.generation;row[2]=binding.bornEpoch;
        if(!value)continue;
        row[1]=value->ownerHeader;row[3]=value->closedMutationEpoch;row[4]=value->closedAtConsumer;
        row[5]=value->boundConsumers;row[6]=value->completedConsumers;row[7]=value->pendingConsumers;row[8]=value->unsupportedConsumers;
        row[9]=uint64_t(value->sourceClosed)|(uint64_t(value->detached)<<1)|(uint64_t(value->allPriorConsumersAccounted)<<2)|
            (uint64_t(value->unsupported)<<3)|(uint64_t(value->retiring)<<4);
        row[10]=std::count_if(consumer.definitions.begin(),consumer.definitions.end(),[&](const auto& item){return item.second.generation==binding.generation;});
        row[11]=generationLedger.CanAdopt(binding.generation,false);
    }
    CyberpunkVR_PreparationGenerationSeq.fetch_add(1);
}
uint64_t BeginMutationLocked(uint64_t header,uint32_t kind){
    ++CyberpunkVR_PreparationDefinitionMutationCounters[0];++CyberpunkVR_PreparationDefinitionMutationCounters[kind];
    if(!header || (mutations.size()>=MaxTrackedOwners && !mutations.contains(header))){mutationOverflow=true;++CyberpunkVR_PreparationDefinitionMutationCounters[3];return 0;}
    auto& stamp=mutations[header];const auto previousEpoch=stamp.epoch;const auto previousGeneration=stamp.generation;
    stamp.epoch=++nextMutation;stamp.generation=0;++stamp.inFlight;stamp.kind=kind;
    if(previousGeneration){
        if(generationLedger.CloseSource(previousGeneration,previousEpoch,stamp.epoch)){
            closedGenerations.emplace_back(previousGeneration,previousEpoch);++CyberpunkVR_PreparationGenerationStats[4];
        }else CyberpunkVR_PreparationGenerationStats[15]=uint32_t(generationLedger.Rejection());
    }
    PublishGenerationStats();return stamp.epoch;
}
void EndMutation(uint64_t header,uint64_t token){
    if(!token)return;std::lock_guard lock(mutex);const auto found=mutations.find(header);
    if(found==mutations.end() || !found->second.inFlight){mutationOverflow=true;return;}
    --found->second.inFlight;
}
bool RelativeCall(uint32_t returned,uint32_t target){
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);std::array<uint8_t,5> bytes{};int32_t displacement{};
    if(returned<5 || !Read(base+returned-5,bytes.data(),bytes.size()) || bytes[0]!=0xE8)return false;
    std::memcpy(&displacement,bytes.data()+1,4);return int64_t(returned)+displacement==target;
}
bool WriterCallsitesCompatible(){
    static const bool valid=[] {
        for(const auto& site:builderSites)if(!RelativeCall(site.begin,0x983F1C) || !RelativeCall(site.end,0x982C14))return false;
        for(const auto returned:{0x9855C4u,0x9856BAu,0x9858BDu})if(!RelativeCall(returned,0x9864E8))return false;
        for(const auto returned:{0x983E0Du,0x986DEBu,0x1F35E9Bu})if(!RelativeCall(returned,0x986E74))return false;
        return RelativeCall(0x983DBC,0x983E48);
    }();return valid;
}
bool NativeHeaderRead(uint64_t header,retirement_deferral::NativeHeader& out){
    retirement_deferral::NativeHeader again{};return Read(header,&out,16) && Read(header,&again,16) && out==again;
}
bool NonemptyCertifiedHeader(uint64_t header,definition_tree::Snapshot& tree){
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);retirement_deferral::NativeHeader native{};uint64_t allocator{},callback{};
    if(!NativeHeaderRead(header,native) || !native.capacity || !native.count || native.count>native.capacity ||
        native.capacity>definition_tree::MaxRoots || native.data>UINT64_MAX-uint64_t(native.capacity)*8-8 ||
        !Read(native.data+uint64_t(native.capacity)*8,&allocator,8) || allocator!=base+0x2B39FA0 ||
        !Read(allocator+0x20,&callback,8) || callback!=base+0x985ABC)return false;
    std::vector<uint64_t> roots(native.count);if(!Read(native.data,roots.data(),roots.size()*8) ||
        std::none_of(roots.begin(),roots.end(),[](uint64_t value){return value!=0;}))return false;
    return tree.Capture(roots,base,Read,true) && owned_execution::TreeCertified(tree) && tree.MatchesBefore(Read);
}
bool TreesDisjoint(const definition_tree::Snapshot& first,const definition_tree::Snapshot& second){
    for(const auto& a:first.Nodes())for(const auto& b:second.Nodes())if(a.object==b.object ||
        (a.controlBlock && a.controlBlock==b.controlBlock))return false;
    for(const auto& a:first.Arrays())for(const auto& b:second.Arrays())if(a.capacity && b.capacity &&
        a.data<b.trailerAddress+8 && b.data<a.trailerAddress+8)return false;
    for(const auto& a:first.Strings())for(const auto& b:second.Strings())if(a.allocationBytes && b.allocationBytes &&
        a.data<b.data+b.allocationBytes && b.data<a.data+a.allocationBytes)return false;
    return true;
}
bool InvokeSavedDestroy(void* header){
    __try{reinterpret_cast<void(*)(void*)>(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x986E74)(header);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool WriteEmptyNativeHeader(uint64_t header,uint64_t allocator){
    __try{*reinterpret_cast<volatile uint64_t*>(uintptr_t(header+8))=0;
        *reinterpret_cast<volatile uint64_t*>(uintptr_t(header))=allocator;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
enum class NativeIntentResult {NoAttachedOwner,Detached,Quarantined,FatalInvariant};
NativeIntentResult FatalProtectedIntent(RetainedOwner& owner,uint64_t detail){
    owner.state->UnsupportedExtraCompletion(0);writerCoverageLost=true;CyberpunkVR_OwnedExecutionClosing=1;
    OwnedTelemetryWrite write;auto* fields=CyberpunkVR_OwnedExecutionStats;
    fields[0]=6;fields[22]=uint32_t(OwnedFailure::RetirementInvariant);fields[23]=detail;
    const uint64_t failure[]{detail,owner.identity.ownerHeader,owner.identity.generation,owner.identity.bornEpoch,
        owner.lastConsumerId,owner.lastEpoch,owner.lastPairGeneration,protectedAttachedOwners.load()};
    std::copy_n(failure,8,CyberpunkVR_OwnedExecutionRetirementFailure);
    ++CyberpunkVR_OwnedExecutionRetirementTotals[10];RetirementEvent(RetireEvent::Quarantine,&owner,uint32_t(OwnedFailure::RetirementInvariant));
    return NativeIntentResult::FatalInvariant;
}
void FailFastProtectedIntent(){RaiseFailFastException(nullptr,nullptr,0);TerminateProcess(GetCurrentProcess(),0xC0000409);}
void FailClosedIntentException(uint64_t header,uint64_t detail){
    if(!protectedAttachedOwners.load() && !ownedAdmissionWindows.load() && !allocatorServicesActive.load())return;
    OwnedTelemetryWrite write;auto* fields=CyberpunkVR_OwnedExecutionRetirementFailure;
    fields[0]=detail;fields[1]=header;std::fill_n(fields+2,5,0ull);
    const auto sequence=lastProtectionSeq.load();std::array<uint64_t,6> observation{};
    for(size_t i=0;i<observation.size();++i)observation[i]=lastProtectionIdentity[i].load();
    if(!(sequence&1) && sequence==lastProtectionSeq.load() && observation[0]==header)std::copy_n(observation.data()+1,5,fields+2);
    fields[7]=protectedAttachedOwners.load();CyberpunkVR_OwnedExecutionStats[0]=6;
    CyberpunkVR_OwnedExecutionStats[22]=uint32_t(OwnedFailure::RetirementInvariant);CyberpunkVR_OwnedExecutionStats[23]=header;
    CyberpunkVR_OwnedExecutionClosing=1;FailFastProtectedIntent();
}
NativeIntentResult HandleNativeIntentLocked(uint64_t header,retirement_deferral::IntentKind kind,bool supportedScope,uint32_t caller){
    const auto found=mutations.find(header);if(found==mutations.end())return NativeIntentResult::NoAttachedOwner;
    auto& stamp=found->second;
    if(stamp.serviceOwner)return FatalProtectedIntent(*stamp.serviceOwner,4);
    RetainedOwner* attached{};for(const auto& owner:retainedOwners)if(owner && owner->state && owner->identity.ownerHeader==header &&
        !owner->state->ExactHeaderDetached() && !owner->emergencyDetached)attached=owner.get();
    if(!attached)return NativeIntentResult::NoAttachedOwner;
    {OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[4];RetirementEvent(RetireEvent::Intent,attached,0,header,stamp.storage,stamp.epoch,caller);}
    const auto& identity=attached->identity;retirement_deferral::NativeHeader before{};
    if(identity.generation!=stamp.generation || identity.bornEpoch!=stamp.epoch || !supportedScope || stamp.builders || stamp.operations || stamp.inFlight || !stamp.storageAlive || !stamp.builderClosed ||
        !NativeHeaderRead(header,before) || before!=identity.savedNative16B)
        return FatalProtectedIntent(*attached,1);
    // Everything needed for prolongation was reserved at admission. No tree
    // recapture, allocation or native release may occur in this handoff.
    const bool infrastructure=owned_execution::AllocatorsMatch(attached->tree,Read);
    const retirement_deferral::AllocatorProof intentAllocator{attached->state->FrozenAllocator(),infrastructure,true};
    const bool intent=attached->state->ObserveNativeIntent(kind,identity,{true,true,true,true},intentAllocator);
    const auto empty=identity.emptyAllocator16B;
    if(!WriteEmptyNativeHeader(header,empty.data))return FatalProtectedIntent(*attached,2);
    retirement_deferral::NativeHeader after{};const bool copied=NativeHeaderRead(header,after) && after==empty;
    if(!copied)return FatalProtectedIntent(*attached,3);
    if(attached->protectedAttached){attached->protectedAttached=false;protectedAttachedOwners.fetch_sub(1);}
    attached->emergencyDetached=true;
    if(!intent || !attached->state->RecordHeaderTransfer({identity,before,after,true})){
        attached->state->UnsupportedExtraCompletion(0);writerCoverageLost=true;CyberpunkVR_OwnedExecutionClosing=1;
        {OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[10];RetirementEvent(RetireEvent::Quarantine,attached,uint32_t(OwnedFailure::RetirementInvariant),header,stamp.storage,stamp.epoch,caller);}
        return NativeIntentResult::Quarantined;}
    attached->emergencyDetached=false;{OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[5];
        RetirementEvent(RetireEvent::Detach,attached,0,header,stamp.storage,stamp.epoch,caller);PublishRetirementReceipt();}
    return NativeIntentResult::Detached;
}
bool AllocationReceiptsDisjoint(const allocator_binding::Receipt& a,const allocator_binding::Receipt& b){
    for(const auto& first:a.Allocations())for(const auto& second:b.Allocations())
        if(first.address<second.address+second.bytes && second.address<first.address+first.bytes)return false;
    return true;
}
bool SavedVectorMatches(const RetainedOwner& owner){
    const auto& header=owner.identity.savedNative16B;uint64_t footer{},again{};
    if(!header.capacity || !Read(header.data+uint64_t(header.capacity)*8,&footer,8) ||
        !Read(header.data+uint64_t(header.capacity)*8,&again,8) || footer!=again || footer!=owner.identity.emptyAllocator16B.data)return false;
    for(size_t i=0;i<owner.definitions.size();++i){uint64_t object{};if(!Read(header.data+i*8,&object,8) || object!=owner.definitions[i])return false;}
    return owner.definitions.size()==header.count;
}
void ServiceReadyRetirements(uint64_t currentHeader,bool supportedScope,uint64_t scopeEpoch,uint32_t caller)noexcept try{
    if(!supportedScope || CyberpunkVR_OwnedExecutionClosing.load() || retirementBypass)return;
    definition_tree::Snapshot current;
    retirement_deferral::NativeHeader currentNative{};allocator_binding::Receipt currentAllocator;
    struct Candidate {size_t slot{};retirement_deferral::HeaderIdentity identity;};
    std::array<Candidate,retirement_deferral::MaxStates> candidates{};size_t count{};
    {std::lock_guard lock(mutex);
        if(std::none_of(retainedOwners.begin(),retainedOwners.end(),[](const auto& owner){
            return owner && owner->state && owner->ready && !owner->destroyInFlight;}))return;
        const auto found=mutations.find(currentHeader);
        if(found==mutations.end() || !found->second.storageAlive || found->second.epoch!=scopeEpoch || found->second.builders>1 ||
            found->second.operations || found->second.inFlight!=1 || !AllocatorCoverageReady() || allocatorDomain.closing ||
            !NativeHeaderRead(currentHeader,currentNative) || !NonemptyCertifiedHeader(currentHeader,current) ||
            !CaptureAllocatorLocked(currentAllocator) || !currentAllocator.BindSaved({currentNative.data,currentNative.capacity,currentNative.count},current))return;
        for(const auto& owner:retainedOwners)if(owner && owner->state && owner->identity.ownerHeader==currentHeader &&
            !owner->state->ExactHeaderDetached() && !owner->emergencyDetached)return;
        for(size_t i=0;i<retainedOwners.size();++i){const auto& owner=retainedOwners[i];
            if(owner && owner->state && owner->ready && !owner->destroyInFlight)candidates[count++]={i,owner->identity};}
    }
    for(size_t i=0;i<count;++i){const auto candidate=candidates[i];
        retirement_deferral::NativeHeader* saved{};RetainedOwner* owner{};
        {std::lock_guard lock(mutex);
            const auto scope=mutations.find(currentHeader);
            const auto& holder=retainedOwners[candidate.slot];
            if(!holder || holder->identity!=candidate.identity || !holder->state || !holder->ready || holder->destroyInFlight)continue;
            owner=holder.get();
            retirement_deferral::NativeHeader before{};allocator_binding::Receipt fresh;
            if(CyberpunkVR_OwnedExecutionClosing.load() || scope==mutations.end() || scope->second.epoch!=scopeEpoch ||
                scope->second.inFlight!=1 || scope->second.builders>1 || scope->second.operations || !current.MatchesBefore(Read) || !owned_execution::AllocatorsMatch(current,Read) ||
                owner->completionFlags!=0x1FF || !owner->declarationGroup || !owner->executionGroup || !owner->compilerGroup ||
                !NativeHeaderRead(currentHeader,before) || before!=currentNative || !owner->tree.MatchesBefore(Read) ||
                !owned_execution::AllocatorsMatch(owner->tree,Read) || !SavedVectorMatches(*owner) || !TreesDisjoint(owner->tree,current) ||
                !CaptureAllocatorLocked(fresh) || !owner->allocatorBinding.MatchesFresh(fresh) || !currentAllocator.MatchesFresh(fresh) ||
                !AllocationReceiptsDisjoint(owner->allocatorBinding,currentAllocator))continue;
            const retirement_deferral::AllocatorProof allocator{owner->state->FrozenAllocator(),true,true};
            saved=owner->state->BeginDestroy(allocator);
            if(saved){owner->destroyInFlight=true;scope->second.serviceOwner=owner;scope->second.serviceEpoch=scopeEpoch;
                scope->second.serviceStorage=scope->second.storage;++allocatorDomain.servicesActive;allocatorServicesActive.fetch_add(1);
                allocatorDomain.serviceCertificateObserved=true;owner->serviceHeader=currentHeader;owner->serviceStorage=scope->second.storage;
                owner->serviceEpoch=scopeEpoch;owner->serviceCaller=caller;
                OwnedTelemetryWrite write;++CyberpunkVR_OwnedExecutionRetirementTotals[6];RetirementEvent(RetireEvent::DestroyBegin,owner,0,currentHeader,
                    scope->second.storage,scopeEpoch,caller);PublishAllocatorReceipt(&owner->allocatorBinding,owner->tree.Nodes().size());PublishRetirementReceipt();}
        }
        if(!saved)continue;
        // BeginDestroy plus destroyInFlight is a committed permit under the same
        // gate as Close. Closing prevents later permits; this one finishes once.
        // Only plain pointers/scalars enter this certified native-use interval.
        retirementCallOwner=owner;retirementCallHeader=saved;retirementCallScope=scopeEpoch;
        ++retirementBypass;const bool returned=InvokeSavedDestroy(saved);--retirementBypass;
        retirementCallOwner=nullptr;retirementCallHeader=nullptr;retirementCallScope=0;
        {std::lock_guard lock(mutex);const auto scope=mutations.find(currentHeader);
            if(scope!=mutations.end() && scope->second.serviceOwner==owner && scope->second.serviceEpoch==scopeEpoch &&
                scope->second.serviceStorage==scope->second.storage){scope->second.serviceOwner=nullptr;scope->second.serviceEpoch=scope->second.serviceStorage=0;}
            else{FatalProtectedIntent(*owner,8);FailFastProtectedIntent();return;}
            const bool finished=owner->state->FinishDestroy(returned);owner->ready=false;owner->destroyInFlight=false;
            --allocatorDomain.servicesActive;allocatorServicesActive.fetch_sub(1);OwnedTelemetryWrite write;
            if(finished)++CyberpunkVR_OwnedExecutionRetirementTotals[7];else ++CyberpunkVR_OwnedExecutionRetirementTotals[10];
            RetirementEvent(RetireEvent::DestroyReturn,owner,finished?0:uint32_t(OwnedFailure::RetirementInvariant),currentHeader,
                owner->serviceStorage,scopeEpoch,caller);PublishAllocatorReceipt(&owner->allocatorBinding,owner->tree.Nodes().size());PublishRetirementReceipt();}
    }
}catch(...){/* Optional service proof failure preserves every saved header. */}
using Reset=void*(*)(void*,void*,void*);
Reset originalReset{};
void* ResetHook(void* builder,void* graph,void* header){
    if(retirementBypass){std::lock_guard lock(mutex);if(retirementCallOwner)FatalProtectedIntent(*retirementCallOwner,9);FailFastProtectedIntent();return nullptr;}
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto site=std::find_if(std::begin(builderSites),std::end(builderSites),[&](const BuilderSite& row){return caller==base+row.begin;});
    const auto b=reinterpret_cast<uintptr_t>(builder),g=reinterpret_cast<uintptr_t>(graph),h=reinterpret_cast<uintptr_t>(header);
    const bool layout=b && !(b&7) && b<=UINT64_MAX-0xF8 && g && !(g&7) && g<=UINT64_MAX-0x88 && h==g+0x70 &&
        !(b<g+0x88 && g<b+0xF8);
    const bool supported=site!=std::end(builderSites) && layout && WriterCallsitesCompatible();
    uint64_t token{};NativeIntentResult intent{};try{
        std::lock_guard lock(mutex);intent=HandleNativeIntentLocked(h,retirement_deferral::IntentKind::Reset,supported,uint32_t(caller-base));
        if(intent==NativeIntentResult::FatalInvariant){FailFastProtectedIntent();return nullptr;}
        token=BeginMutationLocked(h,1);auto found=mutations.find(h);
        if(found!=mutations.end()){auto& stamp=found->second;stamp.builderClosed=false;
            if(!supported || !stamp.storageAlive || stamp.builders || stamp.operations || stamp.slot>=builders.size() || builders[stamp.slot].address){writerCoverageLost=true;}
            else builders[stamp.slot]={b,h,token,GetCurrentThreadId(),site->end};
            ++stamp.builders;}
    }catch(...){mutationOverflow=true;writerCoverageLost=true;FailClosedIntentException(h,12);}
    ServiceReadyRetirements(h,supported,token,uint32_t(caller-base));
    auto* result=originalReset(builder,graph,header);return result;
}
CVR_DETOUR_IF("[single-pass] native definition generation reset",0x983F1C,ResetHook,originalReset,CodeCompatible)
using Destroy=void(*)(void*);
Destroy originalDestroy{};
void DestroyHook(void* header){
    if(retirementBypass){
        if(retirementCallOwner && retirementCallScope && header==retirementCallHeader){originalDestroy(header);return;}
        std::lock_guard lock(mutex);if(retirementCallOwner)FatalProtectedIntent(*retirementCallOwner,10);FailFastProtectedIntent();return;
    }
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto h=reinterpret_cast<uintptr_t>(header);
    const bool supported=(caller==base+0x983E0D || caller==base+0x986DEB || caller==base+0x1F35E9B) && WriterCallsitesCompatible();
    uint64_t token{};try{std::lock_guard lock(mutex);const auto intent=HandleNativeIntentLocked(h,retirement_deferral::IntentKind::Destroy,supported,uint32_t(caller-base));
        if(intent==NativeIntentResult::FatalInvariant){FailFastProtectedIntent();return;}
        token=BeginMutationLocked(h,2);}catch(...){mutationOverflow=true;writerCoverageLost=true;FailClosedIntentException(h,13);}
    ServiceReadyRetirements(h,supported,token,uint32_t(caller-base));
    originalDestroy(header);EndMutation(reinterpret_cast<uintptr_t>(header),token);
    {std::lock_guard lock(mutex);const auto found=mutations.find(h);if(found!=mutations.end()){found->second.storageAlive=false;found->second.builderClosed=false;}}
}
CVR_DETOUR_IF("[single-pass] native definition generation destroy",0x986E74,DestroyHook,originalDestroy,CodeCompatible)
using Birth=void*(*)(void*);
Birth originalBirth{};
void* BirthHook(void* output){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());auto* result=originalBirth(output);
    try{uint64_t graph{};const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
        std::lock_guard lock(mutex);
        if(caller!=base+0x983DBC || !Read(reinterpret_cast<uintptr_t>(output),&graph,8) || !graph || (graph&7) || graph>UINT64_MAX-0x88){writerCoverageLost=true;return result;}
        const auto header=graph+0x70;retirement_deferral::NativeHeader native{};
        const auto collision=mutations.find(header);
        if(collision!=mutations.end() && collision->second.serviceOwner){FatalProtectedIntent(*collision->second.serviceOwner,5);FailFastProtectedIntent();return result;}
        if(!NativeHeaderRead(header,native) || native!=retirement_deferral::NativeHeader{base+0x2B39FA0,0,0}){writerCoverageLost=true;return result;}
        auto found=mutations.find(header);uint32_t slot{};
        if(found==mutations.end()){if(storageSlots>=MaxTrackedOwners){writerCoverageLost=true;return result;}slot=uint32_t(storageSlots++);}
        else{slot=found->second.slot;if(found->second.builders || found->second.operations || found->second.inFlight){writerCoverageLost=true;return result;}}
        if(nextStorage==UINT64_MAX){writerCoverageLost=true;return result;}
        auto& stamp=mutations[header];if(stamp.serviceOwner){FatalProtectedIntent(*stamp.serviceOwner,5);FailFastProtectedIntent();return result;}
        stamp={};stamp.slot=slot;stamp.storage=++nextStorage;stamp.storageAlive=true;
    }catch(...){writerCoverageLost=true;}
    return result;
}
CVR_DETOUR_IF("[single-pass] native plain graph storage birth",0x983E48,BirthHook,originalBirth,CodeCompatible)
using EndBuilder=void(*)(void*);
EndBuilder originalEndBuilder{};
void EndBuilderHook(void* builder){
    const auto b=reinterpret_cast<uintptr_t>(builder),caller=reinterpret_cast<uintptr_t>(_ReturnAddress());BuilderReservation reservation{};
    {std::lock_guard lock(mutex);for(const auto& active:builders)if(active.address==b){reservation=active;break;}
        const auto active=mutations.find(reservation.header);
        if(active!=mutations.end() && active->second.serviceOwner){FatalProtectedIntent(*active->second.serviceOwner,6);FailFastProtectedIntent();return;}
        if(!reservation.address || caller!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+reservation.endReturn ||
            reservation.thread!=GetCurrentThreadId())writerCoverageLost=true;}
    originalEndBuilder(builder);
    if(reservation.address){std::lock_guard lock(mutex);const auto found=mutations.find(reservation.header);
        if(found==mutations.end() || found->second.slot>=builders.size() || builders[found->second.slot].address!=b ||
            found->second.epoch!=reservation.epoch || !found->second.builders || !found->second.inFlight)writerCoverageLost=true;
        else{auto& stamp=found->second;--stamp.builders;--stamp.inFlight;stamp.builderClosed=!stamp.builders && !stamp.operations;builders[stamp.slot]={};}}
}
CVR_DETOUR_IF("[single-pass] full native definition builder end",0x982C14,EndBuilderHook,originalEndBuilder,CodeCompatible)
using Append=void(*)(void*,void*);
Append originalAppend{};
void AppendHook(void* header,void* unique){
    const auto h=reinterpret_cast<uintptr_t>(header),caller=reinterpret_cast<uintptr_t>(_ReturnAddress()),base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    bool tracked{};
    {std::lock_guard lock(mutex);const auto found=mutations.find(h);
        if(found!=mutations.end()){auto& stamp=found->second;
            if(stamp.serviceOwner){FatalProtectedIntent(*stamp.serviceOwner,7);FailFastProtectedIntent();return;}
            for(const auto& owner:retainedOwners)if(owner && owner->identity.ownerHeader==h && owner->state &&
                owner->state->PendingReaders() && !owner->state->ExactHeaderDetached() && !owner->emergencyDetached){
                FatalProtectedIntent(*owner,11);FailFastProtectedIntent();return;}
            if((caller!=base+0x9855C4 && caller!=base+0x9856BA && caller!=base+0x9858BD) || !stamp.builders || stamp.slot>=builders.size() ||
                builders[stamp.slot].header!=h || builders[stamp.slot].thread!=GetCurrentThreadId())writerCoverageLost=true;
            ++stamp.operations;tracked=true;}
        else writerCoverageLost=true;}
    originalAppend(header,unique);
    if(tracked){std::lock_guard lock(mutex);const auto found=mutations.find(h);
        if(found==mutations.end() || !found->second.operations)writerCoverageLost=true;else --found->second.operations;}
}
CVR_DETOUR_IF("[single-pass] scoped native definition owner append",0x9864E8,AppendHook,originalAppend,CodeCompatible)
bool AllocatorBoundary(uint32_t entry,bool reset){
    try{std::lock_guard lock(mutex);OwnedTelemetryWrite write;allocatorDomain.lastEntry=entry;++allocatorDomain.closeResetEvents;
        if(allocatorDomain.servicesActive){for(const auto& owner:retainedOwners)if(owner && owner->destroyInFlight){FatalProtectedIntent(*owner,20);break;}
            FailFastProtectedIntent();return false;}
        for(const auto& owner:retainedOwners)if(owner && owner->state && owner->state->PendingReaders()){
            FatalProtectedIntent(*owner,21);FailFastProtectedIntent();return false;}
        if(reset){++allocatorDomain.resetEpoch;++allocatorDomain.inFlight;++CyberpunkVR_OwnedExecutionRetirementTotals[13];
            RetirementEvent(RetireEvent::AllocatorReset,nullptr,0,0,0,0,entry);}
        if(!reset || allocatorDomain.ready){allocatorDomain.closing=true;CyberpunkVR_OwnedExecutionClosing=1;
            retirementStates.Shutdown();++CyberpunkVR_OwnedExecutionRetirementTotals[12];
            RetirementEvent(RetireEvent::AllocatorClose,nullptr,0,0,0,0,entry);}
        else ++allocatorDomain.incarnation;
        PublishAllocatorReceipt();PublishRetirementReceipt();return true;
    }catch(...){FailClosedIntentException(0,22);CyberpunkVR_OwnedExecutionClosing=1;return true;}
}
void AllocatorResetReturned(){std::lock_guard lock(mutex);if(allocatorDomain.inFlight)--allocatorDomain.inFlight;
    else{allocatorDomain.closing=true;CyberpunkVR_OwnedExecutionClosing=1;}PublishAllocatorReceipt();}
bool CommonComponent(void* object,uint32_t offset){return reinterpret_cast<uintptr_t>(object)==reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+allocator_binding::CommonRva+offset;}
using AllocatorVoid=void(*)();AllocatorVoid originalAllocatorClose{};
void AllocatorCloseHook(){if(AllocatorBoundary(0x1D947C0,false))originalAllocatorClose();}
CVR_DETOUR_IF("[single-pass] common allocator CRT close",0x1D947C0,AllocatorCloseHook,originalAllocatorClose,CodeCompatible)
using AllocatorNoArg=uintptr_t(*)();AllocatorNoArg originalAllocatorConstruct{};
uintptr_t AllocatorConstructHook(){if(!AllocatorBoundary(0x20505F4,true))return 0;const auto result=originalAllocatorConstruct();AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common allocator construction boundary",0x20505F4,AllocatorConstructHook,originalAllocatorConstruct,CodeCompatible)
using AllocatorTwo=uintptr_t(*)(void*,void*);AllocatorTwo originalAllocatorSetup{};
uintptr_t AllocatorSetupHook(void* unused,void* config){if(!AllocatorBoundary(0x2050974,true))return 0;
    const auto result=originalAllocatorSetup(unused,config);AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common allocator backing boundary",0x2050974,AllocatorSetupHook,originalAllocatorSetup,CodeCompatible)
using AllocatorOne=uintptr_t(*)(void*);AllocatorOne originalSmallConstruct{};
uintptr_t SmallConstructHook(void* pool){const bool tracked=CommonComponent(pool,0xC928);if(tracked && !AllocatorBoundary(0x2050698,true))return 0;
    const auto result=originalSmallConstruct(pool);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common small pool construction",0x2050698,SmallConstructHook,originalSmallConstruct,CodeCompatible)
using AllocatorDestroy=void(*)(void*);AllocatorDestroy originalSmallDestroy{};
void SmallDestroyHook(void* pool){if(!CommonComponent(pool,0xC928) || AllocatorBoundary(0x205071C,false))originalSmallDestroy(pool);}
CVR_DETOUR_IF("[single-pass] common small pool close",0x205071C,SmallDestroyHook,originalSmallDestroy,CodeCompatible)
using AllocatorScalar=uintptr_t(*)(void*,uint32_t);AllocatorScalar originalSmallScalar{};
uintptr_t SmallScalarHook(void* pool,uint32_t flags){if(CommonComponent(pool,0xC928) && !AllocatorBoundary(0x2050750,false))return 0;return originalSmallScalar(pool,flags);}
CVR_DETOUR_IF("[single-pass] common small pool scalar close",0x2050750,SmallScalarHook,originalSmallScalar,CodeCompatible)
AllocatorTwo originalMiddleConstruct{},originalMiddleSetup{},originalMiddleRanges{},originalFallbackSetup{};
uintptr_t MiddleConstructHook(void* arena,void* config){const bool tracked=CommonComponent(arena,0xCD80);if(tracked && !AllocatorBoundary(0x16ABEC,true))return 0;
    const auto result=originalMiddleConstruct(arena,config);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common middle arena construction",0x16ABEC,MiddleConstructHook,originalMiddleConstruct,CodeCompatible)
uintptr_t MiddleSetupHook(void* arena,void* range){const bool tracked=CommonComponent(arena,0xCD80);if(tracked && !AllocatorBoundary(0x16AE58,true))return 0;
    const auto result=originalMiddleSetup(arena,range);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common middle arena setup",0x16AE58,MiddleSetupHook,originalMiddleSetup,CodeCompatible)
uintptr_t MiddleRangesHook(void* arena,void* range){const bool tracked=CommonComponent(arena,0xCD80);if(tracked && !AllocatorBoundary(0x16AE90,true))return 0;
    const auto result=originalMiddleRanges(arena,range);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common middle arena range writer",0x16AE90,MiddleRangesHook,originalMiddleRanges,CodeCompatible)
AllocatorOne originalFallbackConstruct{};
uintptr_t FallbackConstructHook(void* arena){const bool tracked=CommonComponent(arena,0xCE40);if(tracked && !AllocatorBoundary(0x2050D5C,true))return 0;
    const auto result=originalFallbackConstruct(arena);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common fallback construction boundary",0x2050D5C,FallbackConstructHook,originalFallbackConstruct,CodeCompatible)
uintptr_t FallbackSetupHook(void* arena,void* config){const bool tracked=CommonComponent(arena,0xCE40);if(tracked && !AllocatorBoundary(0x2050E6C,true))return 0;
    const auto result=originalFallbackSetup(arena,config);if(tracked)AllocatorResetReturned();return result;}
CVR_DETOUR_IF("[single-pass] common fallback setup boundary",0x2050E6C,FallbackSetupHook,originalFallbackSetup,CodeCompatible)
bool AllocatorCoverageReady(){
    return CodeCompatible() && g_cvrDetourReg_AllocatorCloseHook.installed && g_cvrDetourReg_AllocatorConstructHook.installed &&
        g_cvrDetourReg_AllocatorSetupHook.installed && g_cvrDetourReg_SmallConstructHook.installed && g_cvrDetourReg_SmallDestroyHook.installed &&
        g_cvrDetourReg_SmallScalarHook.installed && g_cvrDetourReg_MiddleConstructHook.installed && g_cvrDetourReg_MiddleSetupHook.installed &&
        g_cvrDetourReg_MiddleRangesHook.installed && g_cvrDetourReg_FallbackConstructHook.installed && g_cvrDetourReg_FallbackSetupHook.installed;
}
void RecordCacheResultLocked(RootAdmission& admission,uint64_t slot){
        if(!admission.cachePending)return;admission.cachePending=false;
        const auto address=admission.cache,key=admission.key;const auto views=admission.views,use=admission.use;
        if(!CyberpunkVR_OwnedExecutionCache[6] || CyberpunkVR_OwnedExecutionCache[6]!=address || CyberpunkVR_OwnedExecutionClosing.load())return;
        uint8_t miss{};uint64_t actualKey{};uint32_t actualViews{},actualUse{};
        if(slot<address || (slot-address)%0x90 || (slot-address)/0x90>=4 || !Read(admission.missed,&miss,1) || miss>1 ||
            !Read(slot+8,&actualKey,8) || actualKey!=key || !Read(slot+0x8C,&actualViews,4) || actualViews!=views ||
            !Read(slot,&actualUse,4) || actualUse!=use)return;
        OwnedTelemetryWrite write;auto* stats=CyberpunkVR_OwnedExecutionCache;
        ++stats[0];++stats[miss?2:1];stats[5]=stats[0];stats[7]=key;stats[8]=views;stats[9]=(slot-address)/0x90;stats[10]=use;
        stats[11]=admission.renderer;stats[12]=admission.id;stats[13]=admission.epoch;stats[14]=admission.pairGeneration;stats[15]=0x219B32;
        uint64_t* row{};uint64_t* empty{};for(auto& candidate:CyberpunkVR_OwnedExecutionCacheRows){
            if(candidate[0]==address && candidate[1]==key && candidate[2]==views)row=candidate;
            else if(!candidate[0] && !empty)empty=candidate;}
        if(!row && empty){row=empty;row[0]=address;row[1]=key;row[2]=views;row[3]=stats[0];row[7]=admission.renderer;
            ++stats[3];++CyberpunkVR_OwnedExecutionRetirementTotals[15];}
        if(row){row[4]=stats[0];++row[miss?6:5];}else stats[4]=1;
        ++CyberpunkVR_OwnedExecutionRetirementTotals[14];
}
bool CacheResultCompatible(){
    const uint8_t expected[]{0x48,0x8B,0xD8,0x48,0x89,0x44,0x24,0x78,0x48,0x8D,0x70,0x10};
    std::array<uint8_t,sizeof(expected)> actual{};
    return CodeCompatible() && Read(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x219B32,actual.data(),actual.size()) &&
        std::equal(actual.begin(),actual.end(),expected);
}
CVR_DETOUR_IF("[single-pass] immediate native cache result",0x219B32,CvrRenderGraphCacheResultDetour,CvrRenderGraphCacheResultOriginal,CacheResultCompatible)
bool CaptureCompatible();
resource_replay::Identity ReplayIdentity(const Consumer& consumer){
    const auto& identity=consumer.ownedState->FrozenIdentity();
    return {identity.consumerId,identity.epoch,identity.pairGeneration,consumer.resourcePool,identity.sourceGraph,identity.executionGraph};
}
void PublishReplay(Consumer& consumer){
    if(!consumer.resourceReplay)return;
    const auto& replay=*consumer.resourceReplay;const auto& identity=replay.Binding();
    uint64_t entered{},finished{};
    for(auto& row:consumer.resourceReplayRows){row[5]=replay.Cursor(row[2]);entered+=row[6]!=0;finished+=row[7]!=0;}
    const uint64_t stats[]{consumer.resourceReplayFailure || replay.Failed()?4u:replay.Complete()?3u:2u,
        identity.consumerId,identity.epoch,identity.generation,identity.pool,identity.executionGraph,identity.sourceGraph,
        replay.Declarations().size(),replay.CompletedCalls(),replay.RequestDifferences(),entered,finished,
        consumer.resourceReplayFailure?consumer.resourceReplayFailure:uint32_t(replay.Rejection()),
        consumer.resourceReplaySlot,consumer.resourceReplayCursor,consumer.resourceReplayMisses};
    std::copy_n(stats,16,CyberpunkVR_OwnedResourceReplayStats);
    const uint64_t proof[]{identity.consumerId,consumer.resourceReplayCompilerChecked,consumer.resourceReplayTerminalChecked,
        consumer.resourceReplayRows.size(),identity.epoch,identity.generation,identity.pool,0};
    std::copy_n(proof,8,CyberpunkVR_OwnedResourceReplayProof);
    publishedReplayRows=consumer.resourceReplayRows;
    CyberpunkVR_OwnedResourceReplayRows=reinterpret_cast<uintptr_t>(publishedReplayRows.data());
}
void ReplayFailed(Consumer& consumer,uint32_t failure,uint32_t slot=0,uint32_t cursor=0){
    if(!consumer.resourceReplayFailure){consumer.resourceReplayFailure=failure;consumer.resourceReplaySlot=slot;consumer.resourceReplayCursor=cursor;}
    OwnedTelemetryWrite write;PublishReplay(consumer);
}
bool CaptureReplay(Consumer& consumer){
    consumer.resourceReplay=std::make_unique<resource_replay::Receipt>();
    std::fill_n(CyberpunkVR_OwnedResourceReplayAsync,8,0ull);CyberpunkVR_OwnedResourceReplayAsync[0]=consumer.ticket->Id();
    std::fill_n(CyberpunkVR_OwnedResourceReplayMismatch,16,0ull);
    const auto identity=ReplayIdentity(consumer);
    std::vector<resource_replay::NodeBinding> nodes;
    for(const auto& node:consumer.executionGraph.nodes)nodes.push_back({node.handle&0xDFFFFFFFu,node.order,node.bytes[29],graph_snapshot::Field<uint32_t>(node.bytes,24)});
    std::array<uint32_t,resource_replay::Slots> before{},after{};
    std::vector<resource_replay::RawDeclaration> a,b;
    const auto capture=[&](auto& counts,auto& rows){
        uint8_t mode{};if(!Read(identity.pool+2,&mode,1) || mode!=1)return false;
        for(uint32_t slot=0;slot<resource_replay::Slots;++slot){
            const auto at=identity.pool+uint64_t(slot)*resource_uses::SlotStride;
            if(!Read(at+resource_uses::DeclarationCount,&counts[slot],4) || counts[slot]>resource_replay::RowsPerSlot)return false;
            const auto start=rows.size();rows.resize(start+counts[slot]);
            if(counts[slot] && !Read(at+resource_uses::Declarations,rows.data()+start,uint64_t(counts[slot])*48))return false;
        }
        return Read(identity.pool+2,&mode,1) && mode==1;
    };
    if(!capture(before,a) || !capture(after,b) || !consumer.resourceReplay->Capture(identity,ReplayIdentity(consumer),nodes,before,after,a,b)){
        ReplayFailed(consumer,consumer.resourceReplay->Failed()?uint32_t(consumer.resourceReplay->Rejection()):100);return false;}
    for(const auto& node:nodes)consumer.resourceReplayRows.push_back({node.handle,node.ordinal,node.slot,node.viewIndex,before[node.slot],0,0,0});
    OwnedTelemetryWrite write;PublishReplay(consumer);return true;
}
bool ReplayGrammarUnchanged(Consumer& consumer){
    if(!consumer.resourceReplay || !consumer.resourceReplay->Captured())return false;
    const auto& replay=*consumer.resourceReplay;const auto& identity=replay.Binding();
    const auto& expected=replay.Declarations();size_t index{};uint8_t state{};
    if(!Read(identity.pool+2,&state,1) || state!=2)return false;
    for(uint32_t slot=0;slot<resource_replay::Slots;++slot){
        const auto at=identity.pool+uint64_t(slot)*resource_uses::SlotStride;uint32_t count{},again{};
        if(!Read(at+resource_uses::DeclarationCount,&count,4) || count!=replay.DeclaredCount(slot))return false;
        for(uint32_t i=0;i<count;++i){resource_replay::RawDeclaration a{},b{};
            const auto row=at+resource_uses::Declarations+uint64_t(i)*48;
            if(index>=expected.size() || !Read(row,a.data(),48) || !Read(row,b.data(),48))return false;
            const auto bytes=resource_replay::PayloadBytes(expected[index][0]);if(bytes==UINT32_MAX)return false;
            const auto equal=[&](const auto& value){return value[0]==expected[index][0] && value[1]==slot &&
                std::equal(value.begin()+4,value.begin()+8+bytes,expected[index].begin()+4);};
            if(!equal(a) || !equal(b))return false;++index;
        }
        if(!Read(at+resource_uses::DeclarationCount,&again,4) || again!=count)return false;
    }
    return index==expected.size() && Read(identity.pool+2,&state,1) && state==2;
}
using ResourceCommand=uintptr_t(*)(void*,const void*,uint8_t);
ResourceCommand originalResourceCommand{};
uintptr_t ResourceCommandHook(void* pool,const void* command,uint8_t slot){
    if(!CyberpunkVR_OwnedResourceReplayEnable.load(std::memory_order_relaxed) || CyberpunkVR_OwnedExecutionClosing.load())return originalResourceCommand(pool,command,slot);
    std::shared_ptr<Consumer> consumer;resource_replay::Token token;resource_replay::NodeBinding node;
    resource_replay::Identity identity;uint32_t cursor{};bool observed=false;
    try{
        std::lock_guard lock(mutex);
        if(replayNodeDepth && replayNodeDepth<=replayNodes.size()){
            const auto& scope=replayNodes[replayNodeDepth-1];
            if(scope.consumer && scope.identity.pool==reinterpret_cast<uintptr_t>(pool)){
                consumer=scope.consumer;node=scope.node;identity=scope.identity;
                std::array<uint8_t,48> bytes{};uint8_t mode{};
                bool valid=node.slot==slot && Read(identity.pool+2,&mode,1) && mode==2 && Read(identity.pool+uint64_t(slot)*resource_uses::SlotStride+0x40,&cursor,4) &&
                    Read(reinterpret_cast<uintptr_t>(command),bytes.data(),1);
                const auto payload=valid?resource_replay::PayloadBytes(bytes[0]):UINT32_MAX;
                valid=valid && payload!=UINT32_MAX && Read(reinterpret_cast<uintptr_t>(command)+4,bytes.data()+4,4) &&
                    (!payload || Read(reinterpret_cast<uintptr_t>(command)+8,bytes.data()+8,payload));
                // 1F0ED0 writes the explicit R8 slot, not the request's scratch byte1.
                bytes[1]=slot;
                observed=valid && consumer->resourceReplay->Before(identity,node,mode,cursor,{bytes.data(),8+payload},token);
                if(!observed){OwnedTelemetryWrite write;
                    if(!CyberpunkVR_OwnedResourceReplayMismatch[0]){
                        size_t offset{};for(uint32_t i=0;i<slot;++i)offset+=consumer->resourceReplay->DeclaredCount(i);
                        CyberpunkVR_OwnedResourceReplayMismatch[0]=identity.consumerId;
                        CyberpunkVR_OwnedResourceReplayMismatch[1]=slot;CyberpunkVR_OwnedResourceReplayMismatch[2]=cursor;
                        CyberpunkVR_OwnedResourceReplayMismatch[3]=uint32_t(consumer->resourceReplay->Rejection());
                        if(offset+cursor<consumer->resourceReplay->Declarations().size())std::memcpy(CyberpunkVR_OwnedResourceReplayMismatch+4,consumer->resourceReplay->Declarations()[offset+cursor].data(),48);
                        std::memcpy(CyberpunkVR_OwnedResourceReplayMismatch+10,bytes.data(),48);
                    }
                    ReplayFailed(*consumer,valid?uint32_t(consumer->resourceReplay->Rejection()):101,slot,cursor);
                }
            }
        }
        if(!consumer)for(const auto& [group,value]:consumers)if(value->resourceReplay && value->resourceReplay->Captured() && value->resourcePool==reinterpret_cast<uintptr_t>(pool)){
            uint8_t mode{};if(Read(value->resourcePool+2,&mode,1) && mode==2){++value->resourceReplayMisses;ReplayFailed(*value,102,slot);}break;}
    }catch(...){if(consumer){std::lock_guard lock(mutex);ReplayFailed(*consumer,103,slot,cursor);}}
    const auto result=originalResourceCommand(pool,command,slot);
    if(observed)try{
        std::lock_guard lock(mutex);OwnedTelemetryWrite write;uint32_t after{};
        if(!Read(identity.pool+uint64_t(slot)*resource_uses::SlotStride+0x40,&after,4) ||
            !consumer->resourceReplay->After(token,identity,node,after,uint8_t(result)))ReplayFailed(*consumer,104,slot,after);
        PublishReplay(*consumer);
    }catch(...){std::lock_guard lock(mutex);ReplayFailed(*consumer,105,slot);}
    return result;
}
CVR_DETOUR_IF("[single-pass] native declaration/execution resource parity",0x1F0F80,ResourceCommandHook,originalResourceCommand,CodeCompatible)
std::shared_ptr<graph_jobs::NativeRootTicket> Capture(void* ownerAddress,void* graphAddress,uint64_t caller,const std::shared_ptr<graph_jobs::NativeRootTicket>& ticket){
    using namespace cvr::detail;
    if(CyberpunkVR_PreparationDefinitionMode.load()!=1 || !preparation::Active() || CyberpunkVR_PreparationPairState.load()!=2 ||
        caller!=reinterpret_cast<uintptr_t>(g_exe_base)+0x219E09 || !CyberpunkVR_PreparationDefinitionRequest.load())return {};
    if(ticket && (ticket->Epoch()!=CyberpunkVR_PreparationGraphJobsEpoch.load() || ticket->Generation()!=CyberpunkVR_PreparationPairGeneration.load()))return {};
    std::shared_ptr<graph_jobs::NativeRootTicket> retired;
    std::lock_guard lock(mutex);
    if(!CyberpunkVR_PreparationDefinitionRequest.exchange(0))return {};
    CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
    auto* stats=CyberpunkVR_PreparationDefinitionStats;std::fill_n(stats,16,0ull);stats[0]=1;
    std::fill_n(CyberpunkVR_PreparationDefinitionAggregate,8,0ull);
    std::fill_n(CyberpunkVR_PreparationDefinitionDeclaration,8,0ull);
    retired=std::move(sourceTicket);std::fill_n(CyberpunkVR_PreparationDefinitionTicket,8,0ull);
    source={};owners.clear();mappings.clear();entries.clear();captureOwner=0;
    definitionTree={};std::fill_n(CyberpunkVR_PreparationDefinitionTreeStats,12,0ull);
    CyberpunkVR_PreparationDefinitionTreeRoots=CyberpunkVR_PreparationDefinitionTreeNodes=CyberpunkVR_PreparationDefinitionTreeHandles=0;
    CyberpunkVR_PreparationDefinitionTreeArrays=CyberpunkVR_PreparationDefinitionTreeAllocators=0;
    CyberpunkVR_PreparationDefinitionTreeStrings=0;
    CyberpunkVR_PreparationDefinitionOwners=CyberpunkVR_PreparationDefinitionMappings=CyberpunkVR_PreparationDefinitionEntries=0;
    captureEpoch=CyberpunkVR_PreparationGraphJobsEpoch.load();captureGeneration=CyberpunkVR_PreparationPairGeneration.load();
    const auto current=[&]{return CyberpunkVR_PreparationDefinitionMode.load()==1 && captureEpoch==CyberpunkVR_PreparationGraphJobsEpoch.load() &&
        captureGeneration==CyberpunkVR_PreparationPairGeneration.load();};
    const auto reject=[&](uint64_t reason){stats[15]=reason;stats[0]=4;};
    try{
        if(!ticket || !ticket->Captured()){
            reject(500);CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);return {};
        }
        sourceTicket=ticket;
        uint64_t graph{},renderer{},cacheBase{};int32_t references{};
        if(!captureEpoch || mutationOverflow || !Read(reinterpret_cast<uintptr_t>(ownerAddress),&captureOwner,8) || !captureOwner ||
            captureOwner>UINT64_MAX-12 || !Read(captureOwner+8,&references,4) || references<=0 ||
            !(graph=ticket->SourceGraphAddress()) ||
            !Read(reinterpret_cast<uintptr_t>(g_exe_base)+RENDERER_GLOBAL_RVA,&renderer,8) || !renderer ||
            !Read(renderer+0x4620,&cacheBase,8)){reject(100);}
        else{
            graph_snapshot::Snapshot snapshot;
            if(!snapshot.Capture(graph,Read) || !source.Capture(cacheBase,graph,snapshot,reinterpret_cast<uintptr_t>(g_exe_base),Read))reject(200+uint32_t(source.Rejection()));
            else{
                for(const auto& owner:source.Owners()){
                    const auto header=owner.move.OwnerAddress();const auto found=mutations.find(header);
                    const bool hasDefinitions=std::any_of(owner.move.Definitions().begin(),owner.move.Definitions().end(),[](auto value){return value!=0;});
                    if(hasDefinitions && (found==mutations.end() || found->second.inFlight || found->second.kind!=1)){reject(300);break;}
                    const auto& native=owner.move.SavedHeader();const Stamp stamp=found==mutations.end()?Stamp{}:found->second;
                    owners.push_back({owner.perViewGraph,header,native.data,owner.move.EmptyHeader().data,stamp.epoch,entries.size(),native.capacity,native.count,stamp.inFlight,stamp.kind});
                    for(const auto value:owner.move.Definitions()){entries.push_back(value);stats[4]+=!value;}
                }
                if(stats[0]!=4 && current()){
                    mappings=source.Mappings();stats[1]=owners.size();stats[2]=mappings.size();stats[3]=entries.size();stats[5]=source.SourceFingerprint();
                    stats[6]=captureOwner;stats[7]=graph;stats[8]=cacheBase;stats[9]=source.CacheSlot();stats[10]=source.CacheKey();
                    stats[11]=captureEpoch;stats[12]=captureGeneration;stats[13]=caller-reinterpret_cast<uintptr_t>(g_exe_base);
                    CyberpunkVR_PreparationDefinitionOwners=reinterpret_cast<uintptr_t>(owners.data());
                    CyberpunkVR_PreparationDefinitionMappings=reinterpret_cast<uintptr_t>(mappings.data());
                    CyberpunkVR_PreparationDefinitionEntries=reinterpret_cast<uintptr_t>(entries.data());stats[0]=2;
                    auto* tree=CyberpunkVR_PreparationDefinitionTreeStats;tree[0]=1;tree[1]=captureEpoch;tree[2]=captureGeneration;tree[3]=ticket->Id();
                    if(definitionTree.Capture(entries,reinterpret_cast<uintptr_t>(g_exe_base),Read,true)){
                        tree[0]=2;tree[4]=definitionTree.Roots().size();tree[5]=definitionTree.Nodes().size();
                        tree[6]=definitionTree.Handles().size();tree[7]=definitionTree.Arrays().size();tree[8]=definitionTree.Allocators().size();
                        tree[10]=definitionTree.Strings().size();
                        CyberpunkVR_PreparationDefinitionTreeRoots=reinterpret_cast<uintptr_t>(definitionTree.Roots().data());
                        CyberpunkVR_PreparationDefinitionTreeNodes=reinterpret_cast<uintptr_t>(definitionTree.Nodes().data());
                        CyberpunkVR_PreparationDefinitionTreeHandles=reinterpret_cast<uintptr_t>(definitionTree.Handles().data());
                        CyberpunkVR_PreparationDefinitionTreeArrays=reinterpret_cast<uintptr_t>(definitionTree.Arrays().data());
                        CyberpunkVR_PreparationDefinitionTreeAllocators=reinterpret_cast<uintptr_t>(definitionTree.Allocators().data());
                        CyberpunkVR_PreparationDefinitionTreeStrings=reinterpret_cast<uintptr_t>(definitionTree.Strings().data());
                    }else{tree[0]=3;tree[9]=uint32_t(definitionTree.Rejection());}
                }else if(stats[0]!=4)reject(301);
            }
        }
    }catch(...){reject(400);}
    CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
    return sourceTicket;
}
using Construct=void*(*)(void*,void*,void*,void*,void*);
Construct originalConstruct{};
void BindSource(Consumer& consumer,uint64_t renderer,definition_generations::Admission admission=definition_generations::Admission::Source){
    using namespace definition_generations;
    const auto& ticket=*consumer.ticket;
    consumer.renderer=renderer;
    if(!generationLedger.HasConsumer(ticket.Id())){RejectGenerations(consumer,Failure::Consumer);return;}
    uint64_t cacheBase{};graph_snapshot::Snapshot snapshot;graph_definition::SourceReceipt receipt;
    if(!renderer || !Read(renderer+0x4620,&cacheBase,8) || !snapshot.Capture(ticket.SourceGraphAddress(),Read) ||
        !receipt.Capture(cacheBase,ticket.SourceGraphAddress(),snapshot,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),Read,
            graph_definition::CacheUsePolicy::AllowConcurrentHits,graph_definition::DefinitionContentPolicy::OwnerMembership)){
        consumer.rejectionDetail=uint32_t(receipt.Rejection())|(uint64_t(receipt.ChangedDetails())<<32);
        RejectGenerations(consumer,Failure::Generation);return;
    }
    for(const auto& owner:receipt.Owners()){
        const auto& definitions=owner.move.Definitions();
        if(std::none_of(definitions.begin(),definitions.end(),[](auto object){return object!=0;}))continue;
        const auto header=owner.move.OwnerAddress();const auto found=mutations.find(header);
        if(found==mutations.end() || !found->second.epoch || found->second.inFlight || found->second.kind!=1){
            consumer.rejectionHeader=header;
            RejectGenerations(consumer,Failure::Mutation);return;
        }
        auto& stamp=found->second;
        if(!stamp.generation){
            stamp.generation=generationLedger.BirthGeneration({header,stamp.epoch,stamp.epoch,stamp.epoch,false},false);
            if(!stamp.generation){RejectGenerations(consumer,generationLedger.Rejection());return;}
            ++CyberpunkVR_PreparationGenerationStats[3];
        }
        if(!generationLedger.ConsumerBound(ticket.Id(),stamp.generation) &&
            !generationLedger.BindConsumer(ticket.Id(),ticket.Epoch(),ticket.Generation(),stamp.generation,stamp.epoch,admission)){
            RejectGenerations(consumer,generationLedger.Rejection());return;
        }
        const DefinitionBinding binding{stamp.generation,stamp.epoch};
        if(std::none_of(consumer.generations.begin(),consumer.generations.end(),[&](const auto& value){return value.generation==stamp.generation;}))consumer.generations.push_back(binding);
        for(const auto object:definitions)if(object){
            if(admission==Admission::Execution)consumer.definitions.insert_or_assign(object,binding);
            else if(!consumer.definitions.emplace(object,binding).second){RejectGenerations(consumer,Failure::Duplicate);return;}
        }
    }
    if(admission==Admission::Source){consumer.sourceVerified=true;consumer.renderer=renderer;++CyberpunkVR_PreparationGenerationStats[5];}
    else{consumer.executionVerified=true;++CyberpunkVR_PreparationGenerationStats[6];}
    PublishGenerationStats();
}
void VerifyExecution(Consumer& consumer){
    if(consumer.ownedState){
        bool valid=consumer.ownedGraph && consumer.ownedGraph->ValidateCompiled();
        if(valid && consumer.sceneBothExpansion){
            valid=consumer.sceneBothExpansion->MatchesSource(consumer.coupledGraph,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)) &&
                consumer.sceneBothExpansion->MatchesCompiled(consumer.ownedGraph->Snapshot()) && ValidateSceneViews(consumer);
        }else if(valid && consumer.sceneExpansion){
            valid=consumer.sceneExpansion->MatchesSource(consumer.coupledGraph,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)) &&
                consumer.sceneExpansion->MatchesCompiled(consumer.ownedGraph->Snapshot()) && ValidateSceneViews(consumer);
        }else if(valid && consumer.coupledSchedule){const auto current=consumer.ownedGraph->Snapshot();
            valid=consumer.coupledSchedule->Matches(consumer.originalGraph,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)) &&
                current.nodes.size()==consumer.executionGraph.nodes.size() && current.edges==consumer.executionGraph.edges;
            if(valid)for(size_t i=0;i<current.nodes.size();++i){const auto& a=current.nodes[i];const auto& b=consumer.executionGraph.nodes[i];
                if(a.handle!=b.handle || a.order!=b.order || a.bytes!=b.bytes || a.vtable!=b.vtable || a.work!=b.work){valid=false;break;}}
        }else if(valid)valid=consumer.ownedGraph->SameSourceOrders();
        for(const auto& reader:consumer.extraReaders){auto& owner=*reader.owner;
            retirement_deferral::NativeHeader current{};const auto& expected=owner.identity.savedNative16B;
            valid=valid && owner.state && !owner.state->DestroyAttemptConsumed() && owner.tree.MatchesBefore(Read) &&
                owned_execution::AllocatorsMatch(owner.tree,Read);
            if(owner.state && !owner.state->ExactHeaderDetached() && !owner.emergencyDetached)valid=valid && NativeHeaderRead(owner.identity.ownerHeader,current) && current==expected;
            else if(owner.state)valid=valid && owner.state->SavedHeader()==expected;
            uint64_t allocator{};if(expected.capacity)valid=valid && Read(expected.data+uint64_t(expected.capacity)*8,&allocator,8) && allocator==owner.identity.emptyAllocator16B.data;
        }
        consumer.executionVerified=valid;
        if(HasSceneExpansion(consumer)){CyberpunkVR_OwnedSceneSplitStats[21]=valid;if(!valid)CyberpunkVR_OwnedSceneSplitStats[22]=8;}
        if(!valid){consumer.rejected=true;consumer.ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Execution);}
        return;
    }
    if(consumer.generationRejected || !consumer.sourceVerified)return;
    // Resolve actual native headers/stamps again; object addresses alone cannot
    // distinguish allocator reuse between queueing and execution.
    BindSource(consumer,consumer.renderer,definition_generations::Admission::Execution);
}
std::shared_ptr<Consumer> RegisterConsumer(void* destination,void* owner,void* graph,void* completion,uint64_t caller,uint64_t returnSlot){
    using namespace cvr::detail;
    if(CyberpunkVR_OwnedExecutionClosing.load() || caller!=reinterpret_cast<uintptr_t>(g_exe_base)+graph_jobs::ConstructReturn)return {};
    OwnedAdmissionWindow admissionWindow;
    ++CyberpunkVR_PreparationNativeConsumers[0];
    std::shared_ptr<Consumer> value=std::make_shared<Consumer>();value->ticket=std::make_shared<graph_jobs::NativeRootTicket>();
    std::lock_guard lock(mutex);
    auto* admission=rootAdmissionDepth && rootAdmissionDepth<=rootAdmissions.size()?&rootAdmissions[rootAdmissionDepth-1]:nullptr;
    if(admission)admission->constructed=true;
    if(consumers.size()>=MaxConsumers){
        if(admission && admission->registered)generationLedger.UnsupportedConsumer(admission->id,admission->epoch,admission->pairGeneration,definition_generations::Failure::Capacity);
        else LoseGenerationCoverage(definition_generations::Failure::Capacity);
        ++CyberpunkVR_PreparationNativeConsumers[6];return {};
    }
    const bool selected=(CyberpunkVR_PreparationDefinitionMode.load()==1 || CyberpunkVR_OwnedExecutionMode.load()==1) && preparation::Active();
    const auto epoch=selected?CyberpunkVR_PreparationGraphJobsEpoch.load():0;
    const auto generation=selected?CyberpunkVR_PreparationPairGeneration.load():0;
    const auto id=admission?admission->id:++nextConsumer;
    if(admission && admission->registered){
        if(generationLedger.SealProvisionalConsumer(id,epoch,generation)){
            admission->epoch=epoch;admission->pairGeneration=generation;
        }else{
            generationLedger.UnsupportedConsumer(id,admission->epoch,admission->pairGeneration);
            admission->registered=false;LoseGenerationCoverage(definition_generations::Failure::Epoch);
        }
    }
    const bool prepared=admission && PrepareOwnedReference(*value,*admission,reinterpret_cast<uintptr_t>(destination),
        reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(graph),reinterpret_cast<uintptr_t>(completion),returnSlot,caller);
    const auto expected=prepared?reinterpret_cast<uintptr_t>(value->ownedGraph->SortableData()):0;
    if(!value->ticket->CaptureBeforeConstruct(reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(graph),reinterpret_cast<uintptr_t>(completion),
        caller,reinterpret_cast<uintptr_t>(g_exe_base),epoch,generation,id,Read,
        JobLeaseApi{RetainGroup,ReleaseGroup,nullptr},graph_jobs::FrameOwnerApi{RetainFrame,ReleaseFrame},expected) || consumers.contains(value->ticket->Group())){
        if(admission && admission->registered)generationLedger.UnsupportedConsumer(id,epoch,generation);
        else LoseGenerationCoverage(definition_generations::Failure::Consumer);
        ++CyberpunkVR_PreparationNativeConsumers[5];return {};}
    if(prepared){
        value->fallbackTicket=std::make_shared<graph_jobs::NativeRootTicket>();
        value->fallbackDeclaration=std::make_shared<graph_jobs::DeclarationCompletionLease>();
        if(!value->fallbackTicket->CaptureBeforeConstruct(reinterpret_cast<uintptr_t>(owner),reinterpret_cast<uintptr_t>(graph),reinterpret_cast<uintptr_t>(completion),
            caller,reinterpret_cast<uintptr_t>(g_exe_base),epoch,generation,id,Read,
            JobLeaseApi{RetainGroup,ReleaseGroup,nullptr},graph_jobs::FrameOwnerApi{RetainFrame,ReleaseFrame}) ||
            !value->fallbackDeclaration->CaptureBeforeConstruct(reinterpret_cast<uintptr_t>(owner),value->fallbackTicket->GraphAddress(),caller,
                reinterpret_cast<uintptr_t>(g_exe_base),epoch,generation,id,Read,JobLeaseApi{RetainGroup,ReleaseGroup,CompleteGroup})){
            generationLedger.UnsupportedConsumer(id,epoch,generation);OwnedReject(OwnedFailure::Ticket);return {};}
    }
    consumers.emplace(value->ticket->Group(),value);++CyberpunkVR_PreparationNativeConsumers[1];
    if(admission)admission->prerequisiteGroup=value->ticket->Group();
    if(selected && epoch && generation){
        value->declaration=std::make_shared<graph_jobs::DeclarationCompletionLease>();
        value->declaration->CaptureBeforeConstruct(reinterpret_cast<uintptr_t>(owner),value->ticket->GraphAddress(),caller,
            reinterpret_cast<uintptr_t>(g_exe_base),epoch,generation,id,Read,JobLeaseApi{RetainGroup,ReleaseGroup,CompleteGroup});
    }
    if(admission && admission->registered)BindSource(*value,admission->renderer);
    else RejectGenerations(*value,definition_generations::Failure::PriorCoverage);
    if(prepared && (!value->sourceVerified || value->generationRejected || !value->declaration || !value->declaration->Captured() ||
        reinterpret_cast<uintptr_t>(value->executionTerminal->group)==value->ticket->Group() ||
        value->declaration->Group()==reinterpret_cast<uintptr_t>(value->executionTerminal->group) ||
        !ReserveExtraOwners(*value,reinterpret_cast<uintptr_t>(graph)))){
        CancelExtraReaders(*value,true);
        if(value->ownedState){value->ownedState->CancelSelection();ownedExecutionStates.Retire(id);value->ownedState=nullptr;}
        value->ownedGraph.reset();value->executionTerminal.reset();
        RecycleRetainedMetadata();PublishRetirementReceipt();
        value->ticket=std::move(value->fallbackTicket);value->declaration=std::move(value->fallbackDeclaration);
    }
    CyberpunkVR_PreparationNativeConsumers[7]=consumers.size();return value;
}
void* ConstructHook(void* destination,void* owner,void* graph,void* unused,void* completion){
    Poll();
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());std::shared_ptr<Consumer> consumer;
    try{consumer=RegisterConsumer(destination,owner,graph,completion,caller,reinterpret_cast<uintptr_t>(_AddressOfReturnAddress()));}catch(...){
        std::lock_guard lock(mutex);
        if(rootAdmissionDepth && rootAdmissionDepth<=rootAdmissions.size()){
            auto& admission=rootAdmissions[rootAdmissionDepth-1];admission.constructed=true;
            if(admission.registered && generationLedger.HasConsumer(admission.id))generationLedger.UnsupportedConsumer(admission.id,admission.epoch,admission.pairGeneration,definition_generations::Failure::Allocation);
            else LoseGenerationCoverage(definition_generations::Failure::Allocation);
        }else LoseGenerationCoverage(definition_generations::Failure::Allocation);
        ++CyberpunkVR_PreparationNativeConsumers[5];
    }
    if(consumer && consumer->ownedState){
        std::lock_guard lock(mutex);OwnedTelemetryWrite write;auto& state=*consumer->ownedState;const auto identity=state.FrozenIdentity();
        const bool authorized=!CyberpunkVR_OwnedExecutionClosing.load() && state.PreconstructGraphSlot(consumer->producerScope)==reinterpret_cast<uintptr_t>(graph) &&
            state.CurrentPhase()==owned_graph_execution::Phase::Prepared;
        uint64_t published{};const bool matched=authorized && CompareGraphSlot(reinterpret_cast<uintptr_t>(graph),identity.sourceGraph,identity.executionGraph,published) && published==identity.sourceGraph;
        uint64_t after{};const bool verified=matched && Read(reinterpret_cast<uintptr_t>(graph),&after,8) && after==identity.executionGraph;
        if(authorized)state.RecordPublication(identity,consumer->producerScope,verified);else state.CancelSelection();
        if(!verified){
            uint64_t rollback{};const bool restored=!authorized || (CompareGraphSlot(reinterpret_cast<uintptr_t>(graph),identity.executionGraph,identity.sourceGraph,rollback) &&
                (rollback==identity.executionGraph || rollback==identity.sourceGraph));
            const bool rollbackVerified=restored && Read(reinterpret_cast<uintptr_t>(graph),&after,8) && after==identity.sourceGraph;
            if(authorized)state.RecordRollback(identity,consumer->producerScope,rollbackVerified);
            CancelExtraReaders(*consumer,rollbackVerified);OwnedReject(rollbackVerified?OwnedFailure::Publication:OwnedFailure::Rollback);
            if(rollbackVerified && state.CanReleaseOwnedStorage() && !state.Escaped()){ownedExecutionStates.Retire(identity.consumerId);consumer->ownedState=nullptr;consumer->ownedGraph.reset();consumer->executionTerminal.reset();
                RecycleRetainedMetadata();PublishRetirementReceipt();
                consumer->ticket=std::move(consumer->fallbackTicket);consumer->declaration=std::move(consumer->fallbackDeclaration);
            }
        }else{
            state.MarkConstructInvoked(identity,consumer->producerScope);
            for(const auto& reader:consumer->extraReaders)reader.owner->state->MarkExtraReaderEscaped(reader.identity);
            CyberpunkVR_OwnedExecutionStats[0]=2;CyberpunkVR_OwnedExecutionStats[14]=1;
        }
    }
    auto ticket=consumer?consumer->ticket:std::shared_ptr<graph_jobs::NativeRootTicket>{};
    std::shared_ptr<graph_jobs::NativeRootTicket> selected;
    try{selected=Capture(owner,graph,caller,ticket);}catch(...){if(consumer && consumer->ownedState){std::lock_guard lock(mutex);
        consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Source);}}
    if(selected && consumer){std::lock_guard lock(mutex);PublishSelectedGenerations(*consumer);}
    auto* result=originalConstruct(destination,owner,graph,unused,completion);
    std::shared_ptr<Consumer> retiredConsumer;
    std::shared_ptr<graph_jobs::NativeRootTicket> retiredTicket;
    if(ticket){std::lock_guard lock(mutex);const bool verified=ticket->VerifyConstruct(reinterpret_cast<uintptr_t>(destination),Read);
        consumer->fallbackTicket.reset();consumer->fallbackDeclaration.reset();
        if(consumer->ownedState){OwnedTelemetryWrite write;consumer->ownedState->RecordConstruct(consumer->ownedState->FrozenIdentity(),verified,verified);
            CyberpunkVR_OwnedExecutionStats[15]=verified;if(!verified)OwnedReject(OwnedFailure::Construct);}
        if(!verified){++CyberpunkVR_PreparationNativeConsumers[5];RejectGenerations(*consumer,definition_generations::Failure::Consumer);}
        if(selected && ticket==sourceTicket && captureEpoch==CyberpunkVR_PreparationGraphJobsEpoch.load() && CyberpunkVR_PreparationDefinitionMode.load()==1){
            CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
            const uint64_t row[]{ticket->Id(),ticket->Group(),ticket->FrameOwner(),ticket->GraphAddress(),ticket->Epoch(),ticket->Generation(),verified,0};
            std::copy_n(row,8,CyberpunkVR_PreparationDefinitionTicket);
            if(!verified){CyberpunkVR_PreparationDefinitionStats[0]=4;CyberpunkVR_PreparationDefinitionStats[15]=600+uint32_t(ticket->Rejection());}
            CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
        }
        if(!verified && !consumer->ownedState){const auto found=consumers.find(ticket->Group());
            if(found!=consumers.end() && found->second==consumer){retiredConsumer=std::move(found->second);consumers.erase(found);++CyberpunkVR_PreparationNativeConsumers[8];}
            if(sourceTicket==ticket)retiredTicket=std::move(sourceTicket);
            CyberpunkVR_PreparationNativeConsumers[7]=consumers.size();
        }}
    return result;
}
CVR_DETOUR_IF("[single-pass] pre-queue definition owners",0xB56968,ConstructHook,originalConstruct,CaptureCompatible)
using QueueExecution=uintptr_t(*)(void*,void*,void*);
QueueExecution originalQueueExecution{};
uintptr_t QueueExecutionHook(void* job,void* unused,void* payload){
    if(CyberpunkVR_OwnedExecutionClosing.load())return originalQueueExecution(job,unused,payload);
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    {std::lock_guard lock(mutex);uint64_t owner{},graph{},prerequisite{},terminal{};
        if(caller==reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x219E15 &&
            Read(reinterpret_cast<uintptr_t>(payload),&owner,8) && Read(reinterpret_cast<uintptr_t>(payload)+8,&graph,8) &&
            Read(reinterpret_cast<uintptr_t>(payload)+0x18,&prerequisite,8) && Read(reinterpret_cast<uintptr_t>(job)+0x18,&terminal,8)){
            const auto found=consumers.find(prerequisite);
            if(found!=consumers.end() && found->second->ownedState){auto& consumer=*found->second;OwnedTelemetryWrite write;
                const auto identity=consumer.ownedState->FrozenIdentity();
                const bool valid=owner==identity.frameOwner && graph==identity.executionGraph && consumer.ticket->ConstructVerified() &&
                    consumer.executionTerminal && terminal==reinterpret_cast<uintptr_t>(consumer.executionTerminal->group);
                if(!valid || !consumer.ownedState->RecordQueued(owned_graph_execution::Path::Execution,identity,terminal,true)){
                    consumer.ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Queue);}
                else CyberpunkVR_OwnedExecutionStats[0]=3;
            }
        }
    }
    return originalQueueExecution(job,unused,payload);
}
CVR_DETOUR_IF("[single-pass] owned reference execution publication",0xA9B980,QueueExecutionHook,originalQueueExecution,CodeCompatible)
using CompilerDispatch=uintptr_t(*)(void*,void*,void*);
CompilerDispatch originalCompilerDispatch{};
uintptr_t CompilerDispatchHook(void* job,void* unused,void* rendererArgument){
    if(CyberpunkVR_OwnedExecutionClosing.load())return originalCompilerDispatch(job,unused,rendererArgument);
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());std::shared_ptr<Consumer> consumer;
    try{std::lock_guard lock(mutex);
        if(rootAdmissionDepth && rootAdmissionDepth<=rootAdmissions.size()){
            const auto found=consumers.find(rootAdmissions[rootAdmissionDepth-1].prerequisiteGroup);
            if(found!=consumers.end() && found->second->ownedState){consumer=found->second;OwnedTelemetryWrite write;
                const auto& ticket=*consumer->ticket;
                if(!consumer->compiler)consumer->compiler=std::make_shared<graph_jobs::CompilerIdentityLease>();
                consumer->compilerProducerIdentity=consumer->declaration?consumer->declaration->ContextAddress():0;
                const graph_jobs::CompilerBinding binding{ticket.Id(),ticket.Epoch(),ticket.Generation(),consumer->renderer,
                    consumer->resourceWrapper,consumer->resourcePool,ticket.FrameOwner(),ticket.GraphAddress(),consumer->compilerProducerIdentity};
                if(!consumer->declaration || !consumer->declaration->Matched() || !consumer->declarationSnapshotReady ||
                    !ticket.ConstructVerified() || !consumer->compiler->CaptureBeforeDispatch(binding,reinterpret_cast<uintptr_t>(job),
                        reinterpret_cast<uintptr_t>(rendererArgument),caller,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),Read,
                        JobLeaseApi{RetainGroup,ReleaseGroup,CompleteGroup})){
                    consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Resource);}
                PublishCompilerReceipt(*consumer);
            }
        }
    }catch(...){if(consumer){std::lock_guard lock(mutex);consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Resource);}}
    const auto result=originalCompilerDispatch(job,unused,rendererArgument);
    if(consumer){std::lock_guard lock(mutex);if(!consumer->compiler || !consumer->compiler->MarkOriginalDispatchReturned()){
        consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Resource);}
        PublishCompilerReceipt(*consumer);}
    return result;
}
CVR_DETOUR_IF("[single-pass] owned compiler dispatch identity",0xABC574,CompilerDispatchHook,originalCompilerDispatch,CodeCompatible)
using Execute=void(*)(void*,void*);
Execute originalExecute{};
bool ExecuteCompatible(){return CodeCompatible() && CyberpunkVR_PreparationGraphJobsCompatibility[0]==1;}
void ExecuteHook(void* payload,void* worker){
    if(CyberpunkVR_OwnedExecutionClosing.load()){originalExecute(payload,worker);return;}
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());std::shared_ptr<Consumer> consumer;
    try{
        uint64_t graph{},group{};
        if(Read(reinterpret_cast<uintptr_t>(payload)+8,&graph,8) && Read(reinterpret_cast<uintptr_t>(payload)+0x18,&group,8)){
            std::lock_guard lock(mutex);const auto found=consumers.find(group);
            if(found!=consumers.end() && !found->second->callbackEntered){
                consumer=found->second;consumer->callbackEntered=true;++CyberpunkVR_PreparationNativeConsumers[2];
                if(!consumer->ticket->MatchExecutionCallback(reinterpret_cast<uintptr_t>(payload),graph,caller,Read)){
                    auto* diagnostic=CyberpunkVR_PreparationNativeConsumerReject;diagnostic[0]=2;diagnostic[1]=group;
                    diagnostic[2]=caller-reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);diagnostic[3]=reinterpret_cast<uintptr_t>(worker);
                    Read(reinterpret_cast<uintptr_t>(worker)+0x20,diagnostic+4,8);
                    if(diagnostic[4])Read(diagnostic[4],diagnostic+5,8);
                    consumer->rejected=true;++CyberpunkVR_PreparationNativeConsumers[5];}
                else{
                    consumer->completion=std::make_shared<graph_jobs::TaskCompletionLease>();
                    if(!consumer->completion->CaptureBeforeCallback(reinterpret_cast<uintptr_t>(worker),caller,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base),
                        group,Read,JobLeaseApi{RetainGroup,ReleaseGroup,CompleteGroup})){
                        auto* diagnostic=CyberpunkVR_PreparationNativeConsumerReject;diagnostic[0]=3;diagnostic[1]=uint32_t(consumer->completion->Rejection());
                        diagnostic[2]=caller-reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);diagnostic[3]=reinterpret_cast<uintptr_t>(worker);
                        Read(reinterpret_cast<uintptr_t>(worker)+0x20,diagnostic+4,8);
                        if(diagnostic[4])Read(diagnostic[4],diagnostic+5,8);
                        int32_t refs{},pending{};if(diagnostic[5]){Read(diagnostic[5]+0x1c,&refs,4);Read(diagnostic[5]+0x18,&pending,4);}
                        diagnostic[6]=uint32_t(refs);diagnostic[7]=uint32_t(pending);
                        consumer->rejected=true;++CyberpunkVR_PreparationNativeConsumers[5];}
                    if(consumer->ownedState && consumer->completion->Captured() && (!consumer->executionTerminal ||
                        consumer->completion->Group()!=reinterpret_cast<uintptr_t>(consumer->executionTerminal->group))){
                        consumer->rejected=true;consumer->ownedState->ReportFailure(owned_graph_execution::Failure::TerminalGroup);OwnedReject(OwnedFailure::Terminal);}
                    if(!consumer->rejected)VerifyExecution(*consumer);
                }
                if(consumer->rejected)RejectGenerations(*consumer,definition_generations::Failure::TerminalProof);
            }else{++CyberpunkVR_PreparationNativeConsumers[5];auto* diagnostic=CyberpunkVR_PreparationNativeConsumerReject;
                LoseGenerationCoverage(definition_generations::Failure::Consumer);
                diagnostic[0]=1;diagnostic[1]=group;diagnostic[2]=caller-reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);diagnostic[3]=reinterpret_cast<uintptr_t>(worker);}
        }else{std::lock_guard lock(mutex);LoseGenerationCoverage(definition_generations::Failure::Consumer);++CyberpunkVR_PreparationNativeConsumers[5];}
    }catch(...){std::lock_guard lock(mutex);if(consumer){consumer->rejected=true;RejectGenerations(*consumer,definition_generations::Failure::Allocation);}
        else LoseGenerationCoverage(definition_generations::Failure::Allocation);++CyberpunkVR_PreparationNativeConsumers[5];}
    originalExecute(payload,worker);
    if(consumer){std::lock_guard lock(mutex);
        consumer->callbackReturned=true;
        if(consumer->completion && consumer->completion->MarkOriginalReturned())++CyberpunkVR_PreparationNativeConsumers[3];
        else{consumer->rejected=true;RejectGenerations(*consumer,definition_generations::Failure::TerminalProof);++CyberpunkVR_PreparationNativeConsumers[5];}}
    if(consumer && consumer->ownedState){std::lock_guard lock(mutex);OwnedTelemetryWrite write;
        if(consumer->rejected || !consumer->completion || !consumer->completion->OriginalReturned()){
            consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);CyberpunkVR_OwnedExecutionStats[0]=6;
        }else{consumer->ownedState->RecordOriginalReturned(owned_graph_execution::Path::Execution,
            consumer->ownedState->FrozenIdentity(),consumer->completion->Group());CyberpunkVR_OwnedExecutionStats[19]=1;}}
}
CVR_DETOUR_IF("[single-pass] native root terminal completion",0xAC4A04,ExecuteHook,originalExecute,ExecuteCompatible)
bool CaptureCompatible(){
    return CodeCompatible() && g_cvrDetourReg_ResetHook.installed && g_cvrDetourReg_DestroyHook.installed && g_cvrDetourReg_ExecuteHook.installed;
}
bool OwnedWriterCoverageReady(){
    return CodeCompatible() && WriterCallsitesCompatible() && !writerCoverageLost.load() && !mutationOverflow.load() &&
        RenderGraphCacheObservationInstalled() && g_cvrDetourReg_CvrRenderGraphCacheResultDetour.installed &&
        g_cvrDetourReg_BirthHook.installed && g_cvrDetourReg_EndBuilderHook.installed && g_cvrDetourReg_AppendHook.installed &&
        g_cvrDetourReg_ResetHook.installed && g_cvrDetourReg_DestroyHook.installed && g_cvrDetourReg_QueueExecutionHook.installed &&
        g_cvrDetourReg_CompilerDispatchHook.installed;
}
}
void PrepareCodeGuards() noexcept{try{CodeCompatible();SceneCodeCompatible();}catch(...){CyberpunkVR_PreparationDefinitionCompatible=2;}}
void BeforeCacheLookup(uint64_t cache,uint32_t use,uint64_t key,uint32_t views,uint64_t missed,uint64_t caller)noexcept{
    if(!rootAdmissionDepth || rootAdmissionDepth>rootAdmissions.size() ||
        caller!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x219B32 || CyberpunkVR_OwnedExecutionClosing.load())return;
    auto& admission=rootAdmissions[rootAdmissionDepth-1];
    admission.cache=cache;admission.use=use;admission.key=key;admission.views=views;admission.missed=missed;admission.cachePending=true;
}
extern "C" void CvrRenderGraphCacheResult(uint64_t slot)noexcept{
    if(!rootAdmissionDepth || rootAdmissionDepth>rootAdmissions.size() || CyberpunkVR_OwnedExecutionClosing.load())return;
    try{std::lock_guard lock(mutex);RecordCacheResultLocked(rootAdmissions[rootAdmissionDepth-1],slot);}
    catch(...){/* Cache counters never grant native retirement authority. */}
}
uint64_t BeginRoot(uint64_t renderer,uint64_t caller) noexcept{
    if(CyberpunkVR_OwnedExecutionClosing.load() || caller!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0xAA3957 || CyberpunkVR_PreparationRootCompatible.load()!=1 ||
        !g_cvrDetourReg_ConstructHook.installed || !CaptureCompatible())return 0;
    const auto scope=++rootAdmissionDepth;++CyberpunkVR_PreparationGenerationStats[0];
    if(scope>rootAdmissions.size()){
        try{std::lock_guard lock(mutex);LoseGenerationCoverage(definition_generations::Failure::Capacity);}catch(...){CyberpunkVR_PreparationGenerationCoverageLost=1;}
        ++CyberpunkVR_PreparationGenerationStats[10];CyberpunkVR_PreparationGenerationStats[15]=uint32_t(definition_generations::Failure::Capacity);return scope;
    }
    auto& admission=rootAdmissions[scope-1];admission={};
    try{
        std::lock_guard lock(mutex);
        admission.id=++nextConsumer;admission.renderer=renderer;
        admission.registered=generationLedger.RegisterProvisionalConsumer(admission.id);
        if(admission.registered)++CyberpunkVR_PreparationGenerationStats[1];
        else{++CyberpunkVR_PreparationGenerationStats[10];CyberpunkVR_PreparationGenerationStats[15]=uint32_t(generationLedger.Rejection());}
        PublishGenerationStats();
    }catch(...){CyberpunkVR_PreparationGenerationCoverageLost=1;++CyberpunkVR_PreparationGenerationStats[10];CyberpunkVR_PreparationGenerationStats[15]=uint32_t(definition_generations::Failure::Allocation);}
    return scope;
}
void EndRoot(uint64_t scope) noexcept{
    if(!scope)return;
    if(scope!=rootAdmissionDepth){++CyberpunkVR_PreparationGenerationStats[10];return;}
    if(scope<=rootAdmissions.size()){
        auto& admission=rootAdmissions[scope-1];
        try{
            std::lock_guard lock(mutex);
            if(admission.prerequisiteGroup){const auto found=consumers.find(admission.prerequisiteGroup);
                if(found!=consumers.end() && found->second->declaration){auto& consumer=*found->second;
                    consumer.declaration->MarkOriginalRootReturned();
                    if(consumer.ownedState){OwnedTelemetryWrite write;
                        if(!consumer.compiler || !consumer.compiler->MarkOriginalRootReturned())
                            consumer.ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);
                        consumer.ownedState->RecordProducerClosed(consumer.ownedState->FrozenIdentity(),scope);consumer.declarationContext=0;
                        CyberpunkVR_OwnedExecutionStats[18]=1;PublishCompilerReceipt(consumer);}
                }}
            // The guarded root's only non-constructor return precedes its first
            // cache lookup (zero-sized view). This is not a job completion proof.
            if(admission.registered && !admission.constructed && generationLedger.HasConsumer(admission.id)){
                if(!generationLedger.CancelUnboundConsumer(admission.id,admission.epoch,admission.pairGeneration,true)){
                    generationLedger.UnsupportedConsumer(admission.id,admission.epoch,admission.pairGeneration);++CyberpunkVR_PreparationGenerationStats[10];
                }
            }
            PublishGenerationStats();
        }catch(...){++CyberpunkVR_PreparationGenerationStats[10];}
        admission={};
    }
    --rootAdmissionDepth;
}
uint64_t BeforeDeclare(uint64_t graph,uint64_t context,uint64_t job,uint64_t caller) noexcept{try{
    if(CyberpunkVR_OwnedExecutionClosing.load() || !rootAdmissionDepth || rootAdmissionDepth>rootAdmissions.size())return 0;
    std::lock_guard lock(mutex);const auto& admission=rootAdmissions[rootAdmissionDepth-1];
    const auto found=consumers.find(admission.prerequisiteGroup);
    if(found==consumers.end() || !found->second->declaration)return 0;
    auto& consumer=*found->second;
    const bool matched=consumer.declaration->MatchBeforeDeclare(job,graph,caller,Read);
    if(consumer.ownedState){OwnedTelemetryWrite write;const auto identity=consumer.ownedState->FrozenIdentity();
        graph_snapshot::Snapshot actual;bool topology=actual.Capture(graph,Read) && actual.edges==consumer.executionGraph.edges &&
            actual.nodes.size()==consumer.executionGraph.nodes.size();
        if(topology)for(size_t i=0;i<actual.nodes.size();++i){const auto& a=actual.nodes[i];const auto& b=consumer.executionGraph.nodes[i];
            if(a.handle!=b.handle || a.order!=b.order || a.bytes!=b.bytes || a.vtable!=b.vtable || a.work!=b.work){topology=false;break;}}
        graph_snapshot::Snapshot sourceNow;
        if(topology)topology=sourceNow.Capture(consumer.ticket->SourceGraphAddress(),Read) && sourceNow.edges==consumer.originalGraph.edges &&
            sourceNow.nodes.size()==consumer.originalGraph.nodes.size() && (HasSceneExpansion(consumer) || sourceNow.nodes.size()==actual.nodes.size());
        if(topology)for(size_t i=0;i<sourceNow.nodes.size();++i){const auto& a=sourceNow.nodes[i];const auto& b=consumer.originalGraph.nodes[i];
            if(a.handle!=b.handle || a.order!=b.order || a.bytes!=b.bytes || a.vtable!=b.vtable || a.work!=b.work ||
                (!HasSceneExpansion(consumer) && a.definition!=actual.nodes[i].definition)){topology=false;break;}}
        if(topology && consumer.coupledSchedule){paired_frontier::Plan refreshed;paired_cpu_frontier::Plan cpu;
            const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
            topology=refreshed.BuildCandidate(sourceNow,base,paired_frontier::Boundary::Discard)==paired_frontier::Result::CandidateNeedsCpuAndResourceCompile &&
                cpu.BuildCandidate(refreshed)==paired_cpu_frontier::Result::CandidateNeedsOwnershipAndResourceCompile &&
                cpu.Proposed()==consumer.coupledSchedule->Proposed() && context_order::Analyze(actual,base,Read).PrefixReady();
            if(topology && HasSceneExpansion(consumer)){OwnedNativeGraph currentCoupled;
                topology=currentCoupled.Create(sourceNow,cpu.Proposed()) && currentCoupled.CompileReference();
                if(topology)topology=(consumer.sceneBothExpansion?
                    consumer.sceneBothExpansion->MatchesDeclaration(currentCoupled.Snapshot(),actual,base):
                    consumer.sceneExpansion->MatchesDeclaration(currentCoupled.Snapshot(),actual,base)) && ValidateSceneViews(consumer);
            }
        }
        if(!matched || !topology || !consumer.ownedState->RecordQueued(owned_graph_execution::Path::Declaration,identity,consumer.declaration->Group(),true)){
            consumer.ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Declaration);
            if(HasSceneExpansion(consumer))CyberpunkVR_OwnedSceneSplitStats[22]=9;
        }else{consumer.declarationContext=context;
            if(HasSceneExpansion(consumer))CyberpunkVR_OwnedSceneSplitStats[20]=1;
            consumer.executionGraph=std::move(actual);consumer.declarationSnapshotReady=true;
            publishedScheduleBindings=std::move(sourceNow.nodes);CyberpunkVR_OwnedExecutionScheduleBindings=reinterpret_cast<uintptr_t>(publishedScheduleBindings.data());
            CyberpunkVR_OwnedExecutionSchedule[3]=paired_frontier::detail::Fingerprint(consumer.executionGraph,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base));
            CyberpunkVR_OwnedExecutionSchedule[15]=1;
            uint64_t wrapper{},pool{};
            const bool resources=Read(context+8,&wrapper,8) && wrapper && Read(wrapper,&pool,8) && pool;
            if(!resources || (consumer.resourceWrapper && consumer.resourceWrapper!=wrapper) || (consumer.resourcePool && consumer.resourcePool!=pool)){
                consumer.ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Resource);return 0;}
            consumer.resourceWrapper=wrapper;consumer.resourcePool=pool;
            CyberpunkVR_OwnedExecutionStats[16]=consumer.declaration->Group();CyberpunkVR_OwnedExecutionStats[17]=1;
            CyberpunkVR_OwnedExecutionStats[21]=consumer.extraReaders.size();}}
    return consumer.ticket->Id();
}catch(...){++CyberpunkVR_PreparationNativeConsumers[5];return 0;}}
bool CopyOwnedDeclarationSnapshot(uint64_t id,graph_snapshot::Snapshot& output)noexcept try{
    if(!id)return false;std::lock_guard lock(mutex);
    for(const auto& [group,consumer]:consumers)if(consumer->ticket->Id()==id && consumer->ownedState && consumer->declarationSnapshotReady){
        output=consumer->executionGraph;return true;}
    return false;
}catch(...){return false;}
void AfterDeclare(uint64_t id) noexcept{try{
    if(!id)return;std::lock_guard lock(mutex);
    for(const auto& [group,consumer]:consumers)if(consumer->ticket->Id()==id && consumer->declaration){
        const bool returned=consumer->declaration->MarkOriginalDeclareReturned();
        if(consumer->ownedState){OwnedTelemetryWrite write;
            if(!returned || !consumer->ownedState->RecordOriginalReturned(owned_graph_execution::Path::Declaration,
                consumer->ownedState->FrozenIdentity(),consumer->declaration->Group())){
                consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Declaration);}}
        return;}
}catch(...){++CyberpunkVR_PreparationNativeConsumers[5];}}
uint64_t BeginResourceQueue(uint64_t context,uint64_t graph) noexcept{
    if(!CyberpunkVR_OwnedResourceReplayEnable.load(std::memory_order_relaxed) || CyberpunkVR_OwnedExecutionClosing.load())return 0;
    try{
        uint64_t owner{};if(!Read(context,&owner,8))return 0;
        std::lock_guard lock(mutex);
        for(const auto& [group,consumer]:consumers)if(consumer->ownedState &&
            consumer->ownedState->FrozenIdentity().executionGraph==graph && consumer->ticket->FrameOwner()==owner){
            OwnedTelemetryWrite write;std::fill_n(CyberpunkVR_OwnedResourceReplayRegistration,8,0ull);
            CyberpunkVR_OwnedResourceReplayRegistration[0]=consumer->ticket->Id();
            CyberpunkVR_OwnedResourceReplayRegistration[1]=1;
            if(!consumer->resourceWrapper || !consumer->resourcePool){
                uint64_t renderer{},wrapper{},pool{};
                if(!Read(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x3427C00,&renderer,8) || renderer!=consumer->renderer ||
                    !Read(renderer+0x98,&wrapper,8) || !wrapper || !Read(wrapper,&pool,8) || !pool){ReplayFailed(*consumer,123);return 0;}
                consumer->resourceWrapper=wrapper;consumer->resourcePool=pool;
            }
            uint8_t mode{};
            if(!consumer->resourcePool || !Read(consumer->resourcePool+2,&mode,1)){ReplayFailed(*consumer,123);return 0;}
            CyberpunkVR_OwnedResourceReplayRegistration[2]=mode;
            if(replayQueueDepth==replayQueues.size()){ReplayFailed(*consumer,110);return 0;}
            graph_snapshot::Snapshot actual;
            if(!actual.Capture(graph,Read) || actual.edges!=consumer->executionGraph.edges || actual.nodes.size()!=consumer->executionGraph.nodes.size()){ReplayFailed(*consumer,111);return 0;}
            for(size_t i=0;i<actual.nodes.size();++i)if(actual.nodes[i].handle!=consumer->executionGraph.nodes[i].handle || actual.nodes[i].bytes!=consumer->executionGraph.nodes[i].bytes){ReplayFailed(*consumer,116);return 0;}
            replayQueues[replayQueueDepth++]=consumer;return replayQueueDepth;
        }
    }catch(...){return 0;}
    return 0;
}
void EndResourceQueue(uint64_t scope) noexcept{
    if(!scope)return;try{std::lock_guard lock(mutex);
        if(scope!=replayQueueDepth || scope>replayQueues.size()){for(auto& value:replayQueues)if(value)ReplayFailed(*value,118);replayQueues={};replayQueueDepth=0;return;}
        replayQueues[--replayQueueDepth].reset();
    }catch(...){}
}
uint64_t BeforeResourceReset(uint64_t pool,uint8_t mode,uint64_t caller) noexcept{
    if(mode!=3 || !CyberpunkVR_OwnedResourceReplayEnable.load())return 0;
    try{std::lock_guard lock(mutex);
        if(!replayNodeDepth || replayNodeDepth>replayNodes.size())return 0;
        const auto& scope=replayNodes[replayNodeDepth-1];if(!scope.consumer || scope.identity.pool!=pool)return 0;
        auto& value=*scope.consumer;OwnedTelemetryWrite write;
        const auto& node=value.executionGraph.nodes[scope.node.ordinal];
        const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
        bool valid=caller==base+0xB79598 && node.work==base+0xB79578 && node.vtable==base+0x312D310 &&
            value.resourceReplay && value.resourceReplay->Complete() && value.resourceReplayCompilerChecked && ReplayGrammarUnchanged(value);
        valid=valid && CyberpunkVR_OwnedResourceReplayAsync[0]==scope.identity.consumerId &&
            CyberpunkVR_OwnedResourceReplayAsync[1]==CyberpunkVR_OwnedResourceReplayAsync[2] &&
            CyberpunkVR_OwnedResourceReplayAsync[2]==CyberpunkVR_OwnedResourceReplayAsync[5] && !CyberpunkVR_OwnedResourceReplayAsync[3] && !CyberpunkVR_OwnedResourceReplayAsync[4];
        for(const auto& row:value.resourceReplayRows){
            valid=valid && row[6]==1 && row[7]==uint32_t(row[1]!=scope.node.ordinal);
            uint32_t cursor{};valid=valid && Read(pool+uint64_t(row[2])*resource_uses::SlotStride+0x40,&cursor,4) && cursor==row[4];
        }
        const uint64_t reset[]{scope.identity.consumerId,scope.node.handle,scope.node.ordinal,scope.node.slot,caller-base,uint64_t(valid),0,0};
        std::copy_n(reset,8,CyberpunkVR_OwnedResourceReplayReset);
        value.resourceReplayResetSealed=valid;if(!valid)ReplayFailed(value,124,scope.node.slot);else PublishReplay(value);
        return scope.identity.consumerId;
    }catch(...){return 0;}
}
void AfterResourceReset(uint64_t consumer,uint64_t pool,uint8_t mode) noexcept{
    if(!consumer || mode!=3)return;
    try{std::lock_guard lock(mutex);
        for(const auto& [group,value]:consumers)if(value->ticket->Id()==consumer && value->resourcePool==pool){OwnedTelemetryWrite write;
            uint8_t state{};value->resourceReplayResetReturned=value->resourceReplayResetSealed && Read(pool+2,&state,1) && state==3;
            CyberpunkVR_OwnedResourceReplayReset[6]=value->resourceReplayResetReturned;
            if(!value->resourceReplayResetReturned)ReplayFailed(*value,125);
            PublishReplay(*value);return;}
    }catch(...){}
}
void EndResourceNode(uint64_t scope) noexcept{
    if(!scope)return;
    try{
        std::lock_guard lock(mutex);
        if(scope!=replayNodeDepth || scope>replayNodes.size()){
            for(uint32_t i=0;i<(std::min)(replayNodeDepth,uint32_t(replayNodes.size()));++i)if(replayNodes[i].consumer)ReplayFailed(*replayNodes[i].consumer,115);
            replayNodes={};replayNodeDepth=0;return;
        }
        auto value=std::move(replayNodes[--replayNodeDepth]);replayNodes[replayNodeDepth]={};
        if(value.consumer && value.node.ordinal<value.consumer->resourceReplayRows.size()){
            ++value.consumer->resourceReplayRows[value.node.ordinal][7];OwnedTelemetryWrite write;PublishReplay(*value.consumer);
        }
    }catch(...){}
}
namespace {
void ObserveResourceContext(void* destination,const void* inputContext,uint64_t caller){
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    const uint32_t kind=caller==base+0xAA4028?1:caller==base+0xA4F9B6?2:caller==base+0xAA0340?3:0;
    AsyncReplay receipt;
    if(kind && CyberpunkVR_OwnedResourceReplayEnable.load() && replayNodeDepth && replayNodeDepth<=replayNodes.size()){
        std::lock_guard lock(mutex);const auto& parent=replayNodes[replayNodeDepth-1];
        std::array<uint64_t,2> owner{};uint8_t flags{},slot{};
        if(parent.consumer && Read(reinterpret_cast<uintptr_t>(inputContext),owner.data(),16) &&
            owner[0]==parent.consumer->ticket->FrameOwner() && owner[1]==parent.consumer->resourceWrapper &&
            Read(reinterpret_cast<uintptr_t>(inputContext)+0x30,&flags,1) && (flags&2) &&
            Read(reinterpret_cast<uintptr_t>(inputContext)+0x38,&slot,1) && slot==parent.node.slot){receipt.node=parent;receipt.kind=kind;}
        else if(parent.consumer)ReplayFailed(*parent.consumer,130);
    }
    if(receipt.node.consumer){std::lock_guard lock(mutex);std::array<uint8_t,72> second{};
        const auto payload=reinterpret_cast<uintptr_t>(destination)-8;
        if(!Read(reinterpret_cast<uintptr_t>(destination),receipt.context.data(),72) || !Read(reinterpret_cast<uintptr_t>(destination),second.data(),72) ||
            second!=receipt.context || asyncReplayPayloads.size()>=4096 || asyncReplayPayloads.contains(payload))ReplayFailed(*receipt.node.consumer,131);
        else{asyncReplayPayloads.emplace(payload,std::move(receipt));OwnedTelemetryWrite write;
            ++CyberpunkVR_OwnedResourceReplayAsync[1];CyberpunkVR_OwnedResourceReplayAsync[3]=asyncReplayPayloads.size();}
    }
}
CVR_DETOUR_IF("[single-pass] source-bound asynchronous resource context",0x1EC7EC,CvrResourceContextDetour,CvrResourceContextOriginal,CodeCompatible)
using ResourceWorker=void(*)(void*,void*);
ResourceWorker originalPassEnd{},originalDefinitionWorker{},originalRangeWorker{};
void ResourceWorkerRun(void* payload,void* worker,uint32_t kind,ResourceWorker original){
    uint32_t scope{};
    if(CyberpunkVR_OwnedResourceReplayEnable.load()){
        std::lock_guard lock(mutex);const auto found=asyncReplayPayloads.find(reinterpret_cast<uintptr_t>(payload));
        if(found!=asyncReplayPayloads.end()){
            auto receipt=std::move(found->second);asyncReplayPayloads.erase(found);auto& value=*receipt.node.consumer;
            OwnedTelemetryWrite write;CyberpunkVR_OwnedResourceReplayAsync[3]=asyncReplayPayloads.size();
            std::array<uint8_t,72> context{},again{};uint8_t mode{};
            if(receipt.kind!=kind || !Read(reinterpret_cast<uintptr_t>(payload)+8,context.data(),72) ||
                !Read(reinterpret_cast<uintptr_t>(payload)+8,again.data(),72) || context!=receipt.context || again!=context ||
                !value.resourceReplay || value.resourceReplay->Binding()!=receipt.node.identity ||
                !Read(value.resourcePool+2,&mode,1) || mode!=2 || replayNodeDepth==replayNodes.size()){++CyberpunkVR_OwnedResourceReplayAsync[4];ReplayFailed(value,132);}
            else{scope=++replayNodeDepth;replayNodes[scope-1]=std::move(receipt.node);++CyberpunkVR_OwnedResourceReplayAsync[2];}
        }
    }
    {
        uint64_t child{},context{};const auto address=reinterpret_cast<uintptr_t>(payload);
        if(CyberpunkVR_SceneChildEnable.load(std::memory_order_relaxed)==1 && kind==2 && address && address<=UINT64_MAX-80){
            context=address+8;Read(address,&child,8);
        }
        // Only A96BFC carries a definition at payload[0]. Other worker kinds
        // clear inherited child attribution until their real dispatcher runs.
        scene_child::Scope attribution(child,context,1);
        original(payload,worker);
    }
    if(scope){std::lock_guard lock(mutex);
        if(scope!=replayNodeDepth){if(replayNodes[scope-1].consumer)ReplayFailed(*replayNodes[scope-1].consumer,133);}
        else{auto parent=std::move(replayNodes[--replayNodeDepth]);replayNodes[replayNodeDepth]={};OwnedTelemetryWrite write;
            ++CyberpunkVR_OwnedResourceReplayAsync[5];if(parent.consumer)PublishReplay(*parent.consumer);}
    }
}
void ResourcePassEnd(void* payload,void* worker){ResourceWorkerRun(payload,worker,1,originalPassEnd);}
void ResourceDefinitionWorker(void* payload,void* worker){ResourceWorkerRun(payload,worker,2,originalDefinitionWorker);}
void ResourceRangeWorker(void* payload,void* worker){ResourceWorkerRun(payload,worker,3,originalRangeWorker);}
CVR_DETOUR_IF("[single-pass] asynchronous pass-end resource scope",0xAC22B4,ResourcePassEnd,originalPassEnd,CodeCompatible)
CVR_DETOUR_IF("[single-pass] asynchronous definition resource scope",0xA96BFC,ResourceDefinitionWorker,originalDefinitionWorker,CodeCompatible)
CVR_DETOUR_IF("[single-pass] asynchronous range resource scope",0xA98398,ResourceRangeWorker,originalRangeWorker,CodeCompatible)
using BuildResourcePayload=uintptr_t(*)(void*,void*,void*);
BuildResourcePayload originalResourcePayload{};
uintptr_t ResourcePayloadHook(void* holder,void* unused,void* input){
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    std::shared_ptr<Consumer> consumer;ReplayPayload receipt;
    if(CyberpunkVR_OwnedResourceReplayEnable.load() && caller==reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x1E6541 && replayQueueDepth && replayQueueDepth<=replayQueues.size()){
        std::lock_guard lock(mutex);consumer=replayQueues[replayQueueDepth-1];
        if(consumer && Read(reinterpret_cast<uintptr_t>(input),receipt.bytes.data(),32) && Read(reinterpret_cast<uintptr_t>(input)+0x21,receipt.bytes.data()+0x21,1)){
            const auto& nodes=consumer->executionGraph.nodes;size_t matches{};
            for(const auto& node:nodes)if(std::equal(node.bytes.begin()+24,node.bytes.begin()+32,receipt.bytes.begin()+8) &&
                std::equal(node.bytes.begin()+8,node.bytes.begin()+24,receipt.bytes.begin()+16)){
                receipt.node={consumer,ReplayIdentity(*consumer),{node.handle&0xDFFFFFFFu,node.order,node.bytes[29],graph_snapshot::Field<uint32_t>(node.bytes,24)}};++matches;}
            uint64_t owner{};std::memcpy(&owner,receipt.bytes.data(),8);
            if(matches!=1 || owner!=consumer->ticket->FrameOwner()){ReplayFailed(*consumer,119);receipt.node={};}
        }else if(consumer)ReplayFailed(*consumer,120);
    }
    const auto result=originalResourcePayload(holder,unused,input);
    if(receipt.node.consumer){std::lock_guard lock(mutex);std::array<uint64_t,2> a{},b{};std::array<uint8_t,40> bytes{};
        if(!Read(reinterpret_cast<uintptr_t>(holder),a.data(),16) || !Read(reinterpret_cast<uintptr_t>(holder),b.data(),16) || a!=b ||
            a[0]!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x985AC4 || !Read(a[1],bytes.data(),32) || !Read(a[1]+0x21,bytes.data()+0x21,1) || bytes!=receipt.bytes ||
            replayPayloads.size()>=512 || replayPayloads.contains(a[1]))ReplayFailed(*consumer,121);
        else{replayPayloads.emplace(a[1],std::move(receipt));OwnedTelemetryWrite write;
            ++CyberpunkVR_OwnedResourceReplayRegistration[3];CyberpunkVR_OwnedResourceReplayRegistration[5]=replayPayloads.size();}
    }
    return result;
}
CVR_DETOUR_IF("[single-pass] early resource-node payload identity",0x987240,ResourcePayloadHook,originalResourcePayload,CodeCompatible)
using ExecuteResourceNode=void(*)(void*,void*);
ExecuteResourceNode originalResourceNode{};
void ResourceNodeHook(void* payload,void* worker){
    uint64_t scope{};
    if(CyberpunkVR_OwnedResourceReplayEnable.load()){
        std::lock_guard lock(mutex);const auto found=replayPayloads.find(reinterpret_cast<uintptr_t>(payload));
        if(found!=replayPayloads.end()){
            auto receipt=std::move(found->second);replayPayloads.erase(found);auto& value=*receipt.node.consumer;
            std::array<uint8_t,40> a{},b{};uint64_t renderer{},wrapper{},pool{};uint8_t mode{};
            if(!value.resourceReplay || !value.resourceReplay->Captured() ||
                value.resourceReplay->Binding()!=receipt.node.identity ||
                reinterpret_cast<uintptr_t>(_ReturnAddress())!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x985AFB ||
                !Read(reinterpret_cast<uintptr_t>(payload),a.data(),32) || !Read(reinterpret_cast<uintptr_t>(payload)+0x21,a.data()+0x21,1) ||
                !Read(reinterpret_cast<uintptr_t>(payload),b.data(),32) || !Read(reinterpret_cast<uintptr_t>(payload)+0x21,b.data()+0x21,1) || a!=receipt.bytes || b!=a ||
                !Read(reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0x3427C00,&renderer,8) || renderer!=value.renderer ||
                !Read(renderer+0x98,&wrapper,8) || wrapper!=value.resourceWrapper || !Read(wrapper,&pool,8) || pool!=value.resourcePool ||
                !Read(pool+2,&mode,1) || mode!=2 || replayNodeDepth==replayNodes.size())ReplayFailed(value,122);
            else{scope=++replayNodeDepth;replayNodes[scope-1]=std::move(receipt.node);++value.resourceReplayRows[replayNodes[scope-1].node.ordinal][6];
                OwnedTelemetryWrite write;++CyberpunkVR_OwnedResourceReplayRegistration[4];CyberpunkVR_OwnedResourceReplayRegistration[5]=replayPayloads.size();}
        }
    }
    originalResourceNode(payload,worker);EndResourceNode(scope);
}
CVR_DETOUR_IF("[single-pass] source-bound execution resource node",0x1E623C,ResourceNodeHook,originalResourceNode,CodeCompatible)
}
uint64_t BeforeOwnedResourceCompile(uint64_t pool,uint8_t mode,uint64_t job,uint64_t caller) noexcept{try{
    if(CyberpunkVR_OwnedExecutionClosing.load() || mode!=2 || caller!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+0xAF5A15)return 0;
    std::lock_guard lock(mutex);
    for(const auto& [group,consumer]:consumers)if(consumer->ownedState && consumer->compiler &&
        consumer->compiler->MatchesCompilerParent(job,Read) && !consumer->resourceCompiled){
        OwnedTelemetryWrite write;auto* fields=CyberpunkVR_OwnedExecutionResources;
        const auto& ticket=*consumer->ticket;
        const graph_jobs::CompilerBinding expected{ticket.Id(),ticket.Epoch(),ticket.Generation(),consumer->renderer,
            consumer->resourceWrapper,consumer->resourcePool,ticket.FrameOwner(),ticket.GraphAddress(),consumer->compilerProducerIdentity};
        if(!consumer->compiler->MatchesBinding(expected) || !consumer->declarationSnapshotReady || !consumer->declaration ||
            !consumer->declaration->Matched() || !consumer->compiler->MatchBeforeCompiler(pool,mode,job,caller,Read)){
            fields[0]=4;fields[11]=uint32_t(OwnedFailure::Resource);consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);
            PublishCompilerReceipt(*consumer);return 0;}
        fields[0]=1;fields[1]=pool;fields[2]=consumer->resourceWrapper;fields[3]=consumer->ticket->SourceGraphAddress();
        fields[4]=consumer->ticket->GraphAddress();fields[5]=consumer->originalSource.SourceFingerprint();
        fields[6]=paired_frontier::detail::Fingerprint(consumer->executionGraph,reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base));
        fields[7]=1;fields[8]=0xAF5A15;fields[9]=mode;
        const auto read=[](void*,uint64_t address,void* output,size_t bytes){return Read(address,output,bytes);};
        if(!consumer->resourceUses.CapturePool(pool,consumer->executionGraph,read)){
            fields[0]=4;fields[11]=uint32_t(consumer->resourceUses.failure);
            consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);return 0;}
        if(CyberpunkVR_OwnedResourceReplayEnable.load())CaptureReplay(*consumer);
        PublishCompilerReceipt(*consumer);return consumer->ticket->Id();}
}catch(...){OwnedReject(OwnedFailure::Resource);}return 0;}
void AfterOwnedResourceCompile(uint64_t id,uint64_t pool) noexcept{try{
    if(!id)return;std::lock_guard lock(mutex);
    for(const auto& [group,consumer]:consumers)if(consumer->ticket->Id()==id && consumer->ownedState){OwnedTelemetryWrite write;
        auto* fields=CyberpunkVR_OwnedExecutionResources;uint8_t state{};fields[0]=2;
        if(!CyberpunkVR_OwnedExecutionClosing.load() && consumer->compiler && consumer->compiler->MarkOriginalCompilerReturned() && consumer->resourcePool==pool && fields[1]==pool && Read(pool+2,&state,1) && state==2){
            fields[10]=state;fields[0]=3;consumer->resourceCompiled=true;
            if(consumer->resourceReplay){consumer->resourceReplayCompilerChecked=ReplayGrammarUnchanged(*consumer);
                if(!consumer->resourceReplayCompilerChecked)ReplayFailed(*consumer,112);else PublishReplay(*consumer);}}
        else{fields[0]=4;fields[11]=uint32_t(OwnedFailure::Resource);consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);}
        PublishCompilerReceipt(*consumer);return;}
}catch(...){OwnedReject(OwnedFailure::Resource);}}
void Match(const graph_snapshot::Snapshot& snapshot,uint64_t graph,uint64_t frameOwner,uint64_t epoch,uint64_t generation){
    std::lock_guard lock(mutex);auto* stats=CyberpunkVR_PreparationDefinitionStats;
    if(CyberpunkVR_PreparationDefinitionMode.load()!=1 || stats[0]!=2 || !sourceTicket || !sourceTicket->ConstructVerified() || !source.Ready() ||
        epoch!=CyberpunkVR_PreparationGraphJobsEpoch.load() || generation!=CyberpunkVR_PreparationPairGeneration.load() ||
        epoch!=captureEpoch || generation!=captureGeneration || graph!=source.CompositeGraph() || frameOwner!=captureOwner ||
        snapshot.failure || snapshot.nodes!=source.Source()->nodes || snapshot.edges!=source.Source()->edges)return;
    for(const auto& row:owners){const auto found=mutations.find(row.header);
        if(!row.epoch){if(found!=mutations.end())return;continue;}
        if(found==mutations.end() || found->second.inFlight || found->second.epoch!=row.epoch || found->second.kind!=row.kind)return;}
    if(!source.MatchesBefore(Read))return;
    CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);stats[14]=1;stats[0]=3;CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
}
void Poll() noexcept{try{
    if(CyberpunkVR_OwnedExecutionClosing.load())return;
    std::shared_ptr<graph_jobs::NativeRootTicket> retired;
    std::vector<std::shared_ptr<Consumer>> completed;
    {std::lock_guard lock(mutex);if(CyberpunkVR_OwnedExecutionClosing.load())return;
        if(!CyberpunkVR_PreparationDefinitionMode.load() || captureEpoch!=CyberpunkVR_PreparationGraphJobsEpoch.load())
        retired=std::move(sourceTicket);
        completed.reserve(consumers.size());
        for(auto at=consumers.begin();at!=consumers.end();){const auto& value=at->second;
            if(value->ownedState){OwnedTelemetryWrite write;auto& state=*value->ownedState;
                if(value->rejected || !value->declaration || value->declaration->Failed() ||
                    (value->compiler && value->compiler->Failed()) || state.Quarantined()){
                    state.ReportFailure(owned_graph_execution::Failure::MissingProof);CyberpunkVR_OwnedExecutionStats[0]=6;++at;continue;}
                const auto identity=state.FrozenIdentity();
                if(value->compiler && value->compiler->Captured() && value->compiler->OriginalDispatchReturned() &&
                    value->compiler->OriginalRootReturned() && CompleteGroup(reinterpret_cast<void*>(uintptr_t(value->compiler->Group()))) &&
                    (!value->compiler->Matched() || !value->compiler->OriginalCompilerReturned())){
                    state.ReportFailure(owned_graph_execution::Failure::MissingProof);OwnedReject(OwnedFailure::Resource);
                    CyberpunkVR_OwnedExecutionStats[0]=6;PublishCompilerReceipt(*value);++at;continue;}
                if(value->declaration->Complete())state.ObserveCompletion(owned_graph_execution::Path::Declaration,identity,value->declaration->Group(),true);
                if(value->completion && value->completion->Complete())state.ObserveCompletion(owned_graph_execution::Path::Execution,identity,value->completion->Group(),true);
                PublishCompilerReceipt(*value);
                if(!state.CanReleaseOwnedStorage() || !value->resourceCompiled || !value->compiler || !value->compiler->Complete()){++at;continue;}
                if(value->resourceReplay){value->resourceReplayTerminalChecked=value->resourceReplayResetSealed && value->resourceReplayResetReturned;
                    for(const auto& row:value->resourceReplayRows)value->resourceReplayTerminalChecked=value->resourceReplayTerminalChecked && row[6]==1 && row[7]==1;
                    if(!value->resourceReplayTerminalChecked)ReplayFailed(*value,113);
                    else if(!value->resourceReplay->Complete())ReplayFailed(*value,114);
                    CyberpunkVR_OwnedResourceReplayReset[7]=value->resourceReplayTerminalChecked;
                    PublishReplay(*value);}
                if(value->sceneChildScopes){++at;continue;}
                if(!value->extraReadersCompleted){bool released=true;
                    for(const auto& reader:value->extraReaders){const retirement_deferral::OwnedGraphCompletionProof proof{reader.identity,
                        value->ticket->ConstructVerified() && value->executionVerified,!state.ProducerOpen(),true,
                        state.Queued(owned_graph_execution::Path::Declaration),state.OriginalReturned(owned_graph_execution::Path::Declaration),state.Complete(owned_graph_execution::Path::Declaration),
                        state.Queued(owned_graph_execution::Path::Execution),state.OriginalReturned(owned_graph_execution::Path::Execution),state.Complete(owned_graph_execution::Path::Execution)};
                        const bool complete=reader.owner->state->CompleteExtraReader(proof);released=complete && released;
                        if(complete){auto& owner=*reader.owner;
                            owner.completionFlags=uint64_t(proof.ownedIdentityVerified) |
                                (uint64_t(proof.producerClosed && proof.originalProducerReturned)<<1) |
                                (uint64_t(proof.declarationQueued)<<2) | (uint64_t(proof.originalDeclarationReturned)<<3) |
                                (uint64_t(proof.declarationComplete)<<4) | (uint64_t(proof.executionQueued)<<5) |
                                (uint64_t(proof.originalExecutionReturned)<<6) | (uint64_t(proof.executionAggregateComplete)<<7) |
                                (uint64_t(value->compiler->Complete())<<8);
                            owner.declarationGroup=value->declaration->Group();owner.executionGroup=value->completion->Group();
                            owner.compilerGroup=value->compiler->Group();owner.completedReadersReceipt=owner.state->CompletedReaders();
                            ++CyberpunkVR_OwnedExecutionRetirementTotals[2];RetirementEvent(RetireEvent::Complete,&owner);
                            PublishAllocatorReceipt(&owner.allocatorBinding,owner.tree.Nodes().size());}}
                    if(!released){state.ReportFailure(owned_graph_execution::Failure::MissingProof);CyberpunkVR_OwnedExecutionStats[0]=6;++at;continue;}
                    value->extraReadersCompleted=true;}
                // The replaced native consumer read these frozen generations,
                // not a recaptured cache incarnation. Its real owned aggregate
                // closure also closes that earlier source-observation row.
                if(generationLedger.HasConsumer(identity.consumerId) && !generationLedger.CompleteConsumer({identity.consumerId,
                    identity.epoch,identity.pairGeneration,value->completion->OriginalReturned(),value->completion->Complete()})){
                    RejectGenerations(*value,generationLedger.Rejection());state.ReportFailure(owned_graph_execution::Failure::MissingProof);
                    CyberpunkVR_OwnedExecutionStats[0]=6;++at;continue;}
                CyberpunkVR_OwnedExecutionStats[0]=4;CyberpunkVR_OwnedExecutionStats[20]=1;
                ownedExecutionStates.Retire(identity.consumerId);value->ownedState=nullptr;
                completed.push_back(std::move(at->second));at=consumers.erase(at);++CyberpunkVR_PreparationNativeConsumers[4];continue;
            }
            // Observation-only pins can be dropped after a rejected callback
            // returns. No native definitions were adopted by this registry.
            if(value->rejected && value->callbackReturned){completed.push_back(std::move(at->second));at=consumers.erase(at);
                ++CyberpunkVR_PreparationNativeConsumers[8];continue;}
            if(!value->completion || !value->completion->Complete()){++at;continue;}
            if(value->declaration && !value->declaration->Failed() && !value->declaration->Complete()){++at;continue;}
            if(!value->generationRejected && !generationLedger.CompleteConsumer({value->ticket->Id(),value->ticket->Epoch(),value->ticket->Generation(),value->completion->OriginalReturned(),true}))
                RejectGenerations(*value,generationLedger.Rejection());
            if(sourceTicket==value->ticket){
                CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
                const uint64_t row[]{value->ticket->Id(),value->completion->Group(),value->completion->OriginalReturned(),1,
                    value->ticket->Epoch(),value->ticket->Generation(),value->ticket->FrameOwner(),value->ticket->GraphAddress()};
                std::copy_n(row,8,CyberpunkVR_PreparationDefinitionAggregate);
                if(value->declaration){const auto& declaration=*value->declaration;
                    const uint64_t fields[]{declaration.Id(),declaration.Group(),declaration.Matched(),declaration.OriginalDeclareReturned(),
                        declaration.OriginalRootReturned(),declaration.Complete(),uint32_t(declaration.Rejection()),declaration.FrameOwner()};
                    std::copy_n(fields,8,CyberpunkVR_PreparationDefinitionDeclaration);}
                CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
                PublishSelectedGenerations(*value,true);
            }
            completed.push_back(std::move(at->second));at=consumers.erase(at);++CyberpunkVR_PreparationNativeConsumers[4];
        }
        CyberpunkVR_PreparationNativeConsumers[7]=consumers.size();
        for(const auto& owner:retainedOwners)if(owner && owner->state){
            owner->ready=owner->state->NativeIntentSeen() && owner->state->ExactHeaderDetached() &&
                !owner->state->PendingReaders() && !owner->state->Quarantined() && !owner->state->DestroyAttemptConsumed();
        }
        RecycleRetainedMetadata();PublishRetirementReceipt();
        std::erase_if(closedGenerations,[](const auto& generation){return generationLedger.ForgetClosedObservation(generation.first,generation.second);});
        PublishGenerationStats();
    }
}catch(...){CyberpunkVR_PreparationGenerationCoverageLost=1;++CyberpunkVR_PreparationNativeConsumers[5];++CyberpunkVR_PreparationGenerationStats[10];CyberpunkVR_PreparationGenerationStats[15]=uint32_t(definition_generations::Failure::Allocation);}
}
void CloseOwnedExecution()noexcept{
    try{std::lock_guard lock(mutex);OwnedTelemetryWrite write;
        if(allocatorDomain.servicesActive){FailClosedIntentException(0,23);return;}
        CyberpunkVR_OwnedExecutionClosing=1;CyberpunkVR_OwnedExecutionRequest=0;CyberpunkVR_OwnedExecutionMode=0;CyberpunkVR_OwnedSceneSplitEnable=0;CyberpunkVR_SceneChildEnable=0;
        for(const auto& [group,consumer]:consumers)if(consumer->ownedState){
            consumer->ownedState->ReportFailure(owned_graph_execution::Failure::MissingProof);CyberpunkVR_OwnedExecutionStats[0]=6;}
        ownedExecutionStates.CancelSelection();retirementStates.Shutdown();PublishRetirementReceipt();
        // Registries, graphs and pins intentionally remain alive. Close grants
        // neither producer completion nor native-release/destruction authority.
    }catch(...){CyberpunkVR_OwnedExecutionClosing=1;if(allocatorServicesActive.load())FailClosedIntentException(0,24);}
}
bool QueueIdentity(uint64_t payload,uint64_t graph,uint64_t caller,uint64_t& epoch,uint64_t& generation){
    std::lock_guard lock(mutex);
    if(!sourceTicket || !source.Ready() || CyberpunkVR_PreparationDefinitionMode.load()!=1 ||
        sourceTicket->Epoch()!=CyberpunkVR_PreparationGraphJobsEpoch.load() || !sourceTicket->MatchQueued(payload,graph,caller,Read))return false;
    epoch=sourceTicket->Epoch();generation=sourceTicket->Generation();
    CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);CyberpunkVR_PreparationDefinitionTicket[7]=1;CyberpunkVR_PreparationDefinitionSeq.fetch_add(1);
    return true;
}
}
namespace cvr::stereo::scene_child {
struct Receipt {
    std::shared_ptr<definition_tracking::Consumer> owner;
    Identity identity;
    graph_parent::Context context;
    ~Receipt(){
        if(!owner)return;
        std::lock_guard lock(definition_tracking::mutex);
        if(owner->sceneChildScopes)--owner->sceneChildScopes;
    }
};
namespace {
thread_local std::shared_ptr<const Receipt> current;
thread_local uint32_t depth{};
uint64_t nextInvocation{}; // Serialized by definition_tracking::mutex.
bool Enabled(){return CyberpunkVR_SceneChildEnable.load(std::memory_order_relaxed)==1 &&
    !CyberpunkVR_OwnedExecutionClosing.load(std::memory_order_relaxed);}
bool Context(uint64_t address,graph_parent::Context& context,uint64_t& frameOwner,uint64_t& wrapper){
    using definition_tracking::Read;
    std::array<uint64_t,2> before{},after{};uint8_t flags{},again{};
    if(!address || address>UINT64_MAX-72 || !Read(address,before.data(),16) || !Read(address+0x30,&flags,1) ||
        !(flags&2) || !graph_parent::ReadContext(Read,address,context) ||
        !Read(address,after.data(),16) || !Read(address+0x30,&again,1) || before!=after || !(again&2))return false;
    frameOwner=before[0];wrapper=before[1];return frameOwner && wrapper;
}
bool Child(uint64_t address,detail::ChildKey& child){
    using definition_tracking::Read;child={};
    uint64_t table{},work{},again{};
    if(!address || (address&7) || address>UINT64_MAX-24 || !Read(address,&table,8) ||
        !table || (table&7) || table>UINT64_MAX-16 || !Read(table+8,&work,8) ||
        !work || !Read(address,&again,8) || table!=again)return false;
    child={address,table,work,0};return true;
}
bool OwnerAlive(const definition_tracking::Consumer& owner){
    return owner.sceneBothExpansion && owner.sceneBothExpansion->Ready() && owner.ticket && owner.ownedGraph && owner.ownedState &&
        !owner.rejected && !owner.generationRejected && owner.executionVerified && owner.declarationSnapshotReady &&
        !owner.extraReadersCompleted && owner.ownedState->CurrentPhase()==owned_graph_execution::Phase::Escaped &&
        owner.ownedState->ConstructVerified() && owner.ownedState->Queued(owned_graph_execution::Path::Execution);
}
bool Match(definition_tracking::Consumer& owner,uint64_t node,const graph_parent::Context& context,
           uint64_t frameOwner,uint64_t wrapper,Identity& identity,bool validateGraph){
    using namespace definition_tracking;
    if(!OwnerAlive(owner) || context.viewIndex>1 || frameOwner!=owner.ticket->FrameOwner() || wrapper!=owner.resourceWrapper)return false;
    const auto& frozen=owner.ownedState->FrozenIdentity();
    if(frozen.consumerId!=owner.ticket->Id() || frozen.epoch!=owner.ticket->Epoch() || frozen.pairGeneration!=owner.ticket->Generation() ||
        frozen.frameOwner!=frameOwner || frozen.sourceGraph!=owner.ticket->SourceGraphAddress() || frozen.executionGraph!=owner.ticket->GraphAddress())return false;
    const uint32_t view=context.viewIndex,section=view?2u:1u;
    const auto handle=view?owner.sceneBothExpansion->TailHandle(1):owner.sceneBothExpansion->PrefixHandle(0);
    const auto root=std::find_if(owner.executionGraph.nodes.begin(),owner.executionGraph.nodes.end(),
        [&](const auto& value){return value.handle==handle;});
    if(root==owner.executionGraph.nodes.end() || root->bytes[29]!=context.slot ||
        (validateGraph && !owner.ownedGraph->ValidateCompiled()))return false;
    graph_parent::Receipt parent;
    if(!graph_parent::Resolve(owner.executionGraph,context,parent))return false;
    if(parent.handle!=handle || parent.object!=owner.sceneBothViews[view].Definitions()[section-1].object ||
        parent.work!=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base)+scene_sections::WorkRva)return false;
    auto& source=owner.sceneBothSources[view];borrowed_scene_sections::CallerLifetime lifetime;
    if(!JoinSceneReader(owner,source,lifetime) || lifetime!=owner.sceneBothLifetimes[view] ||
        !owner.sceneBothViews[view].Matches(source,lifetime) || source.RootIdentity()!=owner.sceneBothExpansion->SelectedSource(view))return false;
    detail::ChildKey child;std::array<detail::ChildKey,scene_sections::ChildCount> children{};
    if(!Child(node,child) || source.Children().size()!=children.size())return false;
    for(size_t i=0;i<children.size();++i){const auto& item=source.Children()[i];children[i]={item.object,item.vtable,item.work,item.slot};}
    uint32_t slot{};if(!detail::UniqueChild(children,view,section,child,slot))return false;
    identity.consumerId=frozen.consumerId;identity.epoch=frozen.epoch;identity.pairGeneration=frozen.pairGeneration;
    identity.frameOwner=frameOwner;identity.sourceGraph=frozen.sourceGraph;identity.executionGraph=frozen.executionGraph;
    identity.rootObject=parent.object;identity.childObject=child.object;identity.childVtable=child.vtable;identity.childWork=child.work;
    identity.workContext=context.address;identity.rootHandle=parent.handle;identity.view=view;identity.section=section;
    identity.childSlot=slot;return true;
}
std::shared_ptr<const Receipt> Capture(uint64_t node,uint64_t workContext,uint32_t phase,uint64_t parentInvocation){
    using namespace definition_tracking;
    if(!Enabled() || phase>1 || !node || !SceneCodeCompatible())return {};
    std::lock_guard lock(mutex);if(!Enabled() || nextInvocation==UINT64_MAX)return {};
    graph_parent::Context context;uint64_t frameOwner{},wrapper{};
    if(!Context(workContext,context,frameOwner,wrapper))return {};
    std::shared_ptr<Consumer> selected;Identity identity;
    for(const auto& [group,owner]:consumers){Identity candidate;
        if(!Match(*owner,node,context,frameOwner,wrapper,candidate,true))continue;
        if(selected)return {};selected=owner;identity=candidate;
    }
    if(!selected || selected->sceneChildScopes==UINT32_MAX ||
        !selected->sceneBothSources[identity.view].MatchesBefore(Read))return {};
    auto receipt=std::make_shared<Receipt>();
    identity.invocation=++nextInvocation;identity.parentInvocation=parentInvocation;
    identity.thread=GetCurrentThreadId();identity.phase=phase;
    receipt->identity=identity;receipt->context=context;
    ++selected->sceneChildScopes;receipt->owner=std::move(selected);return receipt;
}
}
Scope::Scope(uint64_t node,uint64_t workContext,uint32_t phase)noexcept:
    previous_(std::move(current)),previousDepth_(depth){
    if(depth<UINT32_MAX)++depth;
    if(previousDepth_>=MaxDepth)return;
    try{current=Capture(node,workContext,phase,previous_?previous_->identity.invocation:0);}catch(...){current.reset();}
}
Scope::~Scope(){current=std::move(previous_);depth=previousDepth_;}
bool DescribeCurrent(Identity& output)noexcept try{
    output={};const auto receipt=current;
    if(!Enabled() || !receipt || receipt->identity.thread!=GetCurrentThreadId())return false;
    std::lock_guard lock(definition_tracking::mutex);
    graph_parent::Context context;uint64_t frameOwner{},wrapper{};Identity identity;
    if(!Enabled() || !receipt->owner || !receipt->owner->sceneChildScopes ||
        !Context(receipt->identity.workContext,context,frameOwner,wrapper) || context!=receipt->context ||
        !Match(*receipt->owner,receipt->identity.childObject,context,frameOwner,wrapper,identity,false))return false;
    identity.invocation=receipt->identity.invocation;identity.parentInvocation=receipt->identity.parentInvocation;
    identity.thread=receipt->identity.thread;identity.phase=receipt->identity.phase;
    if(identity!=receipt->identity)return false;output=identity;return true;
}catch(...){output={};return false;}
bool DescribeCurrentAtList(ID3D12GraphicsCommandList* list,Identity& output,native_recording::Lease& recording)noexcept try{
    output={};recording={};Identity before,after;
    if(!list || !DescribeCurrent(before))return false;
    const auto base=reinterpret_cast<uintptr_t>(cvr::detail::g_exe_base);
    const auto context=camera_upload::CurrentContext(base);native_recording::Lease lease;
    if(context.list!=reinterpret_cast<uintptr_t>(list) || !native_recording::Capture(list,context,lease) ||
        !native_recording::ValidateRecording(lease,list,context) || !DescribeCurrent(after) || after!=before ||
        camera_upload::CurrentContext(base)!=context || !native_recording::ValidateRecording(lease,list,context))return false;
    output=before;recording=std::move(lease);return true;
}catch(...){output={};recording={};return false;}
}
extern "C" __declspec(dllexport) void CyberpunkVR_CloseOwnedExecution()noexcept{cvr::stereo::definition_tracking::CloseOwnedExecution();}
extern "C" void CvrResourceContextAfter(void* destination,const void* source,uint64_t caller) noexcept{
    if(!CyberpunkVR_OwnedResourceReplayEnable.load(std::memory_order_relaxed))return;
    try{cvr::stereo::definition_tracking::ObserveResourceContext(destination,source,caller);}catch(...){}
}
