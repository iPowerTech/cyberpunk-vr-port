// ViewChainTimeline -- the render chain's own boundaries on the node timeline (Profiler.cpp).
//
// The node spans say WHEN each view's graph nodes run, not why nothing runs between them. The render
// scene chains everything it schedules on one handle (scene+8): the frame submit (vfunc +0x10,
// sub_14029384C), then each view's FlushRenderScene group (vfunc +0x18, sub_140293AF4, whose last stage
// is the view job sub_140AA3904), and the game thread waits on that same handle (vfunc +0x310,
// sub_1402933E4) before it builds and submits the next frame. These spans put those steps on the same
// clock as the nodes, so the gaps in the node timeline can be named.
//
// Installed only when the game is started with CVR_CHAIN_TIMELINE=1 (or CVR_JOB_TIMELINE=1), and then
// recorded only while CyberpunkVR_NodeTimeline is on; a normal run does not touch these functions.
// Kinds continue Profiler.cpp's list: 6 scene wait, 7 frame submit, 8 view flush call, 9 view job,
// 10 game-thread frame, 11 RenderFrameTick job, 12 IssueRenderSubScene scheduling one view (sub_140167FC8),
// 13 IssueRenderSubScene over the view list (sub_140167EB4), 14 the world tick (sub_14026C044, which
// schedules the world's jobs and waits for them), 15 any job-counter wait (sub_1402933F0) the game thread
// makes inside its frame. For 8, 9 and 12 the rva field carries the
// view tag: bits 0..23 = the view object's address >> 4 (which view it is), bits 24..31 = the type byte
// at +16 of the view's +4000 object (FlushRenderScene treats type 3 apart).
//
// JOB SPANS (only when the game is started with CVR_JOB_TIMELINE=1): every redJobs2 job a worker runs
// (kind 20, the indirect call in the worker loop sub_140143060 at +0x143248, patched to go through
// JobRun) and every job the game thread runs while it waits in FlushCounter (kind 21, sub_14014B164).
// rva = the job function, extra = the job's profiler name string (as an offset into the exe). For worker
// jobs bits 28..31 of rva carry the queue the worker took the job from (3 is served first, 0 last); the
// exe's code ends below 0x10000000. The patch site has no unwind info of its own, which is why it is not
// installed in a normal run.

#include "Stereo/StereoInternal.hpp"
#include "Stereo/DetourRegistry.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/Trampoline.hpp"
#include <windows.h>
#include <cstdint>
#include <intrin.h>
#include <cstring>

namespace cvr {
namespace detail {
namespace {

using SceneWaitFn = uintptr_t (*)(void*);
using FrameSubmitFn = uintptr_t (*)(void*, void*, char);
using ViewFlushFn = uintptr_t (*)(void*, int64_t*);
using ViewJobFn = void* (*)(int64_t*, void*);
using GameFrameFn = char (*)(void*, float, uint32_t*, void*);
using FrameTickFn = uintptr_t (*)(uint8_t*, void*);
using SubSceneFn = uintptr_t (*)(void*, uint8_t, int64_t*, int64_t*, int64_t*);
using SubSceneListFn = uintptr_t (*)(void*, void*, uint8_t*);
using WorldTickFn = uintptr_t (*)(void*, void*, void*);
using CounterWaitFn = uintptr_t (*)(void*);
using JobFn = void (*)(void*, void*);
using RunJobFn = uintptr_t (*)(uintptr_t, uintptr_t*, int, uint8_t, uintptr_t, uintptr_t);

SceneWaitFn g_orig_scene_wait = nullptr;
FrameSubmitFn g_orig_frame_submit = nullptr;
ViewFlushFn g_orig_view_flush = nullptr;
ViewJobFn g_orig_view_job = nullptr;
GameFrameFn g_orig_game_frame = nullptr;
FrameTickFn g_orig_frame_tick = nullptr;
SubSceneFn g_orig_sub_scene = nullptr;
SubSceneListFn g_orig_sub_scene_list = nullptr;
WorldTickFn g_orig_world_tick = nullptr;
CounterWaitFn g_orig_counter_wait = nullptr;
RunJobFn g_orig_run_job = nullptr;
thread_local bool t_in_game_frame = false;

bool On() { return CyberpunkVR_NodeTimeline != 0; }

// The same getter FlushRenderScene calls on the view (vfunc +32), then the type byte of the object at
// +4000 that FlushRenderScene checks.
uint32_t ViewTag(int64_t view) {
    __try {
        if (!view) return 0xFFFFFFFFu;
        const auto table = *reinterpret_cast<uintptr_t**>(view);
        const auto data = reinterpret_cast<uintptr_t (*)(int64_t)>(table[4])(view);
        uint32_t type = 0xFE;
        if (data) {
            const auto target = *reinterpret_cast<uintptr_t*>(data + 4000);
            type = target ? *reinterpret_cast<uint8_t*>(target + 16) : 0xFD;
        }
        return (static_cast<uint32_t>(view >> 4) & 0x00FFFFFFu) | (type << 24);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFDu;
    }
}

uintptr_t Detour_SceneWait(void* scene) {
    if (!On()) return g_orig_scene_wait(scene);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_scene_wait(scene);
    prof_timeline_add(t0, prof_now(), 0, 6);
    return r;
}

uintptr_t Detour_FrameSubmit(void* scene, void* frame, char flag) {
    if (!On()) return g_orig_frame_submit(scene, frame, flag);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_frame_submit(scene, frame, flag);
    prof_timeline_add(t0, prof_now(), 0, 7);
    return r;
}

uintptr_t Detour_ViewFlush(void* scene, int64_t* view) {
    if (!On()) return g_orig_view_flush(scene, view);
    const uint32_t tag = ViewTag(view ? *view : 0);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_view_flush(scene, view);
    prof_timeline_add(t0, prof_now(), tag, 8);
    return r;
}

void* Detour_ViewJob(int64_t* view, void* job) {
    if (!On()) return g_orig_view_job(view, job);
    const uint32_t tag = ViewTag(view ? *view : 0);
    const int64_t t0 = prof_now();
    void* r = g_orig_view_job(view, job);
    prof_timeline_add(t0, prof_now(), tag, 9);
    return r;
}

char Detour_GameFrame(void* game, float dt, uint32_t* frame, void* extra) {
    if (!On()) return g_orig_game_frame(game, dt, frame, extra);
    const int64_t t0 = prof_now();
    t_in_game_frame = true;
    const char r = g_orig_game_frame(game, dt, frame, extra);
    t_in_game_frame = false;
    prof_timeline_add(t0, prof_now(), 0, 10);
    return r;
}

uintptr_t Detour_FrameTick(uint8_t* data, void* job) {
    if (!On()) return g_orig_frame_tick(data, job);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_frame_tick(data, job);
    prof_timeline_add(t0, prof_now(), 0, 11);
    return r;
}

uintptr_t Detour_SubScene(void* scene, uint8_t prio, int64_t* after, int64_t* done, int64_t* view) {
    if (!On()) return g_orig_sub_scene(scene, prio, after, done, view);
    const uint32_t tag = ViewTag(view ? *view : 0);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_sub_scene(scene, prio, after, done, view);
    prof_timeline_add(t0, prof_now(), tag, 12);
    return r;
}

uintptr_t Detour_SubSceneList(void* scene, void* group, uint8_t* list) {
    if (!On()) return g_orig_sub_scene_list(scene, group, list);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_sub_scene_list(scene, group, list);
    prof_timeline_add(t0, prof_now(), 0, 13);
    return r;
}

uintptr_t Detour_WorldTick(void* world, void* a2, void* a3) {
    if (!On()) return g_orig_world_tick(world, a2, a3);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_world_tick(world, a2, a3);
    prof_timeline_add(t0, prof_now(), 0, 14);
    return r;
}

uintptr_t Detour_CounterWait(void* counter) {
    if (!On() || !t_in_game_frame) return g_orig_counter_wait(counter);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_counter_wait(counter);
    const int64_t t1 = prof_now();
    if (t1 - t0 > 0) prof_timeline_add(t0, t1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(g_exe_base)), 15);
    return r;
}

bool JobTimelineWanted() { return GetEnvironmentVariableW(L"CVR_JOB_TIMELINE", nullptr, 0) != 0; }
bool ChainTimelineWanted() { return JobTimelineWanted() || GetEnvironmentVariableW(L"CVR_CHAIN_TIMELINE", nullptr, 0) != 0; }

uint32_t NameRva(const char* name) {
    const auto offset = reinterpret_cast<uintptr_t>(name) - reinterpret_cast<uintptr_t>(g_exe_base);
    return name && offset < 0x10000000u ? static_cast<uint32_t>(offset) : 0xFFFFFFFFu;
}

// Called from the worker-loop patch in place of `call [rsp+20h]`, same stack, same arguments, plus the
// queue index the worker loop keeps in dil.
void JobRun(void* data, void* nameArg, JobFn fn, uint32_t queue) {
    if (!On()) { fn(data, nameArg); return; }
    const int64_t t0 = prof_now();
    fn(data, nameArg);
    const auto rva = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn) - reinterpret_cast<uintptr_t>(g_exe_base));
    prof_timeline_add(t0, prof_now(), (rva & 0x0FFFFFFFu) | ((queue & 0xFu) << 28), 20, NameRva(*static_cast<const char**>(nameArg)));
}

uintptr_t Detour_RunJob(uintptr_t dispatcher, uintptr_t* job, int a3, uint8_t priority, uintptr_t a5, uintptr_t a6) {
    if (!On() || !job) return g_orig_run_job(dispatcher, job, a3, priority, a5, a6);
    const int64_t t0 = prof_now();
    const uintptr_t r = g_orig_run_job(dispatcher, job, a3, priority, a5, a6);
    const auto desc = reinterpret_cast<const char**>(job[2]);
    prof_timeline_add(t0, prof_now(), static_cast<uint32_t>(job[0] - reinterpret_cast<uintptr_t>(g_exe_base)), 21,
                      NameRva(desc ? *desc : nullptr));
    return r;
}

// mov [rbp-80h], dil / call qword ptr [rsp+20h] -- the job call of the worker loop.
bool InstallJobCallPatch() {
    auto* site = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x143244;
    static const uint8_t expected[8] = {0x40, 0x88, 0x7D, 0x80, 0xFF, 0x54, 0x24, 0x20};
    if (memcmp(site, expected, sizeof(expected)) != 0) return false;
    auto* code = static_cast<uint8_t*>(AllocateTrampoline(site, 64));
    if (!code) return false;
    int pos = 0;
    for (uint8_t b : {0x40, 0x88, 0x7D, 0x80}) code[pos++] = b;                    // mov [rbp-80h], dil
    for (uint8_t b : {0x4C, 0x8B, 0x44, 0x24, 0x20}) code[pos++] = b;              // mov r8, [rsp+20h]
    for (uint8_t b : {0x44, 0x0F, 0xB6, 0xCF}) code[pos++] = b;                    // movzx r9d, dil
    WriteMovRaxImm64(code, pos, reinterpret_cast<uintptr_t>(&JobRun));
    code[pos++] = 0xFF; code[pos++] = 0xD0;                                         // call rax
    code[pos++] = 0xE9;                                                             // jmp past the site
    *reinterpret_cast<int32_t*>(code + pos) = static_cast<int32_t>((site + 8) - (code + pos + 4));
    pos += 4;
    DWORD old;
    VirtualProtect(site, 8, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE9;
    *reinterpret_cast<int32_t*>(site + 1) = static_cast<int32_t>(code - (site + 5));
    site[5] = site[6] = site[7] = 0x90;
    VirtualProtect(site, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 8);
    return true;
}

}  // namespace

CVR_HOOK_IF("JobTimeline", ::cvr::hooks::Stage::Boot, 5, InstallJobCallPatch, JobTimelineWanted);
CVR_DETOUR_IF("[timeline] game-thread job run sub_14014B164", 0x14B164, Detour_RunJob, g_orig_run_job, JobTimelineWanted)

CVR_DETOUR_IF("[timeline] scene wait sub_1402933E4", 0x2933E4, Detour_SceneWait, g_orig_scene_wait, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] frame submit sub_14029384C", 0x29384C, Detour_FrameSubmit, g_orig_frame_submit, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] view flush sub_140293AF4", 0x293AF4, Detour_ViewFlush, g_orig_view_flush, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] view job sub_140AA3904", 0xAA3904, Detour_ViewJob, g_orig_view_job, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] game frame sub_140292A54", 0x292A54, Detour_GameFrame, g_orig_game_frame, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] RenderFrameTick job sub_14020869C", 0x20869C, Detour_FrameTick, g_orig_frame_tick, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] sub-scene job sub_140167FC8", 0x167FC8, Detour_SubScene, g_orig_sub_scene, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] sub-scene list sub_140167EB4", 0x167EB4, Detour_SubSceneList, g_orig_sub_scene_list, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] world tick sub_14026C044", 0x26C044, Detour_WorldTick, g_orig_world_tick, ChainTimelineWanted)
CVR_DETOUR_IF("[timeline] counter wait sub_1402933F0", 0x2933F0, Detour_CounterWait, g_orig_counter_wait, ChainTimelineWanted)

}  // namespace detail
}  // namespace cvr
