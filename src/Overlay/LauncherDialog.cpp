#include "Overlay/LauncherDialog.hpp"
#include <windows.h>
#include <commctrl.h>
#include <iterator>
#include <string>

extern "C" void SetWindowResolutionAndPersist(int width, int height);
extern "C" int GetCurrentWindowWidth();
extern "C" int GetCurrentWindowHeight();
extern "C" void SetRuntimeModeAndPersist(int mode);
extern "C" int GetXrRuntimeMode();
extern "C" void SetHmdTypeAndPersist(int hmdType);
extern "C" int GetCurrentHmdType();
extern "C" void SetLauncherDebugAndPersist(int on);
extern "C" int GetLauncherDebug();

#define ID_COMBO_RES     101
#define ID_BUTTON_START  102
#define ID_COMBO_RUNTIME 103
#define ID_COMBO_HMD     104
#define ID_CHECK_DEBUG   105

// --- Visual constants -------------------------------------------------------
static const COLORREF kColBg     = RGB(248, 249, 251); // window background
static const COLORREF kColHeader = RGB(0, 120, 215);   // Windows accent blue
static const COLORREF kColSub    = RGB(120, 124, 130);  // muted gray
static const COLORREF kColLabel  = RGB(45, 48, 54);     // near-black label
static const int kMargin  = 26;
static const int kClientW = 372;
static const int kClientH = 452;   // room for the DEBUG row under the resolution combo
static const int kFieldW  = kClientW - 2 * kMargin;

struct ResolutionPreset {
    int width;
    int height;
    const wchar_t* label;
};

struct HmdPreset {
    int mhdType;
    const wchar_t* hmd_type;
    const wchar_t* label;
    const ResolutionPreset* resolutions;
    int resolutionCount;
};

// --- Per-HMD resolution lists ---
// Risoluzioni NATIVE per ogni visore (per occhio)
// --- Per-HMD resolution lists ---
static const ResolutionPreset kQuest2Resolutions[] = {
    {1832, 1920, L"1832 x 1920 (Native)"},
    {1680, 1760, L"1680 x 1760"},
    {2048, 2138, L"2048 x 2138"},
    {2560, 2672, L"2560 x 2672"},
    {4096, 4280, L"4096 x 4280"},
    {5000, 5232, L"5000 x 5232"},
    {6000, 6272, L"6000 x 6272"},
};

static const ResolutionPreset kQuest3SResolutions[] = {
    {1920, 1880, L"1920 x 1880 (Native)"},
    {1680, 1646, L"1680 x 1646"},
    {2048, 2006, L"2048 x 2006"},
    {2560, 2507, L"2560 x 2507"},
    {4096, 4008, L"4096 x 4008"},
    {5000, 4896, L"5000 x 4896"},
    {6000, 5880, L"6000 x 5880"},
};

// QUEST 3 IS SHAPED BY ITS FOV, NOT BY ITS PANEL.
//
// The old ladder here was the panel aspect -- 2064x2208 is the physical per-eye panel, and every
// other entry copied its ~0.936. That is the wrong shape to render at, and it is measurable: the
// engine does not let us set the horizontal and vertical FOV independently. It takes one FOV value
// and derives the other axis from the render target's aspect (verified live: tanH = tanV * w/h, to
// five decimals). So the aspect of the render target IS the lever on the H/V ratio, and it has to
// equal the aspect of the frustum we submit.
//
// That frustum, for a Quest 3, comes out of the runtime's own numbers:
//     horizontal  de-canted by shift from -54/+40 to a symmetric +-47   -> tan 1.072369
//     vertical    symmetrised by the MAX half-tangent of U+50 / D-49    -> tan 1.191754
//     ideal AR = 1.072369 / 1.191754 = 0.8998
// R.E.A.L. VR arrives at the same number and prints it -- "ideal AR would be 0.8998" -- and picks
// 2864x3184 (0.8995) to match. At the old 0.936 the vertical always renders narrower than what is
// submitted, whatever FOV we write, so the compositor stretches the image and the world reads too
// large.
//
// Every entry below is within 0.2% of that ideal, both dimensions multiples of 8. Each one needs a
// matching vrcam_<W>x<H> / vrcam_feed_<W>x<H> asset pair to exist.
//
// Contrast with kPico4Resolutions, which is square and correct: the Pico 4 has parallel panels and
// a symmetric 104x104 FOV, so its ideal AR really is 1.0. The square presets were never a mistake
// -- they were simply Pico-shaped, and Quest 3 is not.
static const ResolutionPreset kQuest3Resolutions[] = {
    {2064, 2296, L"2064 x 2296 (Native width)"},
    {1832, 2032, L"1832 x 2032"},
    {2296, 2552, L"2296 x 2552"},
    {2560, 2848, L"2560 x 2848"},
    {2864, 3184, L"2864 x 3184"},
    {3072, 3416, L"3072 x 3416"},
    {4096, 4552, L"4096 x 4552"},
    {5000, 5560, L"5000 x 5560"},
    {6000, 6672, L"6000 x 6672"},
};

static const ResolutionPreset kPico4Resolutions[] = {
    {2160, 2160, L"2160 x 2160 (Native)"},
    {1920, 1920, L"1920 x 1920"},
    {2048, 2048, L"2048 x 2048"},
    {2560, 2560, L"2560 x 2560"},
    {3072, 3072, L"3072 x 3072"},
    {4096, 4096, L"4096 x 4096"},
    {5000, 5000, L"5000 x 5000"},
    {6000, 6000, L"6000 x 6000"},
};

static const ResolutionPreset kPico4UltraResolutions[] = {
    {2160, 2160, L"2160 x 2160 (Native)"},
    {1920, 1920, L"1920 x 1920"},
    {2048, 2048, L"2048 x 2048"},
    {2560, 2560, L"2560 x 2560"},
    {3072, 3072, L"3072 x 3072"},
    {4096, 4096, L"4096 x 4096"},
    {5000, 5000, L"5000 x 5000"},
    {6000, 6000, L"6000 x 6000"},
};

static const ResolutionPreset kCrystalOGResolutions[] = {
    {2464, 2448, L"2464 x 2448 (Native)"},
    {2160, 2145, L"2160 x 2145"},
    {2048, 2034, L"2048 x 2034"},
    {2560, 2542, L"2560 x 2542"},
    {3072, 3051, L"3072 x 3051"},
    {4096, 4072, L"4096 x 4072"},
    {5000, 4968, L"5000 x 4968"},
    {6000, 5960, L"6000 x 5960"},
};

static const ResolutionPreset kCrystalLightResolutions[] = {
    {2464, 2448, L"2464 x 2448 (Native)"},
    {2160, 2145, L"2160 x 2145"},
    {2048, 2034, L"2048 x 2034"},
    {2560, 2542, L"2560 x 2542"},
    {3072, 3051, L"3072 x 3051"},
    {4096, 4072, L"4096 x 4072"},
    {5000, 4968, L"5000 x 4968"},
    {6000, 5960, L"6000 x 5960"},
};

static const ResolutionPreset kCrystalSuperResolutions[] = {
    {2464, 2448, L"2464 x 2448 (Native)"},
    {2160, 2145, L"2160 x 2145"},
    {2048, 2034, L"2048 x 2034"},
    {2560, 2542, L"2560 x 2542"},
    {3072, 3051, L"3072 x 3051"},
    {4096, 4072, L"4096 x 4072"},
    {5000, 4968, L"5000 x 4968"},
    {6000, 5960, L"6000 x 5960"},
};

// Crystal Super Ultra Wide: risoluzioni quadrate confermate (Crystal aspect ~1.0)
static const ResolutionPreset kCrystalWFResolutions[] = {
    {2464, 2448, L"2464 x 2448 (Native)"},
    {2160, 2145, L"2160 x 2145"},
    {2048, 2034, L"2048 x 2034"},
    {2560, 2542, L"2560 x 2542"},
    {3072, 3051, L"3072 x 3051"},
    {4096, 4072, L"4096 x 4072"},
    {5000, 4968, L"5000 x 4968"},
    {6000, 5960, L"6000 x 5960"},
};

// Valve Index: aspect ratio nativo 0.9 (1440x1600)
static const ResolutionPreset kValveIndexResolutions[] = {
    {1440, 1600, L"1440 x 1600 (Native)"},
    {1260, 1400, L"1260 x 1400"},
    {1080, 1200, L"1080 x 1200"},
    {1800, 2000, L"1800 x 2000"},
    {4096, 4552, L"4096 x 4552"},
    {5000, 5552, L"5000 x 5552"},
    {6000, 6664, L"6000 x 6664"},
};

// PIMAX DREAM AIR, shaped by its FOV like the Quest 3 ladder above and unlike the Crystal ones
// below -- which are near-square and wrong for every Pimax on a lighthouse setup.
//
// A tester's SteamVR reported lens 99 x 88 deg for this headset, so:
//     ideal AR = tan(49.5) / tan(44) = 1.170850 / 0.965689 = 1.21245
// and SteamVR's own recommendation, 2504x2068, comes to 1.2108 -- the runtime already knows the
// shape. A square pick misses it by 17.5%, which is a vertically stretched world however the FOV
// is written, because the render target's aspect is the only lever on the H/V ratio.
//
// Starts at the runtime's own recommendation rather than below it: this headset's native panel is
// well above that, so anything smaller is giving away sharpness for frames the RTT view has to
// render anyway. Both dimensions are multiples of 8 and every entry is within 0.2% of ideal.
static const ResolutionPreset kPimaxDreamAirResolutions[] = {
    {2504, 2064, L"2504 x 2064 (Runtime recommended)"},
    {2760, 2280, L"2760 x 2280"},
    {3072, 2536, L"3072 x 2536"},
    {4096, 3384, L"4096 x 3384"},
    {5000, 4128, L"5000 x 4128"},
    {6000, 4952, L"6000 x 4952"},
};

// PLAYSTATION VR2 (PC adapter, SteamVR/OpenXR). Ported from satyaloka93's psvr2-tweaks branch, with
// his reasoning kept because the reasoning IS the measurement:
//
//   SteamVR reports 3400x3468 per eye at 100 percent on the test PSVR2 (AR 0.98039), while the physical
//   panel is 2000x2040 -- the same AR. The shipped archive predates PSVR2 and carries only SQUARE VRCAM
//   components near that shape. Square is the closest authored shape, 2.0 percent wider than ideal, and
//   dramatically better than borrowing the Pimax ladder, where 3072x2536 is 23.6 percent wider than
//   PSVR2's runtime shape. Do NOT list 3400x3468 here until a matching vrcam_3400x3468 component and
//   dynamic texture have actually been imported into the archive.
static const ResolutionPreset kPlayStationVr2Resolutions[] = {
    {1920, 1920, L"1920 x 1920 (Performance)"},
    {2048, 2048, L"2048 x 2048 (Near panel resolution)"},
    {2560, 2560, L"2560 x 2560 (Balanced)"},
    {3072, 3072, L"3072 x 3072 (High)"},
    {3584, 3584, L"3584 x 3584 (Near runtime scale)"},
    {4096, 4096, L"4096 x 4096 (Ultra)"},
    {5000, 5000, L"5000 x 5000"},
    {6000, 6000, L"6000 x 6000"},
};

// BIGSCREEN BEYOND 2 / 2e. Identical panels and optics on both models -- 2560x2560 per eye, 75/90 Hz,
// 116 deg diagonal / 108 H / 96 V -- so one entry covers them.
//
// THE LADDER IS NOT SQUARE EVEN THOUGH THE PANEL IS, which is the same lesson Quest 3 taught: the render
// target must match the FRUSTUM, not the panel. From the published horizontal and vertical:
//
//   AR = tan(108/2) / tan(96/2) = 1.37638 / 1.11061 = 1.2393
//
// The vendor's diagonal does not close with those two -- sqrt(tanH^2 + tanV^2) gives 121 deg against
// the stated 116 -- which is ordinary for marketing figures and is why H and V are used rather than D.
//
// DERIVED FROM THE SPEC, NOT MEASURED: nobody here has the headset. The runtime's own per-eye tangents
// are logged at startup, and if they disagree this ladder should be rebuilt from them -- those are the
// numbers the submit path actually uses.
static const ResolutionPreset kBigscreenBeyond2Resolutions[] = {
    {1920, 1552, L"1920 x 1552"},
    {2048, 1656, L"2048 x 1656"},
    {2560, 2064, L"2560 x 2064 (Native panel width)"},
    {3072, 2480, L"3072 x 2480"},
    {3584, 2888, L"3584 x 2888"},
    {4096, 3304, L"4096 x 3304"},
    {5000, 4032, L"5000 x 4032"},
    {6000, 4840, L"6000 x 4840"},
};

static const HmdPreset kHmdPresets[] = {
    {0, L"Q2",    L"Meta Quest2",     kQuest2Resolutions,       _countof(kQuest2Resolutions)},
    {1, L"Q3S",   L"Meta Quest3s",    kQuest3SResolutions,      _countof(kQuest3SResolutions)},
    {2, L"Q3",    L"Meta Quest3",     kQuest3Resolutions,       _countof(kQuest3Resolutions)},
    {3, L"P4",    L"Pico4",           kPico4Resolutions,        _countof(kPico4Resolutions)},
    {4, L"P4U",   L"Pico4 Ultra",     kPico4UltraResolutions,   _countof(kPico4UltraResolutions)},
    {5, L"CRYOG", L"Crystal OG",      kCrystalOGResolutions,    _countof(kCrystalOGResolutions)},
    {6, L"GRYLI", L"Crystal Light",   kCrystalLightResolutions, _countof(kCrystalLightResolutions)},
    {7, L"CRYSU", L"Crystal Super",   kCrystalSuperResolutions, _countof(kCrystalSuperResolutions)},
    {8, L"CRYWF", L"Crystal Utra Wide", kCrystalWFResolutions,    _countof(kCrystalWFResolutions)},
    {9, L"VINDEX",L"Valve Index",     kValveIndexResolutions,   _countof(kValveIndexResolutions)},
    {10,L"PDA",   L"Pimax Dream Air", kPimaxDreamAirResolutions, _countof(kPimaxDreamAirResolutions)},
    {11,L"PSVR2", L"PlayStation VR2", kPlayStationVr2Resolutions, _countof(kPlayStationVr2Resolutions)},
    {12,L"BSB2",  L"Bigscreen Beyond 2/2e", kBigscreenBeyond2Resolutions, _countof(kBigscreenBeyond2Resolutions)},
};

struct RuntimeOption {
    int mode; // matches xr_runtime: 0 = OpenXR default, 1 = SteamVR
    const wchar_t* label;
};

static const RuntimeOption kRuntimeOptions[] = {
    {1, L"OpenVR  (SteamVR)"},
    {0, L"OpenXR  (VDXR, PimaxOpenXR)"},
};

static HFONT g_fontHeader = nullptr;
static HFONT g_fontSub    = nullptr;
static HFONT g_fontBody   = nullptr;
static HBRUSH g_brushBg   = nullptr;
static HWND g_hHeader  = nullptr;
static HWND g_hSub     = nullptr;
static HWND g_hRuntime = nullptr;
static HWND g_hHmd     = nullptr;   // <-- NUOVO
static HWND g_hRes     = nullptr;
static HWND g_hDebug   = nullptr;

static HFONT MakeFont(int pointSize, int weight) {
    HDC hdc = GetDC(nullptr);
    const int height = -MulDiv(pointSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

static HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, HFONT font) {
    HWND label = CreateWindowW(L"STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, w, h, parent, nullptr, nullptr, nullptr);
    SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
    return label;
}

static HWND MakeCombo(HWND parent, int id, int x, int y, int w) {
    HWND combo = CreateWindowW(WC_COMBOBOXW, L"",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL,
        x, y, w, 260, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(combo, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
    return combo;
}

// --- NUOVA FUNZIONE: popola la combo risoluzioni in base all'HMD selezionato ---
static void PopulateResolutionCombo(int hmdIndex) {
    if (!g_hRes) return;
    SendMessageW(g_hRes, CB_RESETCONTENT, 0, 0);

    if (hmdIndex < 0 || hmdIndex >= static_cast<int>(std::size(kHmdPresets))) {
        hmdIndex = 0;
    }

    const HmdPreset& hmd = kHmdPresets[hmdIndex];
    const int currentWidth = GetCurrentWindowWidth();
    const int currentHeight = GetCurrentWindowHeight();
    int resSel = 0;
    bool found = false;

    for (int i = 0; i < hmd.resolutionCount; ++i) {
        SendMessageW(g_hRes, CB_ADDSTRING, 0, (LPARAM)hmd.resolutions[i].label);
        if (!found &&
            hmd.resolutions[i].width == currentWidth &&
            hmd.resolutions[i].height == currentHeight) {
            resSel = i;
            found = true;
        }
    }

    // Se la risoluzione corrente non è nella lista del nuovo HMD,
    // seleziona la prima (quella nativa).
    if (!found && hmd.resolutionCount > 0) {
        resSel = 0;
    }

    SendMessageW(g_hRes, CB_SETCURSEL, resSel, 0);
}

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hwndButton;
    switch (msg) {
    case WM_CREATE: {
        g_fontHeader = MakeFont(20, FW_SEMIBOLD);
        g_fontSub    = MakeFont(10, FW_NORMAL);
        g_fontBody   = MakeFont(11, FW_NORMAL);
        int y = kMargin;

        g_hHeader = MakeLabel(hwnd, L"CyberpunkVRPort", kMargin, y, kFieldW, 44, g_fontHeader);
        y += 46;
        g_hSub = MakeLabel(hwnd, L"VR Configuration", kMargin, y, kFieldW, 22, g_fontSub);
        y += 38;

        // --- VR Headset (NUOVO) ---
        MakeLabel(hwnd, L"VR Headset", kMargin, y, kFieldW, 24, g_fontBody);
        y += 26;
        g_hHmd = MakeCombo(hwnd, ID_COMBO_HMD, kMargin, y, kFieldW);
        const int currentHmd = GetCurrentHmdType();
        int hmdSel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kHmdPresets)); ++i) {
            SendMessageW(g_hHmd, CB_ADDSTRING, 0, (LPARAM)kHmdPresets[i].label);
            if (kHmdPresets[i].mhdType == currentHmd) {
                hmdSel = i;
            }
        }
        SendMessageW(g_hHmd, CB_SETCURSEL, hmdSel, 0);
        y += 50;

        // --- VR Runtime ---
        MakeLabel(hwnd, L"VR Runtime", kMargin, y, kFieldW, 24, g_fontBody);
        y += 26;
        g_hRuntime = MakeCombo(hwnd, ID_COMBO_RUNTIME, kMargin, y, kFieldW);
        const int currentRuntime = GetXrRuntimeMode();
        int runtimeSel = 0;
        for (int i = 0; i < static_cast<int>(std::size(kRuntimeOptions)); ++i) {
            SendMessageW(g_hRuntime, CB_ADDSTRING, 0, (LPARAM)kRuntimeOptions[i].label);
            if (kRuntimeOptions[i].mode == currentRuntime) {
                runtimeSel = i;
            }
        }
        SendMessageW(g_hRuntime, CB_SETCURSEL, runtimeSel, 0);
        y += 50;

        // -- Render Resolution ---
        MakeLabel(hwnd, L"Render Resolution (per eye)", kMargin, y, kFieldW, 24, g_fontBody);
        y += 26;
        g_hRes = MakeCombo(hwnd, ID_COMBO_RES, kMargin, y, kFieldW);
        PopulateResolutionCombo(hmdSel);   // <-- Popola in base all'HMD selezionato
        y += 56;

        // --- DEBUG ---
        // Master switch for every probe, census and dump in the mod (see debug_gate.cpp).
        // Off is the normal way to play: with it ticked the log grows by tens of MB and the
        // frame time goes with it, because probes like SightAxisProbe take a mutex thousands
        // of times a frame.
        g_hDebug = CreateWindowW(L"BUTTON", L"DEBUG  (probes + verbose log, slow)",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            kMargin, y, kFieldW, 24,
            hwnd, (HMENU)ID_CHECK_DEBUG, nullptr, nullptr);
        SendMessageW(g_hDebug, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        SendMessageW(g_hDebug, BM_SETCHECK,
                     GetLauncherDebug() ? BST_CHECKED : BST_UNCHECKED, 0);
        y += 40;

        // --- Start button ---
        hwndButton = CreateWindowW(L"BUTTON", L"Start Game",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            kMargin, y, kFieldW, 38,
            hwnd, (HMENU)ID_BUTTON_START, nullptr, nullptr);
        SendMessageW(hwndButton, WM_SETFONT, (WPARAM)g_fontBody, TRUE);
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        if (ctrl == g_hHeader) {
            SetTextColor(hdc, kColHeader);
        } else if (ctrl == g_hSub) {
            SetTextColor(hdc, kColSub);
        } else {
            SetTextColor(hdc, kColLabel);
        }
        return (LRESULT)g_brushBg;
    }
    case WM_COMMAND: {
        const int notif = HIWORD(wParam);
        const int id    = LOWORD(wParam);

        // --- Cambio selezione HMD: aggiorna la lista risoluzioni ---
        if (id == ID_COMBO_HMD && notif == CBN_SELCHANGE) {
            const int hmdIdx = (int)SendMessageW(g_hHmd, CB_GETCURSEL, 0, 0);
            PopulateResolutionCombo(hmdIdx);
            break;
        }

        if (id == ID_BUTTON_START) {
            SetLauncherDebugAndPersist(
                SendMessageW(g_hDebug, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
            // Salva la selezione HMD
            int hmdIdx = (int)SendMessageW(g_hHmd, CB_GETCURSEL, 0, 0);
            if (hmdIdx >= 0 && hmdIdx < static_cast<int>(std::size(kHmdPresets))) {
                SetHmdTypeAndPersist(kHmdPresets[hmdIdx].mhdType);
            }

            int rIdx = (int)SendMessageW(g_hRuntime, CB_GETCURSEL, 0, 0);
            if (rIdx >= 0 && rIdx < static_cast<int>(std::size(kRuntimeOptions))) {
                SetRuntimeModeAndPersist(kRuntimeOptions[rIdx].mode);
            }

            int idx = (int)SendMessageW(g_hRes, CB_GETCURSEL, 0, 0);
            if (idx >= 0) {
                // Recupera l'HMD corrente per leggere la sua lista risoluzioni
                int curHmdIdx = (int)SendMessageW(g_hHmd, CB_GETCURSEL, 0, 0);
                if (curHmdIdx >= 0 && curHmdIdx < static_cast<int>(std::size(kHmdPresets))) {
                    const HmdPreset& hmd = kHmdPresets[curHmdIdx];
                    if (idx < hmd.resolutionCount) {
                        SetWindowResolutionAndPersist(
                            hmd.resolutions[idx].width,
                            hmd.resolutions[idx].height);
                    }
                }
            }
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_DESTROY:
        if (g_fontHeader) { DeleteObject(g_fontHeader); g_fontHeader = nullptr; }
        if (g_fontSub)    { DeleteObject(g_fontSub);    g_fontSub = nullptr; }
        if (g_fontBody)   { DeleteObject(g_fontBody);   g_fontBody = nullptr; }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowLauncherDialog() {
    g_brushBg = CreateSolidBrush(kColBg);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = LauncherWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CyberpunkVRPortLauncherClass";
    wc.hbrBackground = g_brushBg;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, kClientW, kClientH};
    AdjustWindowRect(&rc, style, FALSE);
    const int winW = rc.right - rc.left;
    const int winH = rc.bottom - rc.top;
    const int xPos = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    const int yPos = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        L"CyberpunkVRPortLauncherClass",
        L"CyberpunkVRPort Configuration",
        style,
        xPos, yPos, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (hwnd == nullptr) {
        if (g_brushBg) { DeleteObject(g_brushBg); g_brushBg = nullptr; }
        return;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (IsDialogMessage(hwnd, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_brushBg) { DeleteObject(g_brushBg); g_brushBg = nullptr; }
}