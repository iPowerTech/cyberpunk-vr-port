#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace cvr::stereo::packets {
struct Packet { uint64_t first{},second{}; };
enum class Status : uint32_t { Copied=0, Empty, BadArguments, BadSpan, TooLarge, Unreadable };
struct Record {
    uint64_t sequence{},beginQpc{},endQpc{},renderer{},arguments{},context{},view{},storage{},begin{},end{};
    uint32_t frame{},node{},side{},thread{},offset{},count{},status{},metadataFlags{};
    std::array<uint8_t,32> argumentBytes{};
    uint64_t nodeDefinition{},entry{},plane{};
    std::array<uint8_t,48> groupBytes{};
};
static_assert(sizeof(Packet)==16 && sizeof(Record)==216);
constexpr uint32_t RecordCapacity=256,PacketCapacity=262144,MaxSpanPackets=32768;
struct Ticket { uint64_t epoch{}; uint32_t index=UINT32_MAX; };
// Bounded, opt-in snapshot of native sorted packets before their consumption.
// Stored addresses are diagnostic identities only; no game pointer is reused.
void FrameBoundary(bool liveInputs=false);
Ticket Begin(uint32_t node,int side,const void* renderer,const void* arguments,const void* span);
void End(Ticket);
}
extern "C" {
__declspec(dllexport) extern uint32_t CyberpunkVR_GeometryPacketRecordBytes;
// 0=not checked, 1=expected native prologue, 2=unsupported executable.
// Compatibility alone is not proof that hook installation succeeded.
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryPacketHookCompatible;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryPacketRequest,CyberpunkVR_GeometryPacketState;
__declspec(dllexport) extern std::atomic<uint32_t> CyberpunkVR_GeometryPacketSeq,CyberpunkVR_GeometryPacketCount,CyberpunkVR_GeometryPacketDataCount;
__declspec(dllexport) extern cvr::stereo::packets::Record CyberpunkVR_GeometryPacketRecords[cvr::stereo::packets::RecordCapacity];
__declspec(dllexport) extern cvr::stereo::packets::Packet CyberpunkVR_GeometryPacketData[cvr::stereo::packets::PacketCapacity];
}
