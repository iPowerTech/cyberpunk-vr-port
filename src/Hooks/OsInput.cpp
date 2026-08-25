// OsInput -- the Win32 calls the game makes about the cursor and the window, answered for VR.
//
// In a headset there is no desktop cursor to speak of, but the game asks about one constantly, and its
// answers decide where it thinks the mouse is, whether it should clip the pointer to the client area,
// and how large the client area is. Left alone, those answers are about a window that is not what the
// player is looking at.
//
// HOOKED BY IAT, not by detour. These are imports in the game's own module, so rewriting the import
// entry is both cheaper and reversible, and it leaves the system DLLs untouched -- which matters
// because an overlay or a capture tool may be patching the same functions the other way.
//
// A function this file cannot find in the import table is reported and skipped, never faked. The log
// lines about MoveWindow and GetMessagePos are that path, and they are harmless: the game does not call
// what it did not import.

#include <MinHook.h>
#include "Hooks/SwapChainInternal.hpp"
#include <thread>
#include "Hooks/SwapChain.hpp"
#include "Overlay/ImGuiOverlay.hpp"
#include "Hooks/Ngx.hpp"
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <wrl.h>

extern void Log(const char* fmt, ...);
extern volatile int g_verboseLog; // gate per-frame spam (ClipCursor / depth-diag)
extern "C" void PrepareStartupLiveControls();
extern "C" int CyberpunkVR_IsMainViewRecording();
extern "C" int CyberpunkVR_ViewKeyHookActive();
extern "C" void CyberpunkVR_ProfPublish();
extern "C" UINT GetForcedSwapchainWidth();
extern "C" UINT GetForcedSwapchainHeight();
extern "C" UINT GetForcedDisplayModeWidth();
extern "C" UINT GetForcedDisplayModeHeight();
extern "C" UINT GetForcedWindowWidth();
extern "C" UINT GetForcedWindowHeight();
extern "C" int GetMenuMode();

static bool IsGameWindowForeground(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    if (foreground == hwnd || GetAncestor(foreground, GA_ROOT) == hwnd) {
        return true;
    }

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);
    return foregroundPid == GetCurrentProcessId();
}

static bool HookIAT(HMODULE hMod, const char* dllName, const char* funcName, void* newFunc, void** oldFunc) {
    if (!hMod) {
        Log("HookIAT: Invalid module handle\n");
        return false;
    }

    PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        Log("HookIAT: Invalid DOS signature for module %p\n", hMod);
        return false;
    }

    PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hMod) + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        Log("HookIAT: Invalid NT signature for module %p\n", hMod);
        return false;
    }

    DWORD importDirRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!importDirRVA) {
        Log("HookIAT: No import directory for module %p\n", hMod);
        return false;
    }

    PIMAGE_IMPORT_DESCRIPTOR importDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(hMod) + importDirRVA);
    bool dllFound = false;

    while (importDesc->Name) {
        const char* name = reinterpret_cast<const char*>(reinterpret_cast<BYTE*>(hMod) + importDesc->Name);
        if (_stricmp(name, dllName) == 0) {
            dllFound = true;
            PIMAGE_THUNK_DATA thunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hMod) + importDesc->FirstThunk);
            PIMAGE_THUNK_DATA origThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hMod) + importDesc->OriginalFirstThunk);
            
            while (origThunk->u1.AddressOfData) {
                if (!(origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                    PIMAGE_IMPORT_BY_NAME importByName = reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(hMod) + origThunk->u1.AddressOfData);
                    if (strcmp(reinterpret_cast<const char*>(importByName->Name), funcName) == 0) {
                        // Second line of defence against the self-call above: if the slot
                        // already holds our detour, capturing it as "the original" would make
                        // the detour call itself for ever. Leave both alone.
                        if (thunk->u1.Function == reinterpret_cast<uintptr_t>(newFunc)) {
                            Log("HookIAT: '%s!%s' in module %p already hooked -- left as is\n",
                                dllName, funcName, hMod);
                            return true;
                        }
                        DWORD oldProtect;
                        VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t), PAGE_EXECUTE_READWRITE, &oldProtect);
                        if (oldFunc) {
                            *oldFunc = reinterpret_cast<void*>(thunk->u1.Function);
                        }
                        thunk->u1.Function = reinterpret_cast<uintptr_t>(newFunc);
                        VirtualProtect(&thunk->u1.Function, sizeof(uintptr_t), oldProtect, &oldProtect);
                        Log("HookIAT: Successfully hooked '%s!%s' in module %p (original=%p, hooked=%p)\n", dllName, funcName, hMod, oldFunc ? *oldFunc : nullptr, newFunc);
                        return true;
                    }
                }
                thunk++;
                origThunk++;
            }
        }
        importDesc++;
    }

    Log("HookIAT: Failed to find function '%s' in DLL '%s' for module %p%s\n", funcName, dllName, hMod, dllFound ? "" : " (DLL import descriptor not found)");
    return false;
}

using GetCursorPosFn = BOOL(WINAPI*)(LPPOINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindowFn = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using GetClientRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using GetWindowRectFn = BOOL(WINAPI*)(HWND, LPRECT);
using GetCursorInfoFn = BOOL(WINAPI*)(PCURSORINFO);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using GetSystemMetricsFn = int(WINAPI*)(int);
using GetMessagePosFn = DWORD(WINAPI*)(VOID);

static GetCursorPosFn g_origGetCursorPos = nullptr;
static SetCursorPosFn g_origSetCursorPos = nullptr;
static SetWindowPosFn g_origSetWindowPos = nullptr;
static MoveWindowFn g_origMoveWindow = nullptr;
static GetClientRectFn g_origGetClientRect = nullptr;
static GetWindowRectFn g_origGetWindowRect = nullptr;
static GetCursorInfoFn g_origGetCursorInfo = nullptr;
static ClipCursorFn g_origClipCursor = nullptr;
static GetSystemMetricsFn g_origGetSystemMetrics = nullptr;
static GetMessagePosFn g_origGetMessagePos = nullptr;
HWND g_gameHwnd = nullptr;

static BOOL WINAPI HookedGetCursorPos(LPPOINT lpPoint) {
    BOOL res = g_origGetCursorPos ? g_origGetCursorPos(lpPoint) : FALSE;
    static int callCount = 0;
    if (g_verboseLog && callCount++ % 1000 == 0) {
        Log("HookedGetCursorPos: called %d times. lpPoint=%p, pos=(%ld,%ld) g_gameHwnd=%p\n",
            callCount, lpPoint, lpPoint ? lpPoint->x : 0, lpPoint ? lpPoint->y : 0, g_gameHwnd);
    }
    if (res && lpPoint && g_gameHwnd) {
        UINT virtualWidth = GetForcedDisplayModeWidth();
        UINT virtualHeight = GetForcedDisplayModeHeight();
        
        if (virtualWidth > 0 && virtualHeight > 0) {
            RECT rect;
            BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(g_gameHwnd, &rect) : GetClientRect(g_gameHwnd, &rect);
            if (getRectRes) {
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;
                
                if (winWidth > 0 && winHeight > 0 && (static_cast<UINT>(winWidth) != virtualWidth || static_cast<UINT>(winHeight) != virtualHeight)) {
                    POINT clientPt = *lpPoint;
                    if (ScreenToClient(g_gameHwnd, &clientPt)) {
                        clientPt.x = (clientPt.x * virtualWidth) / winWidth;
                        clientPt.y = (clientPt.y * virtualHeight) / winHeight;
                        ClientToScreen(g_gameHwnd, &clientPt);
                        *lpPoint = clientPt;
                    }
                }
            }
        }
    }
    return res;
}

static BOOL WINAPI HookedSetCursorPos(int X, int Y) {
    static int callCount = 0;
    if (g_verboseLog && callCount++ % 100 == 0) {
        Log("HookedSetCursorPos: called %d times, target=(%d,%d) g_gameHwnd=%p\n", callCount, X, Y, g_gameHwnd);
    }
    if (g_gameHwnd) {
        UINT virtualWidth = GetForcedDisplayModeWidth();
        UINT virtualHeight = GetForcedDisplayModeHeight();
        
        if (virtualWidth > 0 && virtualHeight > 0) {
            RECT rect;
            BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(g_gameHwnd, &rect) : GetClientRect(g_gameHwnd, &rect);
            if (getRectRes) {
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;
                
                if (winWidth > 0 && winHeight > 0 && (static_cast<UINT>(winWidth) != virtualWidth || static_cast<UINT>(winHeight) != virtualHeight)) {
                    POINT clientPt = { X, Y };
                    if (ScreenToClient(g_gameHwnd, &clientPt)) {
                        clientPt.x = (clientPt.x * winWidth) / virtualWidth;
                        clientPt.y = (clientPt.y * winHeight) / virtualHeight;
                        ClientToScreen(g_gameHwnd, &clientPt);
                        X = clientPt.x;
                        Y = clientPt.y;
                    }
                }
            }
        }
    }
    return g_origSetCursorPos ? g_origSetCursorPos(X, Y) : FALSE;
}

static BOOL WINAPI HookedGetCursorInfo(PCURSORINFO pci) {
    BOOL res = g_origGetCursorInfo ? g_origGetCursorInfo(pci) : FALSE;
    static int callCount = 0;
    if (g_verboseLog && callCount++ % 1000 == 0) {
        Log("HookedGetCursorInfo: called %d times. ptScreenPos=(%ld,%ld)\n",
            callCount, (res && pci) ? pci->ptScreenPos.x : 0, (res && pci) ? pci->ptScreenPos.y : 0);
    }
    if (res && pci && g_gameHwnd) {
        UINT virtualWidth = GetForcedDisplayModeWidth();
        UINT virtualHeight = GetForcedDisplayModeHeight();
        
        if (virtualWidth > 0 && virtualHeight > 0) {
            RECT rect;
            BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(g_gameHwnd, &rect) : GetClientRect(g_gameHwnd, &rect);
            if (getRectRes) {
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;
                
                if (winWidth > 0 && winHeight > 0 && (static_cast<UINT>(winWidth) != virtualWidth || static_cast<UINT>(winHeight) != virtualHeight)) {
                    POINT clientPt = pci->ptScreenPos;
                    if (ScreenToClient(g_gameHwnd, &clientPt)) {
                        clientPt.x = (clientPt.x * virtualWidth) / winWidth;
                        clientPt.y = (clientPt.y * virtualHeight) / winHeight;
                        ClientToScreen(g_gameHwnd, &clientPt);
                        pci->ptScreenPos = clientPt;
                    }
                }
            }
        }
    }
    return res;
}

static BOOL WINAPI HookedClipCursor(const RECT* lpRect) {
    RECT scaledRect{};
    const RECT* rectToUse = lpRect;
    
    if (g_verboseLog) Log("HookedClipCursor called: rect=%p (%ld,%ld)-(%ld,%ld) g_gameHwnd=%p\n",
        lpRect, lpRect ? lpRect->left : 0, lpRect ? lpRect->top : 0, lpRect ? lpRect->right : 0, lpRect ? lpRect->bottom : 0, g_gameHwnd);

    if (lpRect && g_gameHwnd) {
        int w = lpRect->right - lpRect->left;
        int h = lpRect->bottom - lpRect->top;
        if (w <= 1 && h <= 1) {
            bool isMenuVisible = (GetMenuMode() != 0) || OverlayIsVisible();
            if (isMenuVisible) {
                if (g_verboseLog) Log("HookedClipCursor: Ignored centering clip (%ld,%ld) because menu mode is active\n", lpRect->left, lpRect->top);
                return g_origClipCursor ? g_origClipCursor(nullptr) : ClipCursor(nullptr);
            }
        }

        UINT virtualWidth = GetForcedDisplayModeWidth();
        UINT virtualHeight = GetForcedDisplayModeHeight();
        
        if (virtualWidth > 0 && virtualHeight > 0) {
            RECT rect;
            BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(g_gameHwnd, &rect) : GetClientRect(g_gameHwnd, &rect);
            if (getRectRes) {
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;
                
                if (winWidth > 0 && winHeight > 0 && (static_cast<UINT>(winWidth) != virtualWidth || static_cast<UINT>(winHeight) != virtualHeight)) {
                    int inputWidth = lpRect->right - lpRect->left;
                    int inputHeight = lpRect->bottom - lpRect->top;
                    
                    if (inputWidth > winWidth || inputHeight > winHeight || lpRect->right > winWidth || lpRect->bottom > winHeight) {
                        POINT winPos = { 0, 0 };
                        ClientToScreen(g_gameHwnd, &winPos);
                        
                        scaledRect.left = ((lpRect->left - winPos.x) * winWidth) / virtualWidth + winPos.x;
                        scaledRect.top = ((lpRect->top - winPos.y) * winHeight) / virtualHeight + winPos.y;
                        scaledRect.right = ((lpRect->right - winPos.x) * winWidth) / virtualWidth + winPos.x;
                        scaledRect.bottom = ((lpRect->bottom - winPos.y) * winHeight) / virtualHeight + winPos.y;
                        
                        rectToUse = &scaledRect;
                        if (g_verboseLog) Log("ClipCursor scaled: (%ld,%ld)-(%ld,%ld) -> (%ld,%ld)-(%ld,%ld)\n",
                            lpRect->left, lpRect->top, lpRect->right, lpRect->bottom,
                            scaledRect.left, scaledRect.top, scaledRect.right, scaledRect.bottom);
                    }
                }
            }
        }
    }
    
    return g_origClipCursor ? g_origClipCursor(rectToUse) : ClipCursor(rectToUse);
}

static BOOL WINAPI HookedGetClientRect(HWND hWnd, LPRECT lpRect) {
    BOOL res = g_origGetClientRect ? g_origGetClientRect(hWnd, lpRect) : FALSE;
    static int callCount = 0;
    if (g_verboseLog && callCount++ % 100 == 0) {
        Log("HookedGetClientRect: called %d times. hWnd=%p g_gameHwnd=%p\n", callCount, hWnd, g_gameHwnd);
    }
    if (res && lpRect) {
        if (hWnd && (hWnd == g_gameHwnd || (g_gameHwnd == nullptr && IsGameWindowForeground(hWnd)))) {
            UINT virtualWidth = GetForcedDisplayModeWidth();
            UINT virtualHeight = GetForcedDisplayModeHeight();
            if (virtualWidth > 0 && virtualHeight > 0) {
                lpRect->left = 0;
                lpRect->top = 0;
                lpRect->right = virtualWidth;
                lpRect->bottom = virtualHeight;
            }
        }
    }
    return res;
}

static BOOL WINAPI HookedGetWindowRect(HWND hWnd, LPRECT lpRect) {
    BOOL res = g_origGetWindowRect ? g_origGetWindowRect(hWnd, lpRect) : FALSE;
    static int callCount = 0;
    if (g_verboseLog && callCount++ % 100 == 0) {
        Log("HookedGetWindowRect: called %d times. hWnd=%p g_gameHwnd=%p\n", callCount, hWnd, g_gameHwnd);
    }
    if (res && lpRect) {
        if (hWnd && (hWnd == g_gameHwnd || (g_gameHwnd == nullptr && IsGameWindowForeground(hWnd)))) {
            UINT virtualWidth = GetForcedDisplayModeWidth();
            UINT virtualHeight = GetForcedDisplayModeHeight();
            if (virtualWidth > 0 && virtualHeight > 0) {
                lpRect->right = lpRect->left + virtualWidth;
                lpRect->bottom = lpRect->top + virtualHeight;
            }
        }
    }
    return res;
}

static BOOL WINAPI HookedSetWindowPos(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags) {
    if (!(uFlags & SWP_NOSIZE)) {
        HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        if (GetMonitorInfoA(monitor, &mi)) {
            int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
            int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
            
            if (cx > monWidth) {
                cx = monWidth;
                if (!(uFlags & SWP_NOMOVE)) X = mi.rcMonitor.left;
            }
            if (cy > monHeight) {
                cy = monHeight;
                if (!(uFlags & SWP_NOMOVE)) Y = mi.rcMonitor.top;
            }
        }
    }
    return g_origSetWindowPos ? g_origSetWindowPos(hWnd, hWndInsertAfter, X, Y, cx, cy, uFlags) : FALSE;
}

static BOOL WINAPI HookedMoveWindow(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint) {
    HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (GetMonitorInfoA(monitor, &mi)) {
        int monWidth = mi.rcMonitor.right - mi.rcMonitor.left;
        int monHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
        
        if (nWidth > monWidth) {
            nWidth = monWidth;
            X = mi.rcMonitor.left;
        }
        if (nHeight > monHeight) {
            nHeight = monHeight;
            Y = mi.rcMonitor.top;
        }
    }
    return g_origMoveWindow ? g_origMoveWindow(hWnd, X, Y, nWidth, nHeight, bRepaint) : FALSE;
}

static int WINAPI HookedGetSystemMetrics(int nIndex) {
    int res = g_origGetSystemMetrics ? g_origGetSystemMetrics(nIndex) : 0;
    
    UINT virtualWidth = GetForcedDisplayModeWidth();
    UINT virtualHeight = GetForcedDisplayModeHeight();
    
    if (virtualWidth > 0 && virtualHeight > 0) {
        // SM_CXSCREEN = 0, SM_CYSCREEN = 1, SM_CXFULLSCREEN = 16, SM_CYFULLSCREEN = 17, SM_CXVIRTUALSCREEN = 78, SM_CYVIRTUALSCREEN = 79
        if (nIndex == SM_CXSCREEN || nIndex == SM_CXFULLSCREEN || nIndex == SM_CXVIRTUALSCREEN) {
            if (g_verboseLog) Log("GetSystemMetrics(%d) override: %d -> %d\n", nIndex, res, virtualWidth);
            return virtualWidth;
        }
        if (nIndex == SM_CYSCREEN || nIndex == SM_CYFULLSCREEN || nIndex == SM_CYVIRTUALSCREEN) {
            if (g_verboseLog) Log("GetSystemMetrics(%d) override: %d -> %d\n", nIndex, res, virtualHeight);
            return virtualHeight;
        }
    }
    return res;
}

static DWORD WINAPI HookedGetMessagePos(VOID) {
    DWORD res = g_origGetMessagePos ? g_origGetMessagePos() : 0;
    if (g_gameHwnd) {
        UINT virtualWidth = GetForcedDisplayModeWidth();
        UINT virtualHeight = GetForcedDisplayModeHeight();
        
        if (virtualWidth > 0 && virtualHeight > 0) {
            RECT rect;
            BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(g_gameHwnd, &rect) : GetClientRect(g_gameHwnd, &rect);
            if (getRectRes) {
                int winWidth = rect.right - rect.left;
                int winHeight = rect.bottom - rect.top;
                
                if (winWidth > 0 && winHeight > 0 && (static_cast<UINT>(winWidth) != virtualWidth || static_cast<UINT>(winHeight) != virtualHeight)) {
                    int x = (short)LOWORD(res);
                    int y = (short)HIWORD(res);
                    
                    POINT clientPt = { x, y };
                    if (ScreenToClient(g_gameHwnd, &clientPt)) {
                        clientPt.x = (clientPt.x * virtualWidth) / winWidth;
                        clientPt.y = (clientPt.y * virtualHeight) / winHeight;
                        ClientToScreen(g_gameHwnd, &clientPt);
                        
                        DWORD newRes = MAKELONG(static_cast<WORD>(clientPt.x), static_cast<WORD>(clientPt.y));
                        static int logCount = 0;
    if (g_verboseLog && logCount++ % 100 == 0) {
        Log("GetMessagePos override: (%d,%d) -> (%ld,%ld) win=%dx%d virt=%ux%u\n",
                                x, y, clientPt.x, clientPt.y, winWidth, winHeight, virtualWidth, virtualHeight);
                        }
                        return newRes;
                    }
                }
            }
        }
    }
    return res;
}

// Runs from every swapchain creation, so it must be idempotent -- and it was not.
//
// A second pass re-walks the same IATs, finds our own detour already in the slot, and stores
// THAT as "the original". The detour then calls itself, for ever: enabling the VRCAM mirror
// (which creates the process's second swapchain) died on the spot with
// EXCEPTION_STACK_OVERFLOW. Nothing here needs repeating -- the imports are patched once and
// stay patched -- so the whole function runs exactly once. HookIAT carries the same check
// again, because this is a failure mode that must not come back by another route.
void InstallOSHooks() {
    static std::atomic<bool> s_installed{false};
    bool expected = false;
    if (!s_installed.compare_exchange_strong(expected, true)) return;

    if (g_verboseLog) Log("InstallOSHooks: Installing IAT hooks for user32.dll functions...\n");
    
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        g_origGetCursorPos = reinterpret_cast<GetCursorPosFn>(GetProcAddress(hUser32, "GetCursorPos"));
        g_origSetCursorPos = reinterpret_cast<SetCursorPosFn>(GetProcAddress(hUser32, "SetCursorPos"));
        g_origSetWindowPos = reinterpret_cast<SetWindowPosFn>(GetProcAddress(hUser32, "SetWindowPos"));
        g_origMoveWindow = reinterpret_cast<MoveWindowFn>(GetProcAddress(hUser32, "MoveWindow"));
        g_origGetClientRect = reinterpret_cast<GetClientRectFn>(GetProcAddress(hUser32, "GetClientRect"));
        g_origGetWindowRect = reinterpret_cast<GetWindowRectFn>(GetProcAddress(hUser32, "GetWindowRect"));
        g_origGetCursorInfo = reinterpret_cast<GetCursorInfoFn>(GetProcAddress(hUser32, "GetCursorInfo"));
        g_origClipCursor = reinterpret_cast<ClipCursorFn>(GetProcAddress(hUser32, "ClipCursor"));
        g_origGetSystemMetrics = reinterpret_cast<GetSystemMetricsFn>(GetProcAddress(hUser32, "GetSystemMetrics"));
        g_origGetMessagePos = reinterpret_cast<GetMessagePosFn>(GetProcAddress(hUser32, "GetMessagePos"));
        if (g_verboseLog) Log("InstallOSHooks: Dynamically resolved all user32 original functions.\n");
    } else {
        Log("InstallOSHooks: Warning: user32.dll not found in process!\n");
    }
    
    // BOTH modules, deliberately: the game's, and our own. Our overlay asks user32 for the
    // client rect and cursor too, and it needs the same VIRTUAL answers the game gets --
    // without the self-hook the panel's mouse mapping and the mirror window break.
    HMODULE hMainExe = GetModuleHandleA(nullptr);
    HMODULE hCurrentDll = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(InstallOSHooks), &hCurrentDll);

    HMODULE modules[] = { hMainExe, hCurrentDll };

    for (HMODULE hMod : modules) {
        if (!hMod) continue;
        if (g_verboseLog) Log("InstallOSHooks: Hooking module %p...\n", hMod);
        HookIAT(hMod, "user32.dll", "GetCursorPos", reinterpret_cast<void*>(HookedGetCursorPos), reinterpret_cast<void**>(&g_origGetCursorPos));
        HookIAT(hMod, "user32.dll", "SetCursorPos", reinterpret_cast<void*>(HookedSetCursorPos), reinterpret_cast<void**>(&g_origSetCursorPos));
        HookIAT(hMod, "user32.dll", "SetWindowPos", reinterpret_cast<void*>(HookedSetWindowPos), reinterpret_cast<void**>(&g_origSetWindowPos));
        HookIAT(hMod, "user32.dll", "MoveWindow", reinterpret_cast<void*>(HookedMoveWindow), reinterpret_cast<void**>(&g_origMoveWindow));
        HookIAT(hMod, "user32.dll", "GetClientRect", reinterpret_cast<void*>(HookedGetClientRect), reinterpret_cast<void**>(&g_origGetClientRect));
        HookIAT(hMod, "user32.dll", "GetWindowRect", reinterpret_cast<void*>(HookedGetWindowRect), reinterpret_cast<void**>(&g_origGetWindowRect));
        HookIAT(hMod, "user32.dll", "GetCursorInfo", reinterpret_cast<void*>(HookedGetCursorInfo), reinterpret_cast<void**>(&g_origGetCursorInfo));
        HookIAT(hMod, "user32.dll", "ClipCursor", reinterpret_cast<void*>(HookedClipCursor), reinterpret_cast<void**>(&g_origClipCursor));
        HookIAT(hMod, "user32.dll", "GetSystemMetrics", reinterpret_cast<void*>(HookedGetSystemMetrics), reinterpret_cast<void**>(&g_origGetSystemMetrics));
        HookIAT(hMod, "user32.dll", "GetMessagePos", reinterpret_cast<void*>(HookedGetMessagePos), reinterpret_cast<void**>(&g_origGetMessagePos));
    }
}

static bool GetClampedClientRect(HWND hwnd, RECT* outRect) {
    if (!hwnd || !outRect || !IsWindow(hwnd) || IsIconic(hwnd)) return false;

    RECT client{};
    BOOL getRectRes = g_origGetClientRect ? g_origGetClientRect(hwnd, &client) : GetClientRect(hwnd, &client);
    if (!getRectRes) return false;

    POINT topLeft{client.left, client.top};
    POINT bottomRight{client.right, client.bottom};
    if (!ClientToScreen(hwnd, &topLeft) || !ClientToScreen(hwnd, &bottomRight)) {
        return false;
    }

    RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};
    if (monitor && GetMonitorInfoA(monitor, &monitorInfo)) {
        RECT clipped{};
        if (IntersectRect(&clipped, &screenRect, &monitorInfo.rcMonitor)) {
            screenRect = clipped;
        } else {
            screenRect = monitorInfo.rcMonitor;
        }
    }

    if (screenRect.right <= screenRect.left || screenRect.bottom <= screenRect.top) {
        return false;
    }

    *outRect = screenRect;
    return true;
}

void UpdateCursorCapture(HWND hwnd) {
    if (OverlayIsVisible()) {
        if (g_cursorClipped) {
            ClipCursor(nullptr);
            g_cursorClipped = false;
        }
        return;
    }

    RECT clipRect{};
    if (IsGameWindowForeground(hwnd) && GetClampedClientRect(hwnd, &clipRect)) {
        ClipCursor(&clipRect);
        g_cursorClipped = true;
        return;
    }

    if (g_cursorClipped) {
        ClipCursor(nullptr);
        g_cursorClipped = false;
    }
}
