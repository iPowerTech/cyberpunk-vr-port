#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace cvr::stereo::bounds_trace {
enum class Kind:uint32_t {Begin,WriteBegin,WriteEnd,VrcamBegin,VrcamEnd,MainBegin,MainEnd,End};
struct Record {
    uint64_t sequence{},qpc{},id{},pool{},span{};
    uint32_t kind{},thread{},caller{},node{};
    int32_t side=-1;
    uint32_t slot{},key{},flags{};
    std::array<float,6> bounds{};
    std::array<uint32_t,12> stack{};
    uint32_t result{},stackCount{};
};
static_assert(sizeof(Record)==152);
struct ParticleRecord {
    uint64_t sequence{},qpc{},id{},instance{},state{},view{};
    uint32_t kind{},thread{},caller{},node{},epoch{},stamp=UINT32_MAX,sceneCounter{},instanceIndex{};
    int32_t side=-1;
    uint32_t onscreen=UINT32_MAX,valid{},totalInstances{};
    float accumulated{};
    uint32_t anchor{};
    float systemDelta{},parameterDelta{};
};
static_assert(sizeof(ParticleRecord)==112);
inline constexpr uint32_t Capacity=4096;
bool Begin(uintptr_t pool);
void Mark(Kind,int side,uint32_t nativeCounter);
bool End();
}
extern "C" {
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_BoundsTraceCompatible,CyberpunkVR_BoundsTraceCount,CyberpunkVR_BoundsTraceLost;
__declspec(dllexport) extern cvr::stereo::bounds_trace::Record CyberpunkVR_BoundsTraceRecords[cvr::stereo::bounds_trace::Capacity];
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_ParticleTraceCompatible,CyberpunkVR_ParticleTraceCount,CyberpunkVR_ParticleTraceLost;
__declspec(dllexport) extern cvr::stereo::bounds_trace::ParticleRecord CyberpunkVR_ParticleTraceRecords[cvr::stereo::bounds_trace::Capacity];
}
