// ThreadCpu -- the [cpu] line (include/Hooks/ThreadCpu.hpp).
#include "Hooks/ThreadCpu.hpp"

#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern void Log(const char* fmt, ...);

namespace cvr::threadcpu {
namespace {

using QueryThreadFn = LONG(WINAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
using DescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

struct Seen {
    uint64_t cpu = 0;        // kernel + user, 100 ns
    std::string label;       // name, or module of the start address
};
std::unordered_map<DWORD, Seen> g_seen;

std::string ModuleOf(void* address) {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(address), &module))
        return "?";
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\');
    base = base ? base + 1 : path;
    char out[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8, 0, base, -1, out, sizeof out, nullptr, nullptr);
    return out;
}

std::string LabelOf(HANDLE thread) {
    static const auto query = reinterpret_cast<QueryThreadFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    static const auto describe = reinterpret_cast<DescriptionFn>(
        GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetThreadDescription"));
    std::string name;
    PWSTR description = nullptr;
    if (describe && SUCCEEDED(describe(thread, &description)) && description) {
        char out[128]{};
        WideCharToMultiByte(CP_UTF8, 0, description, -1, out, sizeof out, nullptr, nullptr);
        name = out;
        LocalFree(description);
    }
    void* start = nullptr;
    if (query) query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, nullptr);
    const std::string module = ModuleOf(start);
    return name.empty() ? module : name + " (" + module + ")";
}

}  // namespace

void Report(double windowMs) {
    if (windowMs <= 0) return;
    const DWORD self = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    struct Busy { double percent; DWORD id; const std::string* label; };
    std::vector<Busy> busy;
    std::unordered_map<std::string, double> perModule;
    std::unordered_map<DWORD, Seen> now;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    double total = 0;
    unsigned threads = 0, flatOut = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != self) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        FILETIME created, exited, kernel, user;
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            const uint64_t cpu = (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                                 (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
            Seen seen;
            seen.cpu = cpu;
            const auto before = g_seen.find(entry.th32ThreadID);
            seen.label = before != g_seen.end() ? before->second.label : LabelOf(thread);
            if (before != g_seen.end() && cpu >= before->second.cpu) {
                const double percent = double(cpu - before->second.cpu) * 1e-4 / windowMs * 100.0;
                total += percent;
                ++threads;
                if (percent >= 90.0) ++flatOut;
                const std::string module = seen.label.find('(') != std::string::npos
                    ? seen.label.substr(seen.label.rfind('(') + 1, seen.label.size() - seen.label.rfind('(') - 2)
                    : seen.label;
                perModule[module] += percent;
                busy.push_back({percent, entry.th32ThreadID, nullptr});
            }
            now.emplace(entry.th32ThreadID, std::move(seen));
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    g_seen.swap(now);
    if (busy.empty()) return;
    for (auto& b : busy) b.label = &g_seen[b.id].label;
    std::sort(busy.begin(), busy.end(), [](const Busy& a, const Busy& b) { return a.percent > b.percent; });
    std::vector<std::pair<std::string, double>> modules(perModule.begin(), perModule.end());
    std::sort(modules.begin(), modules.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    char line[3072];
    int used = snprintf(line, sizeof line, "[cpu] %u threads, %.0f%% of a core in all, %u at 90%% or more | by module:",
                        threads, total, flatOut);
    for (size_t i = 0; i < modules.size() && i < 8 && used > 0 && size_t(used) < sizeof line; ++i)
        used += snprintf(line + used, sizeof line - used, " %s %.0f%%", modules[i].first.c_str(), modules[i].second);
    if (used > 0 && size_t(used) < sizeof line) used += snprintf(line + used, sizeof line - used, " | busiest:");
    for (size_t i = 0; i < busy.size() && i < 14 && used > 0 && size_t(used) < sizeof line; ++i)
        used += snprintf(line + used, sizeof line - used, " %s#%lu %.0f%%", busy[i].label->c_str(), busy[i].id,
                         busy[i].percent);
    Log("%s\n", line);
}

}  // namespace cvr::threadcpu
