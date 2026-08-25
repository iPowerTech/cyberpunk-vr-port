// LodFov -- the level-of-detail cone, and the one hook whose value is NOT the render FOV.
//
// The site reads the camera's vertical FOV and the engine squares its tangent to get the
// screen-space-error term it selects detail with. Handed a VR field of view, that term grows by
// several times and every cull and detail decision fires as if the object were that much further
// away. This substitutes a value for the player's view only -- shadow maps and reflections come
// through the same site with their own FOVs and must be left alone, which is what the tolerance
// window around the applied FOV is for.

#include "Core/VrCoreShared.hpp"
#include "Hooks/Hook.hpp"
#include "Hooks/Trampoline.hpp"
#include "Utils/AobScanner.hpp"

#include <windows.h>
#include <cstdint>

namespace { uint64_t g_fixLoDHits = 0; }
extern "C" float __fastcall OnFixLoDCallback(float* rbxPtr, float originalVal) {
    g_fixLoDHits++;
    
    float result = originalVal;
    
    // PATCHA SOLO la camera principale VR (FOV ~93°)
    // Le shadow map hanno FOV diversi (es. 75°) e NON devono essere patchate
    const float targetVrFov = g_normalFovOverrideValue; // ~93.306°
    const float fovTolerance = 5.0f; // Accetta FOV tra 88° e 98°
    
    if (originalVal > (targetVrFov - fovTolerance) && 
        originalVal < (targetVrFov + fovTolerance)) {
        // Questa è la camera principale VR, patcha il LOD
        result = 3.04639287f;
    }
    // Altrimenti lascia il FOV originale (shadow map, reflection, etc.)
    
    if (g_fixLoDHits % 600 == 1) {
        Log("FixLoD: hits=%llu rbx=%p originalVal=%.6f result=%.6f isVRCamera=%d\n",
            static_cast<unsigned long long>(g_fixLoDHits),
            rbxPtr,
            originalVal,
            result,
            (originalVal > (targetVrFov - fovTolerance) && 
             originalVal < (targetVrFov + fovTolerance)) ? 1 : 0);
    }
    return result;
}

bool InstallFixLoDHook() {
    // Match VR Mod's CP2077FixLoD site. The short prefix occurs three times in
    // current Cyberpunk builds; the trailing mulss xmm0,xmm0 disambiguates it.
    const char* pattern =
        "\xF3\x0F\x10\x43\x20\xF3\x0F\x59\x05"
        "\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x59\xC0";
    const char* mask = "xxxxxxxxx????x????xxxx";
    uint8_t* found = static_cast<uint8_t*>(FindPattern("Cyberpunk2077.exe", pattern, mask));
    if (!found) {
        Log("FixLoD hook: Pattern not found!\n");
        return false;
    }

    constexpr int replaceLen = 5; // movss (5)
    void* tramp = AllocateTrampoline(found, 256);
    if (!tramp) return false;

    uint8_t* code = static_cast<uint8_t*>(tramp);
    int pos = 0;

    // --- CALL C++ CALLBACK ---
    // Save volatile registers
    code[pos++] = 0x9C; // pushfq
    code[pos++] = 0x50; // push rax
    code[pos++] = 0x51; // push rcx
    code[pos++] = 0x52; // push rdx
    code[pos++] = 0x41; code[pos++] = 0x50; // push r8
    code[pos++] = 0x41; code[pos++] = 0x51; // push r9
    code[pos++] = 0x41; code[pos++] = 0x52; // push r10
    code[pos++] = 0x41; code[pos++] = 0x53; // push r11
    code[pos++] = 0x55; // push rbp

    // Save xmm registers
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x40; // sub rsp, 40h
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x04; code[pos++] = 0x24; // movups [rsp], xmm0
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10; // movups [rsp+10h], xmm1
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20; // movups [rsp+20h], xmm2
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30; // movups [rsp+30h], xmm3

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xE5; // mov rbp, rsp
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xE4; code[pos++] = 0xF0; // and rsp, -16
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xEC; code[pos++] = 0x20; // sub rsp, 20h

    // Set arg1 (rcx) = rbx
    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xD9; // mov rcx, rbx
    // Set arg2 (xmm1) = [rbx + 20h]
    code[pos++] = 0xF3; code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x4B; code[pos++] = 0x20; // movss xmm1, [rbx+20h]

    WriteMovRaxImm64(code, pos, reinterpret_cast<uintptr_t>(OnFixLoDCallback));
    code[pos++] = 0xFF; code[pos++] = 0xD0; // call rax

    // Save returned value (xmm0) to stack slot for xmm0
    code[pos++] = 0x0F; code[pos++] = 0x11; code[pos++] = 0x45; code[pos++] = 0x00; // movups [rbp], xmm0

    code[pos++] = 0x48; code[pos++] = 0x89; code[pos++] = 0xEC; // mov rsp, rbp

    // Restore xmm registers (xmm0 will be our returned value!)
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x04; code[pos++] = 0x24; // movups xmm0, [rsp]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x4C; code[pos++] = 0x24; code[pos++] = 0x10; // movups xmm1, [rsp+10h]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x54; code[pos++] = 0x24; code[pos++] = 0x20; // movups xmm2, [rsp+20h]
    code[pos++] = 0x0F; code[pos++] = 0x10; code[pos++] = 0x5C; code[pos++] = 0x24; code[pos++] = 0x30; // movups xmm3, [rsp+30h]
    code[pos++] = 0x48; code[pos++] = 0x83; code[pos++] = 0xC4; code[pos++] = 0x40; // add rsp, 40h

    // Restore volatile registers
    code[pos++] = 0x5D; // pop rbp
    code[pos++] = 0x41; code[pos++] = 0x5B; // pop r11
    code[pos++] = 0x41; code[pos++] = 0x5A; // pop r10
    code[pos++] = 0x41; code[pos++] = 0x59; // pop r9
    code[pos++] = 0x41; code[pos++] = 0x58; // pop r8
    code[pos++] = 0x5A; // pop rdx
    code[pos++] = 0x59; // pop rcx
    code[pos++] = 0x58; // pop rax
    code[pos++] = 0x9D; // popfq

    // Jump back to found + 5
    code[pos++] = 0xE9;
    *reinterpret_cast<int32_t*>(code + pos) = static_cast<int32_t>((found + replaceLen) - (code + pos + 4));
    pos += 4;

    DWORD oldProtect;
    VirtualProtect(found, replaceLen, PAGE_EXECUTE_READWRITE, &oldProtect);
    found[0] = 0xE9;
    *reinterpret_cast<int32_t*>(found + 1) = static_cast<int32_t>(code - (found + 5));
    for (int i = 5; i < replaceLen; ++i) found[i] = 0x90; // NOP
    VirtualProtect(found, replaceLen, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), found, replaceLen);
    
    Log("FixLoD hook: Installed successfully at %p! Replaced 5 bytes with trampoline %p.\n", found, tramp);
    return true;
}

CVR_HOOK("LodFov", ::cvr::hooks::Stage::Boot, 60, InstallFixLoDHook);
