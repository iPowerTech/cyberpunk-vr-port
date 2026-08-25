// PrepareAttackHook -- lifted out of src/Natives/Natives.cpp, where it was one of four instrumentation
// subsystems sharing the tail of an 8,400-line file behind nothing but a banner comment.
//
// The PrepareAttack detour -- the lever that decides which direction a
// projectile launches in.
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/GameEngine.hpp>
#include <sstream>
#include <locale>
#include <clocale>
#include "Utils/SharedSlots.hpp"   // CyberpunkVR_Hands_Shared slot map (single source of truth)
#include <RED4ext/Containers/StaticArray.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/Scripting/Utils.hpp>
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#include <RED4ext/Scripting/Natives/Generated/WorldPosition.hpp>
#include <RED4ext/Scripting/Natives/Transform.hpp>
#include <RED4ext/Scripting/Natives/animRig.hpp>
#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>
#include <RED4ext/Scripting/Natives/Generated/Quaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimGraph.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimFeature_IK.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimFeature_MeleeIKData.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimFeature_WeaponUser.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableBool.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableContainer.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableFloat.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableInt.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableQuaternion.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableTransform.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/AnimVariableVector.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimationControlBinding.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimInputSetterAnimFeature.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimInputSetterFloat.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimInputSetterVector.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/IBinding.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/IKTargetAddEvent.hpp>
#include <RED4ext/Scripting/Natives/Generated/red/Event.hpp>
#include <RED4ext/Scripting/Natives/entEntity.hpp>
#include <RED4ext/Scripting/Natives/entAnimationControllerComponent.hpp>
#include <RED4ext/Scripting/Natives/entIPlacedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimatedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/StaticOrientationProvider.hpp>
#include <RED4ext/Scripting/Natives/worldAnimationSystem.hpp>
#include <RED4ext/Scripting/Natives/worldAnimationSystemScriptInterface.hpp>
#include <RED4ext/Scripting/Natives/entSkinnedMeshComponent.hpp>
#include <RED4ext/Scripting/Natives/entAnimationControllerComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/GarmentSkinnedMeshComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/MeshComponent.hpp>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <utility>
#include <iomanip>
#include <string>
#include "Anim/VrikHook.hpp"
#include "Anim/WeaponAim.hpp"
#include "Natives/NativeState.hpp"
#include "Natives/NativeHelpers.hpp"
#include <MinHook.h>
#include "Natives/NativeFunctions.hpp"
#include "Natives/NativeHelpers.hpp"
#include "Natives/NativeState.hpp"



// ============================================================================
// PrepareAttack HOOK — projectile launch direction lever.
// gameAttack_Projectile::PrepareAttack builds the launch event
// whose launchParams.logicalOrientationProvider sets the projectile direction (reads the camera for
// the player). We instrument it (read-only: auto-detect the event base + the OrientationProvider
// handle offset by RTTI type name), then optionally SWAP that provider to a controller-aimed
// entStaticOrientationProvider so the bullet flies down the barrel. All derefs are SEH-guarded.
// ============================================================================
static constexpr uintptr_t kPrepareAttackOffset = 0x1D912B0;
typedef uintptr_t (*PaFn)(uintptr_t, uintptr_t);
static PaFn OrigPA = nullptr;

static void PaSafeTypeName(uintptr_t p, char* out, size_t n) {
    out[0] = 0;
    if (p < 0x100000) return;
    __try {
        auto* obj = reinterpret_cast<RED4ext::IScriptable*>(p);
        auto* t = obj->GetType();
        if (t) { const char* s = t->name.ToString(); if (s) strncpy_s(out, n, s, _TRUNCATE); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}
static bool PaScanForProvider(uintptr_t base, int& outOff, char* typeOut, size_t typeN, volatile uint64_t* qdump) {
    bool found = false;
    __try {
        for (uint32_t o = 0; o < 0xC0; o += 8) {
            uintptr_t q = *reinterpret_cast<uintptr_t*>(base + o);
            if (qdump) qdump[o / 8] = q;
            if (q > 0x100000 && q < 0x7FFFFFFFFFFFull) {
                char tn[96]; PaSafeTypeName(q, tn, sizeof(tn));
                if (tn[0] && strstr(tn, "OrientationProvider")) {
                    outOff = (int)o; if (typeOut) strncpy_s(typeOut, typeN, tn, _TRUNCATE); found = true;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return found;
}

// Swap the launch event's logical (+visual) orientation provider to a controller-aimed static
// provider. Separate fn (no __try) because RED4ext::Handle has a destructor (can't unwind under SEH).
static void PaSwapProvider(uintptr_t evt, int off, const RED4ext::Quaternion& q) {
    auto hLog = CreateStaticOrientationProviderQ(q);
    if (!hLog.instance) return;
    *reinterpret_cast<RED4ext::Handle<RED4ext::ent::IOrientationProvider>*>(evt + off) = hLog;
    auto hVis = CreateStaticOrientationProviderQ(q);   // launchParams: logical@+0x18 visual@+0x38 (=+0x20)
    if (hVis.instance)
        *reinterpret_cast<RED4ext::Handle<RED4ext::ent::IOrientationProvider>*>(evt + off + 0x20) = hVis;
    g_paSwaps++;
}

extern "C" uintptr_t Hooked_PrepareAttack(uintptr_t a1, uintptr_t a2) {
    uintptr_t r = OrigPA ? OrigPA(a1, a2) : 0;
    if (!g_paOn) return r;
    g_paCalls++; g_paA1 = a1; g_paA2 = a2; g_paRet = r;
    PaSafeTypeName(r, (char*)g_paRetType, sizeof(g_paRetType));

    // Identify the launch-event base + the OrientationProvider handle offset. The PrepareAttack ABI
    // may return the event by ptr (r), by hidden-ret (r -> &Handle -> *r = event), or as 'this' (a2).
    uintptr_t cand[3] = { r, 0, a2 };
    __try { if (r > 0x100000) cand[1] = *reinterpret_cast<uintptr_t*>(r); } __except (EXCEPTION_EXECUTE_HANDLER) { cand[1] = 0; }
    int base = -1, off = -1;
    for (int b = 0; b < 3; ++b) {
        if (cand[b] < 0x100000) continue;
        int o = -1; char tn[96] = {0};
        if (PaScanForProvider(cand[b], o, tn, sizeof(tn), (b == 0 ? g_paEvQ : nullptr))) {
            base = b; off = o; strncpy_s((char*)g_paProvType, sizeof(g_paProvType), tn, _TRUNCATE);
            // dump the winning base's qwords
            __try { for (uint32_t k = 0; k < 0xC0; k += 8) g_paEvQ[k/8] = *reinterpret_cast<uintptr_t*>(cand[b] + k); } __except (EXCEPTION_EXECUTE_HANDLER) {}
            break;
        }
    }
    if (base >= 0) { g_paProvBase = base; g_paProvOff = off; }

    // SWAP: replace the detected logical (and visual) orientation provider with a controller static
    // provider so the launch aims down the barrel. Guarded: only on a confirmed detection.
    if (g_paSwap && g_paProvBase >= 0 && g_paProvOff >= 0 && g_pSharedHands) {
        RED4ext::Quaternion q;
        q.i = g_pSharedHands[53]; q.j = g_pSharedHands[54]; q.k = g_pSharedHands[55]; q.r = g_pSharedHands[56];
        const float l2 = q.i*q.i + q.j*q.j + q.k*q.k + q.r*q.r;
        if (l2 > 0.5f && l2 < 2.0f) {
            uintptr_t evt = (g_paProvBase == 0) ? r : (g_paProvBase == 1 ? cand[1] : a2);
            PaSwapProvider(evt, g_paProvOff, q);
        }
    }
    return r;
}

void InstallVRPrepareAttack(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t) {
    aFrame->code++;
    if (g_paInstalled) { if (aOut) *aOut = 2; return; }
    MH_Initialize();
    const uintptr_t modBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("Cyberpunk2077.exe"));
    if (!modBase) { if (aOut) *aOut = -1; return; }

    // The engine calls PrepareAttack as a direct C++ virtual,
    // so resolve the concrete instance-vtable impl: create a throwaway gameAttack_Projectile, read
    // *(inst) = vtable, [vtable + 0x168] = PrepareAttack impl, and hook THAT address.
    void* tgt = nullptr;
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass("gameAttack_Projectile") : nullptr;
    if (cls) {
        void* inst = cls->CreateInstance(true);
        if (inst) {
            __try {
                uintptr_t vt = *reinterpret_cast<uintptr_t*>(inst);
                if (vt >= modBase && vt < modBase + 0x10000000) {
                    uintptr_t impl = *reinterpret_cast<uintptr_t*>(vt + 0x168);
                    if (impl >= modBase && impl < modBase + 0x10000000) {
                        g_paImpl = impl;
                        tgt = reinterpret_cast<void*>(impl);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { tgt = nullptr; }
            // leak the throwaway instance (one-time, tiny) — safer than guessing the free path.
        }
    }
    // Fallback: hook the RTTI thunk (only catches script invocations).
    if (!tgt) tgt = reinterpret_cast<void*>(modBase + kPrepareAttackOffset);

    bool ok = (MH_CreateHook(tgt, reinterpret_cast<void*>(&Hooked_PrepareAttack), reinterpret_cast<void**>(&OrigPA)) == MH_OK)
           && (MH_EnableHook(tgt) == MH_OK);
    g_paInstalled = ok ? 1 : 0;
    if (aOut) *aOut = ok ? (g_paImpl ? 1 : 3) : 0;   // 1=hooked impl, 3=hooked thunk fallback
}
void SetVRPrepareAttackSwap(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t) {
    int32_t on = 0; RED4ext::GetParameter(aFrame, &on); aFrame->code++;
    g_paSwap = on;
}
void GetVRPADump(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, float* aOut, int64_t) {
    int32_t idx = 0; RED4ext::GetParameter(aFrame, &idx); aFrame->code++;
    double v = 0.0;
    switch (idx) {
        case 0: v = (double)g_paCalls; break;
        case 1: v = (double)g_paSwaps; break;
        case 2: v = (double)g_paInstalled; break;
        case 3: v = (double)g_paProvBase; break;
        case 4: v = (double)g_paProvOff; break;
        case 5: v = (double)g_paSwap; break;
        case 6: v = (g_paImpl != 0) ? 1.0 : 0.0; break;  // 1 = hooked the real instance-vtable impl
        default:
            if (idx >= 10 && idx < 34) v = (double)g_paEvQ[idx - 10];
            break;
    }
    if (aOut) *aOut = (float)v;
}

