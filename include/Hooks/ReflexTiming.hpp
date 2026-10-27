#pragma once
#include <cstddef>
#include <cstdint>

namespace cvr::reflex {
// Public Streamline ReflexReport/ReflexState v1 ABI. Request the backward-
// compatible v1 prefix; these structures contain no owned pointers/resources.
struct TimingFrame {
    void* next{};
    uint64_t type[2]{0x4453a1c80d569b37ULL,0x2b9557def4404dbeULL},version{1};
    uint64_t frameId{},input{},simStart{},simEnd{},renderStart{},renderEnd{},presentStart{},presentEnd{};
    uint64_t driverStart{},driverEnd{},queueStart{},queueEnd{},gpuStart{},gpuEnd{};
    uint32_t gpuActiveUs{},gpuFrameUs{};
};
struct TimingState {
    void* next{};
    uint64_t type[2]{0x4728daf9f0bb5985ULL,0x8979bda280aefdb2ULL},version{1};
    bool lowLatencyAvailable{},latencyReportAvailable{};
    uint16_t padding{};
    uint32_t statsWindowMessage{};
    TimingFrame frames[64];
    bool flashIndicatorDriverControlled{};
};
static_assert(sizeof(TimingFrame)==152 && offsetof(TimingFrame,gpuStart)==128 &&
              offsetof(TimingFrame,gpuActiveUs)==144);
static_assert(sizeof(TimingState)==9776 && offsetof(TimingState,frames)==40 &&
              offsetof(TimingState,flashIndicatorDriverControlled)==9768);
}
