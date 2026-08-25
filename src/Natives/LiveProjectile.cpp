// LiveProjectile -- lifted out of src/Natives/Natives.cpp, where it was one of four instrumentation
// subsystems sharing the tail of an 8,400-line file behind nothing but a banner comment.
//
// Finds a live projectile, dumps its target, and steers its orientation as a
// test.
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
// LIVE PROJECTILE finder + CE-target dump + orientation steer test.
// The projectile launch is native (no script-wrapper hook works). So instead: find the LIVE
// gameprojectileComponent in memory (by its instance vtable), report the absolute address of its
// worldTransform.Orientation (+0xe0) so CE "find out what writes" pinpoints the native fn that sets
// the launch direction; and a steer-test that overwrites that orientation with the controller aim.
// ============================================================================
static uintptr_t ResolveProjCompVtable() {
    if (g_projCompVtbl) return g_projCompVtbl;
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass("gameprojectileComponent") : nullptr;
    if (!cls) return 0;
    void* inst = cls->CreateInstance(true);  // leak (one-time) — just need the vtable
    if (!inst) return 0;
    __try { g_projCompVtbl = *reinterpret_cast<uintptr_t*>(inst); } __except (EXCEPTION_EXECUTE_HANDLER) { g_projCompVtbl = 0; }
    return g_projCompVtbl;
}
volatile int g_projTotal = 0;   // total vtable matches (pooled + active)
volatile int g_projValid = 0;   // matches that pass RTTI GetType()=="gameprojectileComponent"

static uintptr_t SafeReadPtr(uintptr_t a) {
    __try { return *reinterpret_cast<uintptr_t*>(a); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// SEH-guarded readability check via VirtualQuery (avoids the AV that crashed the game when a garbage
// float like 0xF51C006E was treated as a pointer and dereferenced).
bool IsReadable(uintptr_t a, size_t n) {
    if (a < 0x10000) return false;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD prot = mbi.Protect;
    if (prot & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    if (!(prot & (PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY))) return false;
    uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return (a + n) <= end;
}
// Validate a candidate is a REAL gameprojectileComponent instance — STRUCTURALLY, with NO virtual
// calls (calling GetType() on a stack transient whose first qword == vtbl crashed the game). The
// component is 0x920 bytes (RTTI dump); a real heap instance has that fully committed, a stack/RTTI
// transient does not. Plus a couple of field-plausibility checks. All reads IsReadable/SEH-gated.
static bool IsProjComp(uintptr_t obj, uintptr_t vtbl) {
    if (!IsReadable(obj, 0x920)) return false;        // full component must be committed+readable
    if (SafeReadPtr(obj) != vtbl) return false;        // first qword == the component vtable
    // entIComponent fields: name(CName)@+0x40 nonzero; id(CRUID)@+0x60 nonzero on a constructed comp.
    uint64_t nm = SafeReadPtr(obj + 0x40);
    uint64_t id = SafeReadPtr(obj + 0x60);
    if (nm == 0 && id == 0) return false;              // unconstructed/pooled blank -> skip
    // a real component sits on a heap allocation, not a thread stack: stacks are tiny regions.
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<void*>(obj), &mbi, sizeof(mbi))) return false;
    if (mbi.RegionSize < 0x20000) return false;        // skip small (stack-like) regions
    return true;
}
// returns offset of a unit-quaternion in the transform region (=orientation), or -1 if none.
// An ACTIVE flying projectile has a valid unit quat; pooled/dormant ones are zeroed/garbage.
static int DetectOrientOffset(uintptr_t comp) {
    __try {
        for (uint32_t o = 0xB0; o <= 0x140; o += 4) {
            float* q = reinterpret_cast<float*>(comp + o);
            // reject NaN/inf and require near-unit magnitude with a non-trivial quat
            float m = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
            if (m > 0.96f && m < 1.04f &&
                q[0] > -1.01f && q[0] < 1.01f && q[1] > -1.01f && q[1] < 1.01f &&
                q[2] > -1.01f && q[2] < 1.01f && q[3] > -1.01f && q[3] < 1.01f)
                return (int)o;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}
static bool ProjIsActive(uintptr_t comp) { return DetectOrientOffset(comp) >= 0; }
static uintptr_t FindLiveProjectile(uintptr_t vtbl) {
    if (!vtbl) return 0;
    // validate the cached one first — must still be a real projectile component
    if (g_projLive) {
        if (IsProjComp(g_projLive, vtbl) && ProjIsActive(g_projLive)) return g_projLive;
        g_projLive = 0;
    }
    int total = 0, valid = 0; uintptr_t firstValid = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        size_t sz = mbi.RegionSize ? mbi.RegionSize : 0x1000;
        // heap only: committed, private, plain RW, bounded — skips images and most stacks
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE && sz <= 64ull*1024*1024) {
            __try {
                uintptr_t* p = reinterpret_cast<uintptr_t*>(base);
                const size_t n = sz / 8;
                for (size_t i = 0; i < n; ++i) {
                    if (p[i] == vtbl) {
                        ++total;
                        uintptr_t comp = base + i*8;
                        if (IsProjComp(comp, vtbl)) {          // RTTI-validated real instance
                            ++valid; if (!firstValid) firstValid = comp;
                            if (ProjIsActive(comp)) { g_projLive = comp; g_projTotal = total; g_projValid = valid; return comp; }
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
        if (addr < base) break;
    }
    g_projTotal = total; g_projValid = valid;
    g_projLive = firstValid;   // a real (but maybe dormant) instance, or 0
    return firstValid;
}
// SEH-guarded raw reads (no C++ objects here, so __try is allowed). Fills the diag globals.
static void ReadProjFields(uintptr_t comp, int qoff) {
    __try {
        float* q = reinterpret_cast<float*>(comp + qoff);
        for (int i = 0; i < 4; ++i) g_projOrientQ[i] = q[i];
        for (uint32_t o = 0xC0; o < 0x160; o += 8)
            g_projDumpQ[(o - 0xC0) / 8] = *reinterpret_cast<uintptr_t*>(comp + o);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// scan-and-collect RTTI-VALIDATED projectile component instances (no stack/RTTI-struct false positives)
static int CollectProjectiles(uintptr_t vtbl, uintptr_t* out, int maxN) {
    int cnt = 0, total = 0; if (!vtbl) return 0;
    MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi))) {
        uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        size_t sz = mbi.RegionSize ? mbi.RegionSize : 0x1000;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE && sz <= 64ull*1024*1024) {
            __try {
                uintptr_t* p = reinterpret_cast<uintptr_t*>(base); const size_t n = sz/8;
                for (size_t i = 0; i < n && cnt < maxN; ++i) {
                    if (p[i] == vtbl) { ++total; uintptr_t comp = base + i*8; if (IsProjComp(comp, vtbl)) out[cnt++] = comp; }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz; if (addr < base) break; if (cnt >= maxN) break;
    }
    g_projTotal = total; g_projValid = cnt;
    return cnt;
}
static void DumpRegion(std::ofstream& out, uintptr_t obj, uint32_t lo, uint32_t hi) {
    static float fbuf[128]; static uintptr_t qbuf[64];
    int nf = 0, nq = 0;
    __try {
        for (uint32_t o = lo; o < hi; o += 4) { if (nf < 128) fbuf[nf++] = *reinterpret_cast<float*>(obj + o); }
        for (uint32_t o = lo; o < hi; o += 8) { if (nq < 64) qbuf[nq++] = *reinterpret_cast<uintptr_t*>(obj + o); }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
    int fi = 0;
    for (uint32_t o = lo; o < hi; o += 8) {
        out << "    +0x" << std::hex << o << std::dec << "  q=0x" << std::hex << qbuf[(o-lo)/8] << std::dec
            << "  f=(" << fbuf[fi] << ", " << fbuf[fi+1] << ")\n";
        fi += 2;
    }
}
void DumpVRLiveProjectile(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t) {
    aFrame->code++;
    uintptr_t vtbl = ResolveProjCompVtable();
    uintptr_t matches[16];
    int total = CollectProjectiles(vtbl, matches, 16);
    g_projTotal = total;
    std::ofstream out(VRDiagPath("vr_live_projectile.txt"), std::ios::trunc);
    out << "gameprojectileComponent vtbl=0x" << std::hex << vtbl << std::dec
        << "  rawMatches=" << g_projTotal << "  RTTI-valid=" << total << "\n";
    int activeIdx = -1, activeOff = -1;
    for (int k = 0; k < total; ++k) {
        uintptr_t comp = matches[k];
        int qoff = DetectOrientOffset(comp);
        out << "\n==== match[" << k << "] = 0x" << std::hex << comp << std::dec
            << (qoff >= 0 ? "  ACTIVE (unit-quat @+0x" : "  (no unit-quat)") ;
        if (qoff >= 0) out << std::hex << qoff << std::dec << ")";
        out << "\n";
        DumpRegion(out, comp, 0xC0, 0x160);
        // follow heap pointers one level (parentTransform/binding/entity may hold the live transform)
        static const uint32_t pofs[] = { 0x90u, 0xd8u, 0xf8u, 0x138u, 0x150u };
        for (uint32_t po : pofs) {
            uintptr_t hp = SafeReadPtr(comp + po);
            if (IsReadable(hp, 0x60)) {   // only deref genuinely-committed memory (no AV like 0xF51C006E)
                out << "  -> [+0x" << std::hex << po << "] = 0x" << hp << std::dec << "  (deref +0x00..+0x60):\n";
                DumpRegion(out, hp, 0x00, 0x60);
            }
        }
        if (qoff >= 0 && activeIdx < 0) { activeIdx = k; activeOff = qoff; }
    }
    if (activeIdx >= 0) {
        g_projLive = matches[activeIdx]; g_projFound = 1;
        g_projOrientAddr = matches[activeIdx] + activeOff;
        ReadProjFields(matches[activeIdx], activeOff);
        out << "\n*** ACTIVE match[" << activeIdx << "]; CE 'find what writes' 0x" << std::hex << g_projOrientAddr << std::dec << " ***\n";
    } else {
        g_projFound = total > 0 ? 1 : 0; if (total) g_projLive = matches[0];
        out << "\n*** no ACTIVE projectile (unit-quat) found; inspect the heap-ptr derefs above for the live transform ***\n";
    }
    if (aOut) *aOut = (activeIdx >= 0) ? 1 : (total > 0 ? 2 : 0);
}
// per-frame steer (called from onUpdate via the CET pump): set the live projectile's orientation to
// the controller aim quat (shared[53..56]). Tests whether the trajectory re-reads orientation.
static void ProjSteerTick() {
    if (!g_projSteer || !g_pSharedHands) return;
    uintptr_t vtbl = g_projCompVtbl ? g_projCompVtbl : ResolveProjCompVtable();
    uintptr_t comp = FindLiveProjectile(vtbl);
    if (!comp) return;
    int qoff = DetectOrientOffset(comp);
    if (qoff < 0) return;   // not an active projectile (no unit-quat orientation)
    __try {
        float qx=g_pSharedHands[53], qy=g_pSharedHands[54], qz=g_pSharedHands[55], qw=g_pSharedHands[56];
        if (qx*qx+qy*qy+qz*qz+qw*qw > 0.5f) {
            float* o = reinterpret_cast<float*>(comp + qoff);
            o[0]=qx; o[1]=qy; o[2]=qz; o[3]=qw;
            g_projSteers++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
void SetVRProjSteer(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t) {
    int32_t on = 0; RED4ext::GetParameter(aFrame, &on); aFrame->code++; g_projSteer = on;
}
void VRProjSteerTick(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, void*, int64_t) {
    aFrame->code++; ProjSteerTick();
}
void GetVRProjLiveDump(RED4ext::IScriptable*, RED4ext::CStackFrame* aFrame, float* aOut, int64_t) {
    int32_t idx = 0; RED4ext::GetParameter(aFrame, &idx); aFrame->code++;
    double v = 0.0;
    switch (idx) {
        case 0: v = (double)g_projFound; break;
        case 1: v = (double)g_projSteers; break;
        case 2: v = (double)(g_projLive & 0xFFFFFFFF); break;
        case 3: v = (double)((g_projLive >> 32) & 0xFFFFFFFF); break;
        case 4: v = (double)g_projOrientQ[0]; break;
        case 5: v = (double)g_projOrientQ[1]; break;
        case 6: v = (double)g_projOrientQ[2]; break;
        case 7: v = (double)g_projOrientQ[3]; break;
        case 8: v = (g_projCompVtbl != 0) ? 1.0 : 0.0; break;
        case 9: v = (double)g_projTotal; break;   // raw vtable matches (incl. stack/RTTI junk)
        case 10: v = (double)g_projValid; break;  // RTTI-validated real instances
        default: break;
    }
    if (aOut) *aOut = (float)v;
}

