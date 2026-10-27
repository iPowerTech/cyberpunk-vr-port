#pragma once
#include <cstdint>

namespace cvr::roomscale::sites {
// CP2077 2.31, EXE SHA256 a7de82945c03e041fc7339fcf9066224d98db2f5d80fea50f7947bb350a60991.
// See docs/cp2077-native-vr-off-21236.md and roomscale_hook_abi_20260919.md.
struct Site { uintptr_t rva; const char* bytes; };
inline constexpr Site CctMove{0x1C75B4, "488BC4488958104889701848897820554154415541564157488D68A84881EC30010000"};
inline constexpr Site Velocity{0x260881C, "40534883EC20488BDA4533C08B91C4000000488BCBE8"};
inline constexpr Site SolverCall{0x90B2EB, "E82CD5CF01"};
inline constexpr uintptr_t SolverVelocityReturn = 0x90B2F0;
inline constexpr Site SwimmingDivingCall{0x2EF1671, "E8A67171FF"};
inline constexpr uintptr_t SwimmingDivingVelocityReturn = 0x2EF1676;
inline constexpr Site SwimmingSurfaceCall{0x2EF2505, "E8126371FF"};
inline constexpr uintptr_t SwimmingSurfaceVelocityReturn = 0x2EF250A;
inline constexpr Site PhysicsProperty{0x27D19C, "488BC44889580848896810488970184889782041564883EC50418BE88BDA448B"};
inline constexpr uintptr_t PhysicalProviderVtable = 0x3098448;
inline constexpr uintptr_t PhysicalAdapterVtable = 0x3098390;
inline constexpr uintptr_t CctBackendVtable = 0x31EB5E8;
}
