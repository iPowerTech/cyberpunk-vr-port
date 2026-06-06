#pragma once
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <psapi.h>

inline void DumpVTable() {
    std::ofstream out("C:\\Users\\dariulone\\Desktop\\CyberpunkVRPort\\vtable_dump.txt");
    
    HMODULE hModule = GetModuleHandleA("Cyberpunk2077.exe");
    if (!hModule) { out << "No Cyberpunk2077.exe module\n"; return; }
    
    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo))) {
        out << "No module info\n"; return;
    }
    
    uint8_t* base = reinterpret_cast<uint8_t*>(moduleInfo.lpBaseOfDll);
    DWORD size = moduleInfo.SizeOfImage;
    
    // 1. Find string "entSkinnedMeshComponent"
    const char* targetString = "entSkinnedMeshComponent";
    size_t strLen = strlen(targetString) + 1;
    
    uint8_t* strAddr = nullptr;
    for (size_t i = 0; i < size - strLen; ++i) {
        if (memcmp(base + i, targetString, strLen) == 0) {
            strAddr = base + i;
            out << "Found string at: " << std::hex << (uintptr_t)strAddr << "\n";
            break; // take first
        }
    }
    
    if (!strAddr) { out << "String not found\n"; return; }
    
    // 2. Find LEA RCX, [strAddr] or similar XREF.
    // LEA RCX, [strAddr] is usually: 48 8D 0D [rel32]
    out << "Scanning for XREFs...\n";
    std::vector<uint8_t*> xrefs;
    for (size_t i = 0; i < size - 7; ++i) {
        if (base[i] == 0x48 && base[i+1] == 0x8D && base[i+2] == 0x0D) {
            int32_t rel = *reinterpret_cast<int32_t*>(base + i + 3);
            uint8_t* target = base + i + 7 + rel;
            if (target == strAddr) {
                xrefs.push_back(base + i);
                out << "Found XREF at: " << std::hex << (uintptr_t)(base + i) << "\n";
            }
        }
        else if (base[i] == 0x48 && base[i+1] == 0x8D && base[i+2] == 0x15) { // LEA RDX
            int32_t rel = *reinterpret_cast<int32_t*>(base + i + 3);
            uint8_t* target = base + i + 7 + rel;
            if (target == strAddr) {
                xrefs.push_back(base + i);
                out << "Found XREF (RDX) at: " << std::hex << (uintptr_t)(base + i) << "\n";
            }
        }
    }
    
    // 3. For each XREF, look slightly backwards or forwards for LEA R?, [VTable]
    // Vtable load is usually LEA R?, [VTable] -> 48 8D 05 [rel32]
    out << "Scanning near XREFs for VTable...\n";
    for (auto xref : xrefs) {
        // scan 100 bytes around
        uint8_t* scanStart = xref > base + 100 ? xref - 100 : base;
        uint8_t* scanEnd = xref + 100;
        for (uint8_t* p = scanStart; p < scanEnd; ++p) {
            if (p[0] == 0x48 && p[1] == 0x8D && p[2] == 0x05) {
                int32_t rel = *reinterpret_cast<int32_t*>(p + 3);
                uint8_t* vtable = p + 7 + rel;
                
                // A valid vtable has pointers that point into the .text section.
                // Let's print the first 20 functions.
                out << "\nPotential VTable at: " << std::hex << (uintptr_t)vtable << " (from instruction at " << (uintptr_t)p << ")\n";
                uintptr_t* vt = reinterpret_cast<uintptr_t*>(vtable);
                for (int v = 0; v < 30; ++v) {
                    uintptr_t func = vt[v];
                    if (func < (uintptr_t)base || func > (uintptr_t)base + size) {
                        out << "  [" << v << "] Out of bounds: " << func << " (stopping)\n";
                        break;
                    }
                    out << "  [" << std::dec << v << std::hex << "] Func: " << func << " (Offset: " << (func - (uintptr_t)base) << ")\n";
                }
            }
        }
    }
    out.close();
}