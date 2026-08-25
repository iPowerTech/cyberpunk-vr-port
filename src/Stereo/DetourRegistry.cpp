#include "Stereo/DetourRegistry.hpp"
#include "Stereo/StereoInternal.hpp"
#include "Utils/StereoLog.hpp"

#include <MinHook.h>
#include <windows.h>

#include <algorithm>
#include <vector>

namespace cvr {
namespace detail {

namespace {

// A FUNCTION-LOCAL STATIC, for the same reason as the hook registry's: detours register from static
// constructors in translation units whose initialisation order is unspecified, and a namespace-scope
// head could be zeroed after the first registration and silently drop it.
EngineDetour*& Head() {
    static EngineDetour* head = nullptr;
    return head;
}

}  // namespace

EngineDetour::EngineDetour(const char* n, uintptr_t r, void* d, void** o, DetourWantedFn w)
    : name(n), rva(r), detour(d), original(o), wanted(w), next(Head()), installed(false) {
    Head() = this;
}

int InstallEngineDetours() {
    // THE MODULE BASE IS CHECKED, NOT ASSUMED. Reaching here before it is resolved would put every
    // detour at offset zero -- MinHook would happily hook whatever is there, and the symptom would be
    // a crash with our module on the stack and nothing pointing at the cause.
    if (!g_exe_base) {
        log("[detour] REFUSING to install: module base not resolved yet.");
        return -1;
    }
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        log("[detour] MH_Initialize failed: %d -- no detour installed.", (int)st);
        return -1;
    }

    std::vector<EngineDetour*> due;
    for (EngineDetour* d = Head(); d; d = d->next) {
        if (!d->installed) due.push_back(d);
    }
    // Ascending RVA rather than link order: the log then reads in the same order as a disassembly of
    // the image, which is how anybody checking these against a game build reads them.
    std::stable_sort(due.begin(), due.end(),
                     [](const EngineDetour* a, const EngineDetour* b) { return a->rva < b->rva; });

    int failed = 0;
    for (EngineDetour* d : due) {
        if (d->wanted && !d->wanted()) {
            log("[detour] %-46s skipped (disabled)", d->name);
            continue;
        }
        void* target = reinterpret_cast<void*>(g_exe_base + d->rva);
        const bool ok = MH_CreateHook(target, d->detour, d->original) == MH_OK &&
                        MH_EnableHook(target) == MH_OK;
        d->installed = ok;
        if (!ok) ++failed;
        // EVERY result, with the address. An RVA that no longer holds the expected function is a
        // game-patch problem and this line is the whole diagnosis; the old pass logged the same
        // information forty-six times, written out by hand each time.
        log("[detour] %-46s %s @%p", d->name, ok ? "ok" : "FAILED", target);
    }
    if (failed) {
        log("[detour] %d of %zu FAILED -- the behaviour behind them is absent, not degraded.",
            failed, due.size());
    }
    return failed;
}

}  // namespace detail
}  // namespace cvr
