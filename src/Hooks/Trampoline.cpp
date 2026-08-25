#include "Hooks/Trampoline.hpp"

#include <windows.h>

// Lifted verbatim out of the core hub when the hooks were split into a file each. None of the
// arithmetic changed -- only that there is now exactly one copy of it, which is the whole point.
// See Hooks/Trampoline.hpp for why a second copy fails silently.

namespace {
uint8_t* g_arenaBase = nullptr;
size_t g_arenaOffset = 0;
}  // namespace

void* AllocateTrampoline(void* targetAddress, size_t size) {
    if (!g_arenaBase) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);

        uintptr_t target = reinterpret_cast<uintptr_t>(targetAddress);
        uintptr_t minAddr = target > 0x7FFFFFFF ? target - 0x7FFFFFFF : 0;
        uintptr_t maxAddr = target + 0x7FFFFFFF;
        if (maxAddr < target) maxAddr = UINTPTR_MAX;
        minAddr -= minAddr % sysInfo.dwAllocationGranularity;

        for (uintptr_t addr = target - sysInfo.dwAllocationGranularity; addr > minAddr; addr -= sysInfo.dwAllocationGranularity) {
            g_arenaBase = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(addr), 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (g_arenaBase) break;
        }
        if (!g_arenaBase) {
            for (uintptr_t addr = target + sysInfo.dwAllocationGranularity; addr < maxAddr; addr += sysInfo.dwAllocationGranularity) {
                g_arenaBase = static_cast<uint8_t*>(VirtualAlloc(reinterpret_cast<void*>(addr), 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                if (g_arenaBase) break;
            }
        }
    }

    if (g_arenaBase && g_arenaOffset + size <= 65536) {
        void* ret = g_arenaBase + g_arenaOffset;
        g_arenaOffset += size;
        return ret;
    }
    return nullptr;
}

void WriteMovRaxImm64(uint8_t* code, int& pos, uintptr_t value) {
    code[pos++] = 0x48;
    code[pos++] = 0xB8;
    *reinterpret_cast<uint64_t*>(code + pos) = static_cast<uint64_t>(value);
    pos += 8;
}

void WriteMovR11Imm64(uint8_t* code, int& pos, uintptr_t value) {
    code[pos++] = 0x49;
    code[pos++] = 0xBB;
    *reinterpret_cast<uint64_t*>(code + pos) = static_cast<uint64_t>(value);
    pos += 8;
}
