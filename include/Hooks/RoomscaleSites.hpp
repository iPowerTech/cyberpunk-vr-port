#pragma once
#include <cstdint>

namespace cvr::roomscale::sites {
// CP2077 2.31, EXE SHA256 a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991.
// See docs/cp2077-native-vr-off-21236.md and roomscale_hook_abi_20260919.md.
struct Site { uintptr_t rva; const char* bytes; };
inline constexpr Site CctMove{0x2BF5A8, "488BC4488958104889701848897820554154415541564157488D68A84881EC30010000"};
inline constexpr Site Velocity{0x20479EC, "40534883EC20488BDA4533C08B91C4000000488BCBE8"};
inline constexpr Site SolverCall{0x6ABBAF, "E838BE9901"};
inline constexpr uintptr_t SolverVelocityReturn = 0x6ABBB4;
inline constexpr Site SwimmingDivingCall{0x29840C1, "E826396CFF"};
inline constexpr uintptr_t SwimmingDivingVelocityReturn = 0x29840C6;
inline constexpr Site SwimmingSurfaceCall{0x2984F51, "E8962A6CFF"};
inline constexpr uintptr_t SwimmingSurfaceVelocityReturn = 0x2984F56;
inline constexpr Site PhysicsProperty{0x23F168, "488BC44889580848896810488970184889782041564883EC50418BE88BDA448B"};
inline constexpr uintptr_t PhysicalProviderVtable = 0x2B5D3B0;
inline constexpr uintptr_t PhysicalAdapterVtable = 0x2B5D470;
inline constexpr uintptr_t CctBackendVtable = 0x2AC9B58;
}
