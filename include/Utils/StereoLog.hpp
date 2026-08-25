// cvr::log for the in-proxy build of sync_stereo.
//
// The standalone plugin opened cyberpunkvrport_stereo.log next to its own DLL. Inside
// dxgi.dll that would mean a SECOND log file beside the one the user actually reads, with
// the stereo hooks' discovery lines split away from the depth/submit lines they have to be
// correlated with. So this forwards to the project logger instead: everything lands in
// cyberpunkvrport.log, tagged [stereo] so it stays greppable.
//
// sync_stereo carries ~140 log sites, most of them RE instrumentation that fires on
// discovery rather than per frame -- but "most" is not "all", and a detour that starts
// logging every dispatch would bury the log. Hence the two guards below: a live switch and
// a hard rate cap.

#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>

// Project logger (vr_core.cpp). Does NOT append a newline -- callers do.
extern void Log(const char* fmt, ...);

// Live switch, readable/writable from the overlay and the debugger. On by default: the
// install sequence is the first thing to check when stereo does not come up.
extern "C" __declspec(dllexport) extern int32_t CyberpunkVR_StereoLog;

namespace cvr {

// Ceiling on how much sync_stereo may write per second. A runaway detour then costs a
// bounded number of lines instead of the whole file.
inline bool log_rate_ok() {
    static ULONGLONG windowStart = 0;
    static unsigned inWindow = 0;
    static bool announced = false;
    const ULONGLONG now = GetTickCount64();
    if (now - windowStart >= 1000) {
        if (announced) {
            Log("[stereo] (log rate cap hit, %u lines suppressed in the last window)\n",
                inWindow > 200 ? inWindow - 200 : 0u);
            announced = false;
        }
        windowStart = now;
        inWindow = 0;
    }
    if (++inWindow > 200) {
        announced = true;
        return false;
    }
    return true;
}

inline void log(const char* fmt, ...) {
    if (!CyberpunkVR_StereoLog) return;
    if (!log_rate_ok()) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    Log("[stereo] %s\n", buf);
}

} // namespace cvr
