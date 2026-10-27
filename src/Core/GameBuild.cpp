#include "Core/GameBuild.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <mutex>

#pragma comment(lib, "version.lib")

namespace cvr::game_build {

namespace {

struct Identity {
    uint32_t timestamp = 0;
    uint32_t sizeOfImage = 0;
    char productVersion[32] = "?";
    bool overridden = false;
    bool supported = false;
    char expected[64] = {};
    char running[64] = {};
};

// The product version ("2.31") as the exe's resources state it: only for the message, the match
// itself is the PE header.
void ReadProductVersion(HMODULE exe, char* out, size_t size) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(exe, path, MAX_PATH)) return;
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(path, &ignored);
    if (!bytes || bytes > (1u << 20)) return;
    auto* block = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, bytes));
    if (!block) return;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    wchar_t* text = nullptr;
    if (GetFileVersionInfoW(path, 0, bytes, block)) {
        struct { WORD language, codepage; }* langs = nullptr;
        UINT langBytes = 0;
        if (VerQueryValueW(block, L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &langBytes) &&
            langBytes >= sizeof(*langs)) {
            wchar_t key[64];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductVersion", langs[0].language, langs[0].codepage);
            if (VerQueryValueW(block, key, reinterpret_cast<void**>(&text), &len) && text && len)
                WideCharToMultiByte(CP_UTF8, 0, text, -1, out, int(size), nullptr, nullptr);
        } else if (VerQueryValueW(block, L"\\", reinterpret_cast<void**>(&info), &len) && info) {
            snprintf(out, size, "%u.%u", HIWORD(info->dwProductVersionMS), LOWORD(info->dwProductVersionMS));
        }
    }
    HeapFree(GetProcessHeap(), 0, block);
}

const Identity& Get() {
    static Identity id;
    static std::once_flag once;
    std::call_once(once, [] {
        HMODULE exe = GetModuleHandleW(nullptr);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
        if (dos && dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                reinterpret_cast<const uint8_t*>(exe) + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE &&
                nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                id.timestamp = nt->FileHeader.TimeDateStamp;
                id.sizeOfImage = nt->OptionalHeader.SizeOfImage;
            }
        }
        ReadProductVersion(exe, id.productVersion, sizeof(id.productVersion));
        wchar_t env[8]{};
        id.overridden = GetEnvironmentVariableW(L"CVR_ALLOW_UNKNOWN_BUILD", env, 8) && env[0] == L'1';
        // The other way round, for testing what a player on a patched game gets: this build is
        // treated as a stranger.
        wchar_t pretend[8]{};
        const bool stranger = GetEnvironmentVariableW(L"CVR_PRETEND_UNKNOWN_BUILD", pretend, 8) && pretend[0] == L'1';
        const bool match = !stranger && id.timestamp == kTimestamp && id.sizeOfImage == kSizeOfImage;
        id.supported = match || id.overridden;
        id.overridden = id.overridden && !match;
        snprintf(id.expected, sizeof(id.expected), "%s / %08X", kVersion, kTimestamp);
        snprintf(id.running, sizeof(id.running), "%s / %08X", id.productVersion, id.timestamp);
    });
    return id;
}

}  // namespace

bool Supported() { return Get().supported; }
bool Overridden() { return Get().overridden; }
const char* Expected() { return Get().expected; }
const char* Running() { return Get().running; }

bool Unmapped(uintptr_t rva) {
    return rva && std::find(std::begin(kUnmapped), std::end(kUnmapped), rva) != std::end(kUnmapped);
}

}  // namespace cvr::game_build
