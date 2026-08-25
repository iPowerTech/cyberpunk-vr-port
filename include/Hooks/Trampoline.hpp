#pragma once

#include <cstdint>
#include <cstddef>

// ================================================================================================
// ONE ARENA, ONE TRANSLATION UNIT -- and this is not a style preference.
//
// An x86-64 jmp rel32 reaches +/-2 GB. To patch a site in the game image we need scratch memory
// inside that window, so the arena is VirtualAlloc'd by walking outward from the FIRST address
// anybody asks about until a reservation succeeds, and every later hook is carved from that same
// 64 KB block.
//
// The obvious move when splitting hooks into a file each is to give each file its own arena. Do
// that and the +/-2 GB pool fragments: the later installers find no reservable page, get nullptr,
// return false, and their hooks are simply ABSENT. There is no crash and no exception -- the mod
// just stops doing one of its jobs. So the arena stays here, shared, and the hook files ask for
// space rather than owning any.
//
// The emitters are here for the same reason they are used everywhere: a trampoline is written by
// hand, byte by byte, and two of these instructions are needed by nearly every site.
// ================================================================================================

// Scratch memory guaranteed to be within jmp rel32 range of `targetAddress`. nullptr when the
// arena is exhausted or could not be placed -- callers MUST check and return false, never patch.
void* AllocateTrampoline(void* targetAddress, size_t size);

// mov rax, imm64  /  mov r11, imm64 -- `pos` advances by the ten bytes written.
//
// The imm64 is usually the address of one of our own globals or callbacks, BAKED INTO THE PATCH AT
// INSTALL TIME. That is why a global fed to these must never become a per-translation-unit copy,
// a function-local static or a thread_local during a refactor: it compiles clean and the patch
// then reads the wrong memory for the rest of the process's life.
void WriteMovRaxImm64(uint8_t* code, int& pos, uintptr_t value);
void WriteMovR11Imm64(uint8_t* code, int& pos, uintptr_t value);
