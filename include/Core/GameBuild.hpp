#pragma once

#include <cstdint>

// ================================================================================================
// THE GAME BUILD THIS SOURCE BELONGS TO.
//
// Every RVA, code guard and patch site in src/ and include/ was verified against exactly one
// Cyberpunk2077.exe. RED4ext loads this plugin on any build (the plugin declares itself runtime-
// independent, which the loader needs), so nothing else would stop it from detouring the old
// addresses of a patched exe -- the crash on boot a game update would otherwise be.
//
// On another build the plugin registers its natives (the scripts that declare them must still
// load) and installs nothing: no detour, no patch, no device hook. The game runs flat and says why.
//
// tools/patchday/remap.py --apply rewrites the constants below together with the addresses, and
// lists between the UNMAPPED markers every address it could not carry over: those values are still
// the old build's, and the detour registry refuses them. See docs/patchday-runbook.md.
// ================================================================================================

namespace cvr::game_build {

inline constexpr const char* kVersion    = "2.5";
inline constexpr uint32_t    kTimestamp   = 0x6AD55DAF;   // IMAGE_FILE_HEADER::TimeDateStamp
inline constexpr uint32_t    kSizeOfImage = 0x0581B000;   // IMAGE_OPTIONAL_HEADER64::SizeOfImage
inline constexpr const char* kExeSha256  = "d36e74c47592e9c656ae250078cb40b2be63c782af5bfbeff2280d43e653fab4";

// The first entry is a placeholder (an empty array is not C++); 0 is never an engine address.
inline constexpr uint32_t kUnmapped[] = {
    // BEGIN UNMAPPED
    0x0,
    // END UNMAPPED
};

// The running exe is the build above. Read once from the loaded image's headers; also true when
// CVR_ALLOW_UNKNOWN_BUILD=1 is set in the environment (development only: every old address is then
// trusted on a build it was never checked against). CVR_PRETEND_UNKNOWN_BUILD=1 makes it false on the
// right build, to test what a player on a patched game gets.
bool Supported();
// The environment override above is what made Supported() true.
bool Overridden();
// rva is one remap.py could not carry to this build.
bool Unmapped(uintptr_t rva);
// "2.31 / 68AF45EA" for the build above and the running one, for the log and the message.
const char* Expected();
const char* Running();

}  // namespace cvr::game_build
