#include "Hooks/Hook.hpp"

#include <vector>
#include <algorithm>

extern void Log(const char* fmt, ...);

namespace cvr::hooks {

namespace {

// A FUNCTION-LOCAL STATIC, not a namespace-scope pointer. Hooks register from static constructors
// in translation units whose initialisation order is unspecified; a namespace-scope head could be
// zeroed AFTER the first registration and silently drop it. This one is created by whoever gets
// there first.
Hook*& Head() {
    static Hook* head = nullptr;
    return head;
}

const char* StageName(Stage s) {
    switch (s) {
    case Stage::PreDevice:  return "pre-device";
    case Stage::Boot:       return "boot";
    case Stage::PostStereo: return "post-stereo";
    }
    return "?";
}

}  // namespace

Hook::Hook(const char* n, Stage s, int o, InstallFn f, EnabledFn g)
    : name(n), stage(s), order(o), install(f), enabled(g), next(Head()), installed(false) {
    Head() = this;
}

int InstallStage(Stage stage) {
    // Collected and sorted rather than walked in place: registration order is link order, which
    // is whatever the linker felt like, and `order` is the thing that was actually decided.
    std::vector<Hook*> due;
    for (Hook* h = Head(); h; h = h->next) {
        if (h->stage == stage && !h->installed) {
            due.push_back(h);
        }
    }
    // stable_sort so equal orders keep a deterministic sequence instead of the linker's.
    std::stable_sort(due.begin(), due.end(),
                     [](const Hook* a, const Hook* b) { return a->order < b->order; });

    int failed = 0;
    for (Hook* h : due) {
        // Switched off is not broken. Reported on its own line so a feature that is simply not
        // wanted never reads as a hook that stopped matching the game.
        if (h->enabled && !h->enabled()) {
            Log("[hooks] %-11s %-24s skipped (disabled)\n", StageName(stage), h->name);
            continue;
        }
        const bool ok = h->install ? h->install() : false;
        h->installed = ok;
        if (!ok) {
            ++failed;
        }
        // EVERY result, not just the failures. A hook that stops matching after a game patch is
        // the most common way this mod breaks, and the symptom -- some one feature quietly gone --
        // is unreadable without this line.
        Log("[hooks] %-11s %-24s %s\n", StageName(stage), h->name, ok ? "ok" : "FAILED");
    }
    if (failed) {
        Log("[hooks] %s: %d of %zu FAILED -- the features behind them are absent, not degraded.\n",
            StageName(stage), failed, due.size());
    }
    return failed;
}

void ReportInstalled() {
    int total = 0, ok = 0;
    for (Hook* h = Head(); h; h = h->next) {
        ++total;
        if (h->installed) ++ok;
    }
    Log("[hooks] %d of %d installed.\n", ok, total);
}

}  // namespace cvr::hooks
