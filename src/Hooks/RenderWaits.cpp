// RenderWaits -- the waits of the game's Present thread between two Presents (include/Hooks/RenderWaits.hpp).
#include "Hooks/RenderWaits.hpp"

#include <MinHook.h>
#include <atomic>
#include <cstdint>
#include <mutex>

extern void Log(const char* fmt, ...);

namespace cvr::render_waits {
namespace {

uint64_t Qpc() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return uint64_t(t.QuadPart); }

// Everything below runs on the armed thread only, so its counts are that thread's own.
thread_local bool t_armed = false;
thread_local bool t_inHook = false;
thread_local uint64_t t_latency = 0, t_object = 0, t_condition = 0, t_address = 0, t_sleep = 0;
std::atomic<HANDLE> g_latencyObject{nullptr};

using CompareFn = BOOL(WINAPI*)(HANDLE, HANDLE);
CompareFn g_compare = nullptr;
bool IsLatency(HANDLE h) {
    const HANDLE latency = g_latencyObject.load(std::memory_order_relaxed);
    return latency && g_compare && h && g_compare(h, latency);
}

using WaitOneFn = DWORD(WINAPI*)(HANDLE, DWORD, BOOL);
using WaitManyFn = DWORD(WINAPI*)(DWORD, const HANDLE*, BOOL, DWORD, BOOL);
using SleepFn = DWORD(WINAPI*)(DWORD, BOOL);
using ConditionSrwFn = BOOL(WINAPI*)(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG);
using ConditionCsFn = BOOL(WINAPI*)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
using AddressFn = BOOL(WINAPI*)(volatile VOID*, PVOID, SIZE_T, DWORD);
WaitOneFn g_waitOne = nullptr;
WaitManyFn g_waitMany = nullptr;
SleepFn g_sleep = nullptr;
ConditionSrwFn g_conditionSrw = nullptr;
ConditionCsFn g_conditionCs = nullptr;
AddressFn g_address = nullptr;

bool Timed() { return t_armed && !t_inHook; }

DWORD WINAPI WaitOne(HANDLE h, DWORD ms, BOOL alertable) {
    if (!Timed()) return g_waitOne(h, ms, alertable);
    const uint64_t start = Qpc();
    const DWORD r = g_waitOne(h, ms, alertable);
    (IsLatency(h) ? t_latency : t_object) += Qpc() - start;
    return r;
}
DWORD WINAPI WaitMany(DWORD count, const HANDLE* handles, BOOL all, DWORD ms, BOOL alertable) {
    if (!Timed()) return g_waitMany(count, handles, all, ms, alertable);
    const uint64_t start = Qpc();
    const DWORD r = g_waitMany(count, handles, all, ms, alertable);
    bool latency = false;
    for (DWORD i = 0; handles && i < count && !latency; ++i) latency = IsLatency(handles[i]);
    (latency ? t_latency : t_object) += Qpc() - start;
    return r;
}
DWORD WINAPI SleepHook(DWORD ms, BOOL alertable) {
    if (!Timed()) return g_sleep(ms, alertable);
    const uint64_t start = Qpc();
    const DWORD r = g_sleep(ms, alertable);
    t_sleep += Qpc() - start;
    return r;
}
BOOL WINAPI ConditionSrw(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD ms, ULONG flags) {
    if (!Timed()) return g_conditionSrw(cv, lock, ms, flags);
    const uint64_t start = Qpc();
    const BOOL r = g_conditionSrw(cv, lock, ms, flags);
    t_condition += Qpc() - start;
    return r;
}
BOOL WINAPI ConditionCs(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms) {
    if (!Timed()) return g_conditionCs(cv, cs, ms);
    const uint64_t start = Qpc();
    const BOOL r = g_conditionCs(cv, cs, ms);
    t_condition += Qpc() - start;
    return r;
}
BOOL WINAPI Address(volatile VOID* address, PVOID compare, SIZE_T size, DWORD ms) {
    if (!Timed()) return g_address(address, compare, size, ms);
    const uint64_t start = Qpc();
    const BOOL r = g_address(address, compare, size, ms);
    t_address += Qpc() - start;
    return r;
}

template <class Fn>
bool HookExport(HMODULE module, const char* name, Fn detour, Fn* original) {
    void* target = module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr;
    if (!target) return false;
    if (MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); *original = nullptr; return false; }
    return true;
}

bool Install() {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;
    const HMODULE base = GetModuleHandleW(L"kernelbase.dll");
    g_compare = reinterpret_cast<CompareFn>(GetProcAddress(base, "CompareObjectHandles"));
    const bool one = HookExport(base, "WaitForSingleObjectEx", &WaitOne, &g_waitOne);
    const bool many = HookExport(base, "WaitForMultipleObjectsEx", &WaitMany, &g_waitMany);
    const bool sleep = HookExport(base, "SleepEx", &SleepHook, &g_sleep);
    const bool srw = HookExport(base, "SleepConditionVariableSRW", &ConditionSrw, &g_conditionSrw);
    const bool cs = HookExport(base, "SleepConditionVariableCS", &ConditionCs, &g_conditionCs);
    const bool address = HookExport(base, "WaitOnAddress", &Address, &g_address);
    Log("[present] wait hooks: object %d, objects %d, sleep %d, condition SRW %d / CS %d, address %d, handle compare %d\n",
        one, many, sleep, srw, cs, address, g_compare != nullptr);
    return one || many || sleep || srw || cs || address;
}

}  // namespace

void ArmPresentThread() {
    static std::once_flag once;
    static bool installed = false;
    std::call_once(once, [] { installed = Install(); });
    t_armed = installed;
}
void EnterHook() { t_inHook = true; }
void LeaveHook() { t_inHook = false; }
void SetLatencyObject(HANDLE object) {
    const HANDLE old = g_latencyObject.exchange(object, std::memory_order_relaxed);
    if (old && old != object) CloseHandle(old);
}

Waits Take() {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double toMs = 1000.0 / double(frequency.QuadPart);
    Waits w;
    w.latencyMs = double(t_latency) * toMs;
    w.objectMs = double(t_object) * toMs;
    w.conditionMs = double(t_condition) * toMs;
    w.addressMs = double(t_address) * toMs;
    w.sleepMs = double(t_sleep) * toMs;
    t_latency = t_object = t_condition = t_address = t_sleep = 0;
    return w;
}

}  // namespace cvr::render_waits
