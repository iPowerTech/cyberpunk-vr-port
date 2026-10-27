#pragma once
#include "Render/ConstantTrace.hpp"
#include "Render/ConstantWriteTimeline.hpp"
#include <stdexcept>

namespace cvr::stereo::constants {
struct TraceReplayResult {
    uint64_t queries{},mismatches{};
    std::array<uint64_t,8> statuses{};
    ConstantWriteTimeline::Context read;
    std::vector<ConstantWriteTimeline::RelatedWrite> related;
};
inline TraceReplayResult ReplayConstantTrace(std::span<const TraceEvent> events,uint64_t explainPosition=0,uint64_t explainAddress=0) {
    const auto require=[](bool value){if(!value)throw std::runtime_error("Invalid constant timeline trace");};
    require(!events.empty() && events.size()<=TraceCapacity);
    ConstantWriteTimeline timeline;TraceReplayResult result;
    std::map<uint64_t,ConstantWriteTimeline::Position> positions;
    uint64_t queue{},batch{},count{};bool submitting{};std::vector<uint64_t> lists;
    for(const auto& event:events) {
        const auto& a=event.args;
        if(submitting)require(event.op==TraceOp::SubmitList || event.op==TraceOp::SubmitEnd);
        switch(event.op) {
        case TraceOp::Mark: {
            const auto point=timeline.Mark(a[0]);require(point.recording==a[1] && point.command==a[2]);break;
        }
        case TraceOp::Write:
            require(a[1]<=UINT32_MAX && a[5]<=1);
            require(timeline.Write({uint32_t(a[1]),a[2]},a[3],a[4],a[5]!=0,a[6])==a[0]);break;
        case TraceOp::Payload:require(a[0]<=UINT32_MAX);timeline.Payload(uint32_t(a[0]),a[1]);break;
        case TraceOp::Explanation:
            require(a[0]<=UINT32_MAX && a[1]<=uint64_t(ConstantWriteTimeline::Detail::CopyTooLarge));
            timeline.Explanation(uint32_t(a[0]),ConstantWriteTimeline::Detail(a[1]));break;
        case TraceOp::Alias:require(a[0]<=UINT32_MAX);timeline.Alias({uint32_t(a[0]),a[1]},{a[2],a[3],a[4]});break;
        case TraceOp::Memory:require(a[0]<=UINT32_MAX);timeline.Memory(uint32_t(a[0]),{a[1],a[2],a[3]});break;
        case TraceOp::Reset:timeline.Reset(a[0]);break;
        case TraceOp::SubmitBegin:
            require(!submitting && a[2]<=16384);queue=a[0];batch=a[1];count=a[2];lists.clear();submitting=true;break;
        case TraceOp::SubmitList:require(submitting && lists.size()<count);lists.push_back(a[0]);break;
        case TraceOp::SubmitEnd:require(submitting && lists.size()==count);timeline.Submit(queue,batch,lists);submitting=false;break;
        case TraceOp::Fence:timeline.Fence(a[0],a[1]);break;
        case TraceOp::Signal:timeline.Signal(a[0],a[1],a[2]);break;
        case TraceOp::Wait:timeline.Wait(a[0],a[1],a[2]);break;
        case TraceOp::CpuSignal:timeline.InvalidateFence(a[0]);break;
        case TraceOp::Progress:timeline.FenceProgress(a[0],a[1]);break;
        case TraceOp::ReadPosition:
            require(a[0] && a[2] && a[2]<=UINT32_MAX && !positions.contains(a[0]));positions.emplace(a[0],ConstantWriteTimeline::Position{uint32_t(a[2]),a[3]});break;
        case TraceOp::Draw:case TraceOp::Binding:require(positions.contains(a[0]));break;
        case TraceOp::Resolve: {
            require(positions.contains(a[0]) && a[3]<result.statuses.size());
            const auto selected=timeline.Resolve(positions.at(a[0]),a[1],a[2]);++result.queries;++result.statuses[size_t(selected.status)];
            if(a[0]==explainPosition && a[1]==explainAddress && result.related.empty()) {
                result.read=timeline.Describe(positions.at(a[0]));result.related=timeline.Related(positions.at(a[0]),a[1],a[2]);
            }
            result.mismatches+=uint64_t(selected.status)!=a[3] || uint64_t(selected.detail)!=a[4] || selected.payload!=a[5] || selected.offset!=a[6];break;
        }
        case TraceOp::FinalResult:require(positions.contains(a[0]));break;
        default:throw std::runtime_error("Unknown constant trace operation");
        }
    }
    require(!submitting);return result;
}
}
