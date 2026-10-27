#pragma once
#include <array>
#include <cstdint>

namespace cvr::stereo::constants {
enum class TraceOp : uint64_t {
    Mark=1, Write, Payload, Explanation, Alias, Reset, SubmitBegin, SubmitList, SubmitEnd,
    Fence, Signal, Wait, CpuSignal, Progress, ReadPosition, Draw, Binding, Resolve, FinalResult, Memory
};
struct TraceEvent {TraceOp op{};std::array<uint64_t,7> args{};};
static_assert(sizeof(TraceEvent)==64);
constexpr uint32_t TraceCapacity=131072;
}
