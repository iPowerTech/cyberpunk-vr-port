// ProjectileRttiDump -- lifted out of src/Natives/Natives.cpp, where it was one of four instrumentation
// subsystems sharing the tail of an 8,400-line file behind nothing but a banner comment.
//
// Enumerates the native classes and properties on the projectile-aim
// path. A diagnostic: it answers "what is actually there" and writes it out.
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
// PROJECTILE-AIM RTTI ENUMERATOR (2026-06-15). Dumps the exact native classes /
// methods / properties of the projectile launch + orientation-provider chain, so we
// hook/swap the RIGHT thing by name (the "beat the registrar wall via RTTI" plan).
// Output: vr_projectile_rtti.txt. Trigger: native DumpVRProjectileRtti() (CET button).
// ============================================================================
static void DumpClassDetail(std::ofstream& out, RED4ext::CRTTISystem* rtti, const char* className)
{
    RED4ext::CClass* cls = rtti->GetClass(className);
    out << "\n==================================================\n";
    out << "CLASS " << className << (cls ? "" : "   <NOT FOUND>") << "\n";
    if (!cls) return;
    // parent chain
    out << "  parents:";
    for (RED4ext::CClass* p = cls->parent; p; p = p->parent) out << " " << p->name.ToString();
    out << "\n  size=0x" << std::hex << cls->GetSize() << std::dec << "\n";

    out << "  -- properties (name : type @offset) --\n";
    RED4ext::DynArray<RED4ext::CProperty*> props;
    cls->GetProperties(props);
    for (uint32_t i = 0; i < props.Size(); ++i) {
        auto* prop = props[i];
        if (!prop) continue;
        out << "    +0x" << std::hex << prop->valueOffset << std::dec
            << "  " << prop->name.ToString() << " : " << GetTypeNameForDump(prop->type) << "\n";
    }
    out << "  -- functions (N=native, E=event) --\n";
    for (auto* func : cls->funcs) {
        if (!func) continue;
        out << "    [" << (func->flags.isNative ? "N" : "s") << (func->flags.isEvent ? "E" : " ")
            << "] " << func->fullName.ToString();
        if (func->returnType && func->returnType->type)
            out << " -> " << GetTypeNameForDump(func->returnType->type);
        out << "  (" << func->params.Size() << " params)\n";
    }
    for (auto* func : cls->staticFuncs) {
        if (!func) continue;
        out << "    [static] " << func->fullName.ToString() << "\n";
    }
}

void DumpVRProjectileRtti(RED4ext::IScriptable* aContext, RED4ext::CStackFrame* aFrame, int32_t* aOut, int64_t a4)
{
    RED4EXT_UNUSED_PARAMETER(aContext); RED4EXT_UNUSED_PARAMETER(a4);
    aFrame->code++;
    auto* rtti = RED4ext::CRTTISystem::Get();
    if (!rtti) { if (aOut) *aOut = -1; return; }
    std::ofstream out(VRDiagPath("vr_projectile_rtti.txt"), std::ios::trunc);

    // 1) ALL orientation/position provider classes (derived from the interfaces) =
    //    candidates to instantiate (entStaticOrientationProvider) or swap into launch params.
    const char* providerBases[] = { "entIOrientationProvider", "entIPositionProvider",
                                    "gameIOrientationProvider", "gameIPositionProvider" };
    for (const char* base : providerBases) {
        auto* baseCls = rtti->GetClass(base);
        out << "\n#### derived of " << base << (baseCls ? "" : " <NOT FOUND>") << " ####\n";
        if (!baseCls) continue;
        RED4ext::DynArray<RED4ext::CClass*> derived;
        rtti->GetDerivedClasses(baseCls, derived);
        for (uint32_t i = 0; i < derived.Size(); ++i)
            if (derived[i]) out << "  - " << derived[i]->name.ToString() << "\n";
    }

    // 2) the projectile launch / attack / event chain — full detail (props + native methods).
    const char* classes[] = {
        "gameAttack_Projectile", "gameIAttack", "gamedataAttack_Projectile_Record",
        "gameprojectileObject", "gameprojectileComponent", "gameprojectileSpawnerComponent",
        "gameprojectileLauncherComponent",
        "gameprojectileShootEvent", "gameprojectileSetUpEvent", "gameprojectileSetUpAndLaunchEvent",
        "gameprojectileLaunchEvent", "gameprojectileLaunchParams", "gameprojectileWeaponParams",
        "gameprojectileTrajectoryParams", "gameprojectileLinearTrajectoryParams",
        "entStaticOrientationProvider", "entStaticPositionProvider",
        "entEntityOrientationProvider", "entEntityPositionProvider",
        "gameuiWeaponShootParams", "gameTargetingSystem",
    };
    for (const char* c : classes) DumpClassDetail(out, rtti, c);

    out.close();
    if (aOut) *aOut = 1;
}

