#pragma once
// WHERE THE PROCESS'S CPU GOES, THREAD BY THREAD (src/Hooks/ThreadCpu.cpp), for the frame-path report. In one scene the
// game takes 25-40% more CPU per frame while it runs at 66-70 fps than while it holds 72 (2026-11-03: 8.5-10 cores
// busy against 7-8.5): this says which threads take it -- the game's main and job threads, the port's own, the GPU
// driver's, SteamVR's client -- and whether one of them runs a core flat out.
namespace cvr::threadcpu {
// Once a window (the [present] line's), on the presenting thread: one [cpu] line with the busiest threads since the
// last call, each by its name or the module it started in, and the totals per module.
void Report(double windowMs);
}  // namespace cvr::threadcpu
