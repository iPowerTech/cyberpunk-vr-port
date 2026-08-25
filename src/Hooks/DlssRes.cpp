// DlssRes -- one hook, one file. Installed from the registry it registers itself with at the
// bottom; see Hooks/Hook.hpp for why the order is declared here rather than in a boot function.
//
// The DLSS render/target sizes, as the engine decides them. Read-only; the override it used to
// apply is gone, so this reports what the engine chose and nothing else.

#include "Core/VrCoreShared.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/Trampoline.hpp"
#include "Utils/AobScanner.hpp"
#include "Utils/MemorySafe.hpp"

#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace {
uint64_t g_dlssResHits = 0;
}  // namespace

extern "C" void __fastcall OnDLSSResCallback(void* dlssPtr) {
    g_dlssResHits++;

    const uintptr_t dlss = reinterpret_cast<uintptr_t>(dlssPtr);
    if (dlss < 0x10000) {
        return;
    }

    const uintptr_t prev = g_dlssResPtr;
    g_dlssResPtr = dlss;

    // Log a state the FIRST time it is seen, not whenever it differs from the previous call.
    //
    // The old test assumed a single DLSS state. With a second view there are two, and the
    // hook alternates between them, so "changed since last call" was true every single time
    // -- the log filled with the same two pointers in turn. Remembering which ones we have
    // already reported keeps the useful signal (a new state appeared) and drops the noise.
    bool firstSight = false;
    {
        static uintptr_t s_seen[8] = {};
        static unsigned s_seenN = 0;
        bool known = false;
        for (unsigned i = 0; i < s_seenN; ++i) {
            if (s_seen[i] == dlss) { known = true; break; }
        }
        if (!known && s_seenN < 8) {
            s_seen[s_seenN++] = dlss;
            firstSight = true;
        }
    }
    (void)prev;
    if (firstSight || ((g_dlssResHits % 600) == 1)) {
        // Log the render/target PAIR, not the three height slots. The old line printed +0x04,
        // +0x18 and +0x1C -- all the same field in triplicate -- which is why it read a tidy
        // "2560/2560/2560" while the actual render size was the lopsided 1485x2560 sitting one
        // field to the left, at +0x00. Now the shape is visible without a debugger.
        uint32_t renderW = 0, renderH = 0, targetW = 0, targetH = 0;
        ReadU32Safe(dlss + 0x00, &renderW);
        ReadU32Safe(dlss + 0x04, &renderH);
        ReadU32Safe(dlss + 0x20, &targetW);
        ReadU32Safe(dlss + 0x24, &targetH);
        const float sx = targetW ? static_cast<float>(renderW) / static_cast<float>(targetW) : 0.0f;
        const float sy = targetH ? static_cast<float>(renderH) / static_cast<float>(targetH) : 0.0f;
        // Read-only now. The override this used to report on is gone, so the line reports what
        // the ENGINE decided and nothing else -- which is all it was ever useful for.
        // On CHANGE only, same reason as the SettingsRes line: 120 identical lines a session.
        static uint32_t s_lastRW = 0, s_lastRH = 0, s_lastTW = 0, s_lastTH = 0;
        const bool changed = (renderW != s_lastRW) || (renderH != s_lastRH) ||
                             (targetW != s_lastTW) || (targetH != s_lastTH);
        s_lastRW = renderW; s_lastRH = renderH; s_lastTW = targetW; s_lastTH = targetH;
        if (changed)
        Log("DLSSRes hook: ptr=%p render=%ux%u target=%ux%u scale=%.3f/%.3f\n",
            reinterpret_cast<void*>(dlss),
            renderW, renderH, targetW, targetH, sx, sy);
    }
}

bool InstallDLSSResHook() {
    const char* pattern = "\x4C\x89\x73\x30\x89\x43\x04\x89\x43\x18\x89\x43\x1C";
    const char* mask = "xxxxxxxxxxxxx";
    uint8_t* found = static_cast<uint8_t*>(FindPattern("Cyberpunk2077.exe", pattern, mask));
    if (!found) return false;

    constexpr int replaceLen = 13;
    void* tramp = AllocateTrampoline(found, 512);
    if (!tramp) return false;

    uint8_t* code = static_cast<uint8_t*>(tramp);
    int pos = 0;

    code[pos++] = 0x4C; code[pos++] = 0x89; code[pos++] = 0x73; code[pos++] = 0x30;
    code[pos++] = 0x89; code[pos++] = 0x43; code[pos++] = 0x04;
    code[pos++] = 0x89; code[pos++] = 0x43; code[pos++] = 0x18;
    code[pos++] = 0x89; code[pos++] = 0x43; code[pos++] = 0x1C;

    code[pos++] = 0x9C;
    code[pos++] = 0x50;
    code[pos++] = 0x51;
    code[pos++] = 0x52;
    code[pos++] = 0x41; code[pos++] = 0x50;
    code[pos++] = 0x41; code[pos++] = 0x51;
    code[pos++] = 0x41; code[pos++] = 0x52;
    code[pos++] = 0x41; code[pos++] = 0x53;
    code[pos++] = 0x55;

    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x40;
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x04; code[pos++] = 0x24;
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10;
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20;
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30;

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xE5;
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xE4; code[pos++] = 0xF0;
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x20;

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xD9;
    WriteMovRaxImm64(code, pos, reinterpret_cast<uintptr_t>(OnDLSSResCallback));
    code[pos++] = 0xFF; code[pos++] = 0xD0;

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xEC;

    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x04; code[pos++] = 0x24;
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10;
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20;
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30;
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xC4; code[pos++] = 0x40;

    code[pos++] = 0x5D;
    code[pos++] = 0x41; code[pos++] = 0x5B;
    code[pos++] = 0x41; code[pos++] = 0x5A;
    code[pos++] = 0x41; code[pos++] = 0x59;
    code[pos++] = 0x41; code[pos++] = 0x58;
    code[pos++] = 0x5A;
    code[pos++] = 0x59;
    code[pos++] = 0x58;
    code[pos++] = 0x9D;

    code[pos++] = 0xE9;
    *reinterpret_cast<int32_t*>(code + pos) = static_cast<int32_t>((found + replaceLen) - (code + pos + 4));
    pos += 4;

    DWORD oldProtect;
    VirtualProtect(found, replaceLen, PAGE_EXECUTE_READWRITE, &oldProtect);
    found[0] = 0xE9;
    *reinterpret_cast<int32_t*>(found + 1) = static_cast<int32_t>(code - (found + 5));
    for (int i = 5; i < replaceLen; ++i) found[i] = 0x90;
    VirtualProtect(found, replaceLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), found, replaceLen);
    return true;
}

CVR_HOOK("DlssRes", ::cvr::hooks::Stage::Boot, 90, InstallDLSSResHook);
