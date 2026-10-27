#pragma once
#include "Render/NativeGeometryPackets.hpp"
#include "Render/NativeGraphSnapshot.hpp"
struct ID3D12CommandList;

namespace cvr::stereo::preparation {
enum class Kind:uint32_t {StageBegin=1,Gather,Filter,Finalize,StageEnd,Consume,GraphBefore,GraphAfter,CullBegin,CullEnd,RootSuppressed,EyeCopyRecorded,DrawBegin,DrawEnd,SubmitList,ResetList,RootCompleted,MaterializeBegin,MaterializeEnd,ProxyGatherBegin,ProxyGatherEnd};
struct Record {
    uint64_t sequence{},qpc{},scope{},context{},view{},storage{},input{},output{};
    uint32_t frame{},kind{};int32_t side=-1;uint32_t thread{},stage{},mode{},flags0{},flags1{},offset{},count{},status{},reserved{};
};
static_assert(sizeof(Record)==112);
constexpr uint32_t RecordCapacity=8192,PacketCapacity=524288;
struct Ticket {uint64_t epoch{},scope{},previousEpoch{};Record previous;};
bool Active();
bool RunActive();
bool Pairing();
void FrameBoundary();
Ticket BeginStage(const void* context,const void* buckets,const void* output,uint32_t flags0,uint32_t flags1,int side);
void EndStage(const Ticket&);
void Output(Kind,const void* output);
void Consume(uint32_t node,int side,const void* context,const void* span);
void Graph(const void* manager,bool after);
void Cull(const void* job,const void* output,int side,bool after,const void* definition=nullptr);
struct MaterializeTicket {uint64_t epoch{};Record row;};
MaterializeTicket BeginMaterialize(const void* task,int side);
void EndMaterialize(const MaterializeTicket&);
// Two packets per proxy: {visible ID, gather method}, {vtable, update method}.
MaterializeTicket BeginProxyGather(const void* context,const void* range,int side);
void EndProxyGather(const MaterializeTicket&);
using PrepareGraph=void*(*)(void*,void*,void*);
bool PairPrepareCompatible();
// Bounded diagnostics: mode 1 rejects an unowned RTT pair (state 4); mode 2
// selects both views under MAIN's output-owning root. Mode 3 additionally
// requires a verified warmup and one suppressed standalone VRCAM request.
void* PreparePair(PrepareGraph,void* manager,void* arguments,void* requested,uint64_t eyeName,uintptr_t mainView,bool gameplay,uintptr_t vrcamView=0);
bool SuppressRtt(const void* frameData,uint64_t eyeName,bool gameplay);
void RootCompleted(const void* frameData,uint64_t generationBefore);
void EyeCopyRecorded(const void* list,const void* source,const void* destination);
struct CopySource {uint64_t view{};uint32_t node{};int32_t side=-1;};
CopySource SetCopySource(CopySource);
// Attribute actual depth draws without enabling shader/binding capture.
struct DrawTag {uint64_t scope{};uint32_t stage{},frame{};int32_t side=-1;};
struct GraphParentRecord {
    uint64_t scope{},context{},manager{},fingerprint{},object{},work{},name{};
    uint32_t handle{},ordinal{},viewIndex{},slot{},status{},stage{};
};
static_assert(sizeof(GraphParentRecord)==80);
bool DescribeGraphParent(uint64_t context,int side,uint32_t frame,uint64_t captureEpoch,GraphParentRecord&);
void PrepareGraphParentCodeGuards(uint64_t imageBase);
void PrepareGraphJobCodeGuards();
class GraphContextScope {
public:
    explicit GraphContextScope(uint64_t context);
    ~GraphContextScope();
    GraphContextScope(const GraphContextScope&)=delete;
    GraphContextScope& operator=(const GraphContextScope&)=delete;
private:
    uint64_t previous_{};
};
bool CurrentGraphParent(int side,uint32_t frame,GraphParentRecord&);
// Compare against the current TLS parent and current frozen capture. Callers
// must separately bind the record to their live native recording generation.
bool RevalidateGraphParent(const GraphParentRecord&);
using GraphWork=uint8_t(*)(uintptr_t*,uint8_t*,void*);
uint8_t InvokeGraphContext(GraphWork,uintptr_t* node,uint8_t* context,void* arguments);
void PublishGraphParentSource(const graph_snapshot::Snapshot&,uint64_t imageBase);
void PollGraphJobs();
void SetGraphJobPoll(void(*)());
void SetSceneUpdatePoll(void(*)());
class DrawScope {
public:
    DrawScope(uint32_t node,int side,const void* arguments);
    ~DrawScope();
    DrawScope(const DrawScope&)=delete;
    DrawScope& operator=(const DrawScope&)=delete;
private:
    friend void DrawRecorded(const void*);
    friend DrawTag CurrentDrawTag();
    friend bool CurrentGraphParent(int,uint32_t,GraphParentRecord&);
    DrawScope* previous{};
    Record source;
    uint64_t captureEpoch{};
    std::array<const void*,64> lists{};
    std::array<uint32_t,64> draws{};
    uint32_t count{};
    bool valid{};
};
void DrawRecorded(const void* list);
DrawTag CurrentDrawTag();
void SubmitLists(const void* queue,uint32_t count,ID3D12CommandList* const* lists,bool serialized=false);
void ResetList(const void* list);
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationRequest,CyberpunkVR_PreparationState,CyberpunkVR_PreparationSeq;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationCount,CyberpunkVR_PreparationPacketCount,CyberpunkVR_PreparationFailure;
__declspec(dllexport) extern uint64_t CyberpunkVR_PreparationRecords,CyberpunkVR_PreparationPackets;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationPairRequest,CyberpunkVR_PreparationPairState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationPairCompatible;
__declspec(dllexport) extern uint64_t CyberpunkVR_PreparationPairData[8];
__declspec(dllexport) extern uint64_t CyberpunkVR_PreparationPairEligibility[8];
__declspec(dllexport) extern uint64_t CyberpunkVR_PreparationPairRoot[8];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationCameraTrace;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationAllStages;
__declspec(dllexport) extern uint64_t CyberpunkVR_PreparationGraphParents;
__declspec(dllexport) extern uint32_t CyberpunkVR_PreparationGraphParentCount,CyberpunkVR_PreparationGraphParentBytes;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationPairVerified,CyberpunkVR_PreparationRootSkipped,CyberpunkVR_PreparationRootCompatible;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationPairMergeLimit,CyberpunkVR_PreparationPairMerged,CyberpunkVR_PreparationPairCompleted;
__declspec(dllexport) extern std::atomic<uint64_t> CyberpunkVR_PreparationPairGeneration;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationRunRequest,CyberpunkVR_PreparationRunState,CyberpunkVR_PreparationRunStop;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationRunFailure,CyberpunkVR_PreparationRunFrames;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationRunDepth[2];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_PreparationRunEyeCopies,CyberpunkVR_PreparationRunEyeSubmissions;
}
