#pragma once

// The part of src/Natives/NativeState.cpp that the natives themselves reach, but that the VRIK and
// weapon-aim ABIs do not. Declarators are copied from the definitions verbatim, never retyped -- see
// Anim/VrikHook.hpp for what retyping one costs.

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>

#include <RED4ext/Scripting/Natives/Generated/Vector4.hpp>

extern HANDLE g_hMapFile;
extern RED4ext::Vector4 g_rootGraphVectorPersistentValue;
extern bool g_chunkDebugEnabled;
extern float g_animParamPersistentValue;
extern float g_rootGraphFloatPersistentValue;
extern float g_rootLiveTrackPersistentValue;
extern float g_rootMetaRigTrackPersistentValue;
extern int32_t g_animInputTestMode;
extern int32_t g_animParamPersistentLastResult;
extern int32_t g_animParamPersistentPreset;
extern int32_t g_chunkDebugBitSlots[4];
extern int32_t g_chunkDebugComponentIndex;
extern int32_t g_chunkDebugHand;
extern int32_t g_rootGraphFloatPersistentLastResult;
extern int32_t g_rootGraphFloatPersistentPreset;
extern int32_t g_rootGraphVectorPersistentLastResult;
extern int32_t g_rootGraphVectorPersistentPreset;
extern int32_t g_rootLiveTrackPersistentArrayMode;
extern int32_t g_rootLiveTrackPersistentLastResult;
extern int32_t g_rootLiveTrackPersistentPreset;
extern int32_t g_rootMetaRigTrackPersistentLastResult;
extern int32_t g_rootMetaRigTrackPersistentPreset;
extern volatile float     g_VRUserEyeHeight;
extern volatile int       g_VRNeutralizeAnimGraph;
std::string VRDiagPath(const char* name);
extern bool g_chunkDebugWasEnabled;
extern char               g_VRSmokeFingerNameL[32][48];
extern char               g_VRSmokeFingerName[32][48];
extern char        g_WeaponRigNames[512];
extern char g_paProvType[96];
extern char g_paRetType[96];
// The bound is part of the declaration because kWeaponPartN is derived from it with
// sizeof, exactly as it was when both lived in one file. Writing the count as a literal
// here would be a second source of truth -- and the first guess at it was wrong (21 for a
// table of 9), which is the whole argument against literals.
extern const char* kWeaponPartNames[9];
inline constexpr int kWeaponPartN = int(sizeof(kWeaponPartNames) / sizeof(kWeaponPartNames[0]));
extern volatile float     g_VRPairLeadTicks;
extern volatile float     g_VRPairSlewRate;
extern volatile float     g_projOrientQ[4];
extern volatile int       g_WeaponRigDiag[8];
extern volatile int       g_projFound;
extern volatile int       g_projSteer;
extern volatile int      g_paInstalled;
extern volatile int      g_paOn;
extern volatile int      g_paProvBase;
extern volatile int      g_paProvOff;
extern volatile int      g_paSwap;
extern volatile uint64_t  g_projDumpQ[40];
extern volatile uint64_t  g_projSteers;
extern volatile uint64_t g_paA1;
extern volatile uint64_t g_paA2;
extern volatile uint64_t g_paRet;
extern volatile uint64_t g_paCalls;
extern volatile uint64_t g_paEvQ[24];
extern volatile uint64_t g_paSwaps;
extern volatile uintptr_t g_paImpl;
extern volatile uintptr_t g_projCompVtbl;
extern volatile uintptr_t g_projLive;
extern volatile uintptr_t g_projOrientAddr;
