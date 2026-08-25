#pragma once

#include <cstdint>
#include <cstddef>

// ================================================================================================
// The two telemetry buffers the patch sites write into, and the layout of each.
//
// WHY THIS IS A SHARED HEADER AND NOT A FIELD PER HOOK. Each trampoline is handed
// `g_telemetry + <offset>` as an IMM64 AT INSTALL TIME. The offsets therefore describe one buffer
// whose layout every site must agree about, byte for byte -- splitting them up so each hook owns
// "its" offset would let two of them drift apart with nothing to catch it.
//
// TWO RULES THAT ARE NOT NEGOTIABLE:
//
//   1. g_telemetry and g_setterTrace must stay SINGLE OBJECTS. Their addresses are baked into
//      machine code. A per-translation-unit copy compiles, links, and makes every patched site
//      write into memory nobody reads -- silently, forever.
//   2. The VirtualAlloc that fills them must run BEFORE any install. The boot does it; a hook that
//      installs earlier bakes in a null pointer.
// ================================================================================================

struct TelemetryData {
    volatile uint32_t locateHits;
    volatile uint32_t _pad1;
    volatile uintptr_t locateRbx;
    volatile float locateXmm0;
    volatile uint32_t _pad2[3];

    volatile uint32_t patchHits;
    volatile uint32_t _pad3;
    volatile uintptr_t patchRdx;
    volatile uintptr_t patchRsi;
    volatile float patchXmm0[4];

    volatile uint32_t finalHits;
    volatile uint32_t _pad4;
    volatile uintptr_t finalRsi;

    volatile uint32_t deltaHeadHits;
    volatile uint32_t _pad5;
    volatile uintptr_t deltaHeadRcx;
    volatile float deltaHeadXmm0;
    volatile uint32_t _pad6[3];

    volatile uint32_t moveXYHits;
    volatile uint32_t _pad7;
    volatile uintptr_t moveXYRsi;
    volatile float moveXYXmm0;
    volatile uint32_t _pad8[3];

    volatile uint32_t freeDeltaHits;
    volatile uint32_t _pad9;
    volatile uintptr_t freeDeltaRsi;
    volatile float freeDeltaXmm3;
    volatile uint32_t _pad10[3];
};
extern TelemetryData* g_telemetry;

inline constexpr int kLocateTelemetryOffset = static_cast<int>(offsetof(TelemetryData, locateHits));
inline constexpr int kPatchTelemetryOffset = static_cast<int>(offsetof(TelemetryData, patchHits));
inline constexpr int kFinalTelemetryOffset = static_cast<int>(offsetof(TelemetryData, finalHits));
inline constexpr int kDeltaHeadTelemetryOffset = static_cast<int>(offsetof(TelemetryData, deltaHeadHits));
inline constexpr int kMoveXYTelemetryOffset = static_cast<int>(offsetof(TelemetryData, moveXYHits));
inline constexpr int kFreeDeltaTelemetryOffset = static_cast<int>(offsetof(TelemetryData, freeDeltaHits));

struct SetterTraceData {
    volatile uint32_t metaWriteHits;
    volatile uint32_t _pad1;
    volatile uintptr_t metaWriteTemp;
    volatile uintptr_t metaWriteMeta;
    volatile uintptr_t metaWriteRsp;

    volatile uint32_t metaConsumeHits;
    volatile uint32_t _pad2;
    volatile uintptr_t metaConsumeTemp;
    volatile uintptr_t metaConsumeMeta;
    volatile uintptr_t metaConsumeRsp;

    volatile uint32_t clearHits;
    volatile uint32_t _pad3;
    volatile uintptr_t clearTemp;
    volatile uintptr_t clearReturn;
};
extern SetterTraceData* g_setterTrace;

inline constexpr int kMetaWriteTraceOffset = static_cast<int>(offsetof(SetterTraceData, metaWriteHits));
inline constexpr int kMetaConsumeTraceOffset = static_cast<int>(offsetof(SetterTraceData, metaConsumeHits));
inline constexpr int kClearTraceOffset = static_cast<int>(offsetof(SetterTraceData, clearHits));
