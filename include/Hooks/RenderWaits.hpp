#pragma once
// WHAT THE GAME'S PRESENT THREAD WAITS ON BETWEEN TWO PRESENTS (src/Hooks/RenderWaits.cpp), for the [present] line.
// In one scene the game's own part of a frame is 6-11 ms for a stretch and 14-22 ms for the next (2026-11-03, 72 Hz,
// GPU 55-65% busy): the 8 ms between the two is a wait, not work, and this says which kind. Only with the XR
// frame-path report on: the first use installs hooks on the system's wait functions (KernelBase), and they time only
// the thread that called ArmPresentThread, outside the port's own Present hook.
#include <windows.h>

namespace cvr::render_waits {

// The calling thread is the game's Present thread: its waits are timed from now on (hooks installed on first call).
void ArmPresentThread();
// Around the port's Present hook: waits in there are the port's own, timed elsewhere.
void EnterHook();
void LeaveHook();
// The swapchain's frame-latency waitable (a duplicate of the game's handle), so waits on it are told apart.
void SetLatencyObject(HANDLE object);

struct Waits {
    double latencyMs = 0;     // on the swapchain's frame-latency waitable
    double objectMs = 0;      // on any other kernel object (fences' events, threads, ...)
    double conditionMs = 0;   // SleepConditionVariableSRW/CS
    double addressMs = 0;     // WaitOnAddress
    double sleepMs = 0;       // SleepEx
};
// The armed thread's waits since the last call (QPC-timed), and starts the next count.
Waits Take();

}  // namespace cvr::render_waits
