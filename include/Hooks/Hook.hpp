#pragma once

// ================================================================================================
// One hook per file, and a registry that installs them.
//
// WHAT THIS REPLACES. The install pass used to be a single hand-written run of ~22 calls inside
// WorkerThread. Nothing in a hook's own source said when it had to run or what it had to run after,
// so the order lived only in that one function -- and it was load-bearing in ways the code did not
// state. Two examples this project has already paid for:
//
//   * InstallViewKeyHook and the stereo module both target RVA 0x1EC404, and MinHook allows one
//     hook per address. The boot calls InitStereoOnce() immediately before InstallViewKeyHook()
//     for that reason alone. When the stereo module lost the race, the entire second view went
//     dark -- no RTV capture, no right eye, no mirror -- and nothing said why.
//   * A hook whose pattern no longer matches simply returns false. In the old pass that return
//     value was discarded, so a missing hook looked exactly like "the mod does nothing".
//
// So a hook now DECLARES its own placement, next to its own code, and the registry does the rest:
// it installs a stage in `order`, reports every result, and counts the failures. The order is
// still a decision, but it is a decision written where the hook is, and it is auditable in one
// line of log rather than by reading a 300-line boot function.
//
// HOW TO ADD ONE. Write the file, end it with:
//
//     CVR_HOOK("LodFov", cvr::hooks::Stage::Boot, 40, InstallFixLoDHook);
//
// and nothing else has to be touched -- not the boot, not CMake (the source list is a glob).
//
// STATIC INITIALISATION. Registration happens in a static constructor, which is safe here because
// the list head is a function-local static: the very first Hook to be constructed creates it. No
// order dependency between translation units, which is the usual way this pattern breaks.
// ================================================================================================

namespace cvr::hooks {

// WHEN a hook may be installed. These are real constraints, not taste.
enum class Stage : int {
    // Before the game creates its D3D12 device. Anything that must observe creation itself.
    PreDevice = 0,
    // The pattern-scan pass: the engine image is loaded and settled. Most hooks are here.
    Boot = 1,
    // After the stereo module has claimed its addresses. Anything that would otherwise race it
    // for the same site belongs here rather than in Boot with a carefully chosen order.
    PostStereo = 2,
};

// True on success. A hook that cannot find its pattern must return false rather than pretend --
// the registry is what turns that into a visible line.
using InstallFn = bool (*)();

// WANTED, which is not the same thing as SUCCEEDED. Several of these hooks are tracers and opt-in
// paths that the old boot wrapped in an `if` before calling. Folding that condition into the
// install function and returning false would report a hook nobody asked for as a FAILURE -- and a
// log that cries wolf is worse than no log at all. A hook that is switched off is SKIPPED and says
// so. Null means "always wanted".
using EnabledFn = bool (*)();

struct Hook {
    const char* name;
    Stage       stage;
    int         order;      // within a stage, ascending; ties keep registration order
    InstallFn   install;
    EnabledFn   enabled;
    Hook*       next;
    bool        installed;

    Hook(const char* n, Stage s, int o, InstallFn f, EnabledFn g);
};

// Installs every hook registered for the stage, in order, logging one line per hook.
// Returns the number that failed, so the caller can say so loudly and once.
int InstallStage(Stage stage);

// For the log line that says what the mod actually managed to do.
void ReportInstalled();

}  // namespace cvr::hooks

// The registration itself. The object is file-static: it exists to run its constructor.
#define CVR_HOOK(displayName, stageValue, orderValue, installFn)                        \
    namespace {                                                                         \
    const ::cvr::hooks::Hook g_cvrHookReg_##installFn{                                  \
        (displayName), (stageValue), (orderValue), (installFn), nullptr};               \
    }

// Same, for a hook the build or the settings may not want. `gateFn` is asked at install time; a
// false answer is reported as SKIPPED, never as a failure.
#define CVR_HOOK_IF(displayName, stageValue, orderValue, installFn, gateFn)             \
    namespace {                                                                         \
    const ::cvr::hooks::Hook g_cvrHookReg_##installFn{                                  \
        (displayName), (stageValue), (orderValue), (installFn), (gateFn)};              \
    }
