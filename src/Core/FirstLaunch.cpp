// FirstLaunch -- the game settings this port was tuned against.
//
// Applied once, gated on a flag file. It exists because a fresh install's defaults produce a VR image
// that is wrong in ways that read as bugs in this mod: the wrong FOV, the wrong upscaler, motion blur
// and depth of field on. Every value here was chosen by testing in the headset.
//
// It is a one-shot rather than an enforcement: after the first launch the settings are the player's.

#include <windows.h>
#include <psapi.h>
#include <xinput.h>
#include "Utils/SharedSlots.hpp"   // CyberpunkVR_Hands_Shared slot map (single source of truth)
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <share.h>
#include "Utils/AobScanner.hpp"
#include "Overlay/LiveControlsUi.hpp"
#include "Overlay/LauncherDialog.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include "Runtimes/RuntimeFovCorrection.hpp"
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <iostream>
#include <MinHook.h>
#include "Hooks/SwapChain.hpp"
#include "Utils/LogThrottle.hpp"
#include "Hooks/Trampoline.hpp"
#include "Utils/MemorySafe.hpp"
#include "Core/Telemetry.hpp"
#include "Core/LiveControls.hpp"
#include "Core/VrCoreShared.hpp"
#include "Core/CoreInternal.hpp"
#include "Core/GameSettings.hpp"
#include "Camera/CameraLink.hpp"
#include "Hooks/Hook.hpp"

// Rewrite the one key in place. PersistLiveControlsUiState rewrites the whole file, but that only
// runs when the overlay saves; this has to survive a launch where the player never opens it, and
// it must not throw away anything else the file carries.
static bool WriteFirstLaunchFlag(int value) {
    char buf[16384] = {};
    size_t len = 0;
    if (FILE* f = _fsopen(g_liveControlPath, "rb", _SH_DENYNO)) {
        len = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
    }
    buf[len] = '\0';

    char out[sizeof(buf) + 32] = {};
    const char* key = "first_launch";
    const size_t keyLen = strlen(key);
    // Only at the start of a line, so the word inside a comment cannot be mistaken for the key.
    char* at = nullptr;
    for (char* p = buf; *p; ++p) {
        if ((p == buf || p[-1] == '\n') && strncmp(p, key, keyLen) == 0) { at = p; break; }
    }
    if (at) {
        const char* tail = strchr(at, '\n');
        if (!tail) tail = at + strlen(at);
        const size_t head = static_cast<size_t>(at - buf);
        memcpy(out, buf, head);
        const int n = _snprintf_s(out + head, sizeof(out) - head, _TRUNCATE,
                                  "%s=%d%s", key, value, tail);
        if (n < 0) return false;
    } else {
        _snprintf_s(out, sizeof(out), _TRUNCATE, "%s%s%s=%d\n",
                    buf, (len && buf[len - 1] != '\n') ? "\n" : "", key, value);
    }

    FILE* w = nullptr;
    if (fopen_s(&w, g_liveControlPath, "wb") != 0 || !w) return false;
    const size_t want = strlen(out);
    const size_t got = fwrite(out, 1, want, w);
    fclose(w);
    return got == want;
}

// ---- FIRST LAUNCH: install the game settings this port was tuned against ----------------------
//
// Cyberpunk's own settings do not live in the game folder. They are a single JSON under
// %LOCALAPPDATA%\CD Projekt Red\Cyberpunk 2077\UserSettings.json, and what a fresh install puts
// there is shaped for a monitor: motion blur, chromatic aberration, film grain, a 16:9 HUD and an
// upscaler preset picked for a flat screen. In a headset those range from unpleasant to unusable,
// and each one is a menu the player would otherwise have to go and find. So the port ships the
// settings it was actually developed and measured against, and installs them ONCE.
//
// Once, and provably once: first_launch lives in vrport.ini and it reads the way it is named --
// 1 means "this is the first launch, do it", and it is CLEARED to 0 only after the merge has
// succeeded, so a failure retries next launch instead of skipping forever. From then on the file
// belongs to the player: change anything in the game's own menus and it stays changed, because we
// never look at it again. Shipping a newer UserSettings.json with a release does not re-apply it
// either. Asking for it again means setting first_launch=1 by hand, a deliberate act.
//
// Merge preset values into the player's file, preserving personal settings and
// entries unknown to the preset. Back up the original before atomic installation.
//
// Called from the RED4ext entry, before the game creates its D3D12 device -- the earliest point we
// have. Whether the game has already read its settings by then is not something this can know, so
// the log says plainly that a fresh install may need one more launch for them to take.
extern "C" void ApplyFirstLaunchGameSettings() {
    InitRuntimePaths();
    EnsureLiveControlFileExists();
    PollLiveControls();
    // Once installed, game settings belong to the player. In particular, changing
    // cascade quality must never trigger replacement of the whole settings file.
    if(g_liveControls.xrFirstLaunch==0)return;

    // The shipped copy sits next to this DLL, which is the only directory the plugin owns.
    wchar_t modulePath[MAX_PATH] = {};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ApplyFirstLaunchGameSettings), &self) || !self) {
        Log("FirstLaunch: cannot locate this module -- settings not installed\n");
        return;
    }
    const auto moduleLength=GetModuleFileNameW(self,modulePath,MAX_PATH);
    if(!moduleLength || moduleLength>=MAX_PATH){Log("FirstLaunch: module path unavailable or too long\n");return;}
    const auto src=std::filesystem::path(modulePath).parent_path()/L"UserSettings.json";
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("FirstLaunch: no shipped UserSettings.json beside the plugin (%ls) -- nothing to do, "
            "leaving first_launch=1\n", src.c_str());
        return;
    }

    wchar_t local[MAX_PATH] = {};
    const auto localLength=GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (!localLength || localLength>=MAX_PATH) {
        Log("FirstLaunch: LOCALAPPDATA unavailable or too long -- settings not installed\n");
        return;
    }
    const auto dst=std::filesystem::path(local)/L"CD Projekt Red"/L"Cyberpunk 2077"/L"UserSettings.json";

    // Absent means the game has never written its settings here, and dropping ours in would be
    // guessing at a layout we have not seen. Say so and try again next launch.
    if (GetFileAttributesW(dst.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("FirstLaunch: %ls does not exist yet -- run the game once, then this applies\n", dst.c_str());
        return;
    }

    const auto installed=cvr::settings::InstallGameSettings(src,dst);
    if(!installed.ok) {
        Log("FirstLaunch: %s -- leaving first_launch=1\n",installed.error.c_str());
        return;
    }

    // Clear the one-shot flag only after successful installation.
    const bool flagged = WriteFirstLaunchFlag(0);
    if(flagged)g_liveControls.xrFirstLaunch = 0;
    Log("FirstLaunch: merged %u VR game settings; personal settings preserved\n"
        "             from %ls\n"
        "             to   %ls\n"
        "             previous settings kept at %ls\n"
        "             first_launch=0 %s -- the game may need one more launch to read them\n",
        installed.changed,src.c_str(),dst.c_str(),installed.backup.empty()?L"(already matched)":installed.backup.c_str(),
        flagged ? "written to vrport.ini" : "COULD NOT BE WRITTEN (will retry)");
}
