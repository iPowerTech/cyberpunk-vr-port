#pragma once
// One rule for per-frame chatter, so a play session leaves a log a human can read.
//
// The launcher already has a DEBUG checkbox; it drives g_verboseLog and a table of probe flags
// (see debug_gate.cpp). What it did NOT cover was the routine diagnostics that log unconditionally
// -- and those are most of the file. A normal session with DEBUG=0 came out as 4512 lines, 1877 of
// them one repeating message, and the CET side was worse: 26 449 lines and 5 MB from a single
// per-frame state print in the smoking mod.
//
// LOG_THROTTLED(ms, ...) logs at most once per `ms` while DEBUG is off, and every time while it is
// on. Each call site keeps its own timer, so sites do not steal each other's budget. Nothing is
// lost silently: the count of skipped calls is available to the message through LOG_SKIPPED.
//
//     LOG_THROTTLED(5000, "[hud] adopted %p (%llu more since the last line)", res, LOG_SKIPPED);
//
// What must NOT be throttled: anything that happens once, any state CHANGE, and anything that says
// something went wrong. Those are the lines worth having in a tester's log, and they are rare by
// construction. Throttle the heartbeat, never the event.

#include <atomic>
#include <cstdint>

#include <windows.h>

// The launcher's DEBUG checkbox, applied by debug_gate.cpp, defined at global scope in vr_core.cpp.
// Declared here rather than pulled in through a header so this file stays standalone -- and with
// the same linkage as the definition, or the linker finds two different variables.
extern volatile int g_verboseLog;

namespace cvr {

struct LogThrottle {
    std::atomic<uint64_t> lastMs{0};
    std::atomic<uint64_t> skipped{0};
};

// True when this call site may log now. `outSkipped` receives how many calls were dropped since
// the last one that got through, so the message can say so instead of quietly under-reporting.
inline bool log_throttle_due(LogThrottle& t, uint32_t periodMs, uint64_t* outSkipped) {
    if (g_verboseLog) { *outSkipped = 0; return true; }
    const uint64_t now = GetTickCount64();
    const uint64_t last = t.lastMs.load(std::memory_order_relaxed);
    if (last && now - last < periodMs) {
        t.skipped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    t.lastMs.store(now, std::memory_order_relaxed);
    *outSkipped = t.skipped.exchange(0, std::memory_order_relaxed);
    return true;
}

}  // namespace cvr

// `LOG_SKIPPED` is only valid inside the body of a LOG_THROTTLED, where it names the count for
// this firing. Pass it as an argument when the message should own up to what it swallowed.
#define LOG_THROTTLED(periodMs, ...)                                             \
    do {                                                                         \
        static ::cvr::LogThrottle s_logThrottle_;                                \
        uint64_t LOG_SKIPPED = 0;                                                \
        if (::cvr::log_throttle_due(s_logThrottle_, (periodMs), &LOG_SKIPPED)) { \
            (void)LOG_SKIPPED;                                                   \
            Log(__VA_ARGS__);                                                    \
        }                                                                        \
    } while (0)

// Same, for the translation units whose logger is the lowercase `log()` wrapper.
#define LOG_THROTTLED_LC(periodMs, ...)                                          \
    do {                                                                         \
        static ::cvr::LogThrottle s_logThrottle_;                                \
        uint64_t LOG_SKIPPED = 0;                                                \
        if (::cvr::log_throttle_due(s_logThrottle_, (periodMs), &LOG_SKIPPED)) { \
            (void)LOG_SKIPPED;                                                   \
            log(__VA_ARGS__);                                                    \
        }                                                                        \
    } while (0)
