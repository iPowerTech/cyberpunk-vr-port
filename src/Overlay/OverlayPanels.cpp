// OverlayPanels -- every control the headset overlay draws, and the small widgets they are made of.
//
// The widgets exist because ImGui has no clamped-int slider that reports change the way this file needs:
// CheckboxInt, SliderIntClamped and InputIntClamped all return whether the value moved, so a panel can
// persist only on change instead of writing the ini every frame.
//
// THE PANELS EDIT A SNAPSHOT, NOT THE LIVE CONTROLS. DrawLiveControls takes a LiveControlsUiState by
// reference, and the caller hands it back to be persisted. The live values are volatile scalars read by
// hooks on other threads; editing them directly from the UI thread would mean a slider drag is a series
// of half-applied states, which is visible in the headset.

#include "Anim/WheelGrab.hpp"
#include "Utils/SharedSlots.hpp"
#include "Core/VrCoreShared.hpp"
#include "Overlay/ImGuiOverlay.hpp"
#include "Hooks/LookAtRayFromHand.hpp"   // the hand-pointing counters shown in the Controls tab
#include "Overlay/LiveControlsUi.hpp"
#include "Runtimes/OpenXRManager.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include "im3d.h"
#include "Overlay/OverlayInternal.hpp"
#include "Camera/CameraState.hpp"

extern volatile int g_verboseLog; // per-frame log spam toggle (default off)
extern void Log(const char* fmt, ...);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern volatile float g_lastLocateQuat[4];
extern "C" int   CyberpunkVR_StereoModuleEnable;   // vr_core.cpp: did we install at all
extern "C" float CyberpunkVR_SnapTurnStickFire;             // Hooks/XInput.cpp
extern "C" float CyberpunkVR_SnapTurnStickRearm;
extern "C" float CyberpunkVR_DebugRightStickX;
extern "C" float CyberpunkVR_DebugRightStickRaw;
extern "C" float CyberpunkVR_DebugRightStickPeakR;
extern "C" float CyberpunkVR_DebugRightStickPeakL;
extern "C" int   CyberpunkVR_DebugRightStickReset;
extern "C" int   CyberpunkVR_StereoModuleLoaded;
extern "C" int32_t CyberpunkVR_StereoLog;
extern "C" int      CyberpunkVR_StereoSubmit;              // openxr_frameloop.cpp
extern "C" int32_t  CyberpunkVR_StereoEyeCapture;
extern "C" uint32_t CyberpunkVR_StereoEyeMaxAgeMs;
extern "C" uint64_t CyberpunkVR_DebugSceneEyeQuatWrites;
extern "C" uint32_t CyberpunkVR_DebugVrcamEyeAgeMs;        // 0xFFFFFFFF = never produced
extern "C" unsigned long long CyberpunkVR_DebugStereoEyeSubmits;
extern "C" int32_t CyberpunkVR_StableCopy;
extern "C" int32_t CyberpunkVR_StableFromTonemap;
extern "C" uint64_t CyberpunkVR_DebugStableCopies;
extern "C" uint64_t CyberpunkVR_DebugStableSkips;
extern "C" int32_t CyberpunkVR_VrcamDlss;
extern "C" int32_t CyberpunkVR_ForceVrcamCam;
extern "C" uint32_t CyberpunkVR_VrcamEnabled;
extern "C" void        CyberpunkVR_SetVrcamEnabled(uint32_t on);
extern "C" const char* CyberpunkVR_VrcamComponentName();
extern "C" const char* CyberpunkVR_VrcamCameraName();
extern "C" uint32_t CyberpunkVR_MirrorOutput;
extern "C" uint64_t CyberpunkVR_DebugVrcamNodeHits;   // 0 => the second view never dispatched
extern "C" uint64_t CyberpunkVR_DebugMirrorRtvHits;
extern "C" int CyberpunkVR_IsVrcamViewActive();
extern "C" float CyberpunkVR_DebugMainProjYY;
extern "C" float CyberpunkVR_DebugMainCamFov;
extern "C" float CyberpunkVR_MainAdsZoomFactor;
extern "C" float CyberpunkVR_DebugVrcamWantFov;
extern "C" float CyberpunkVR_DebugVrcamBaseFov;
extern "C" int32_t  CyberpunkVR_ProfEnable;
extern "C" double   CyberpunkVR_ProfFrameMs;
extern "C" double   CyberpunkVR_ProfDispMainMs;
extern "C" double   CyberpunkVR_ProfDispVrcamMs;
extern "C" uint32_t CyberpunkVR_ProfDispMainNodes;
extern "C" uint32_t CyberpunkVR_ProfDispVrcamNodes;
extern "C" void     CyberpunkVR_ProfDumpNodes();
extern "C" int      CyberpunkVR_ProfSnapshotNodes(uint32_t* rva, double* msv, double* msm,
                                                  uint32_t* cv, uint32_t* cm, int maxn);
extern "C" const char* CyberpunkVR_ProfNodeName(uint32_t rva);
extern "C" uint64_t CyberpunkVR_DebugViewKeyMainNodes;
extern "C" uint64_t CyberpunkVR_DebugViewKeyOtherNodes;
extern volatile int32_t g_lastLocatePosFP[3];
extern "C" float CyberpunkVRPort_HalfIpd();
extern "C" float GetGameRenderFovDeg();
extern "C" int CyberpunkVR_MainIsRightEye;
extern "C" UINT GetForcedDisplayModeWidth();
extern "C" UINT GetForcedDisplayModeHeight();

// DLSS5-NR (neural render) -- Hooks/NgxEvalPassthrough.cpp.
extern "C" int32_t  CyberpunkVR_NgxPassArmed;
extern "C" uint64_t CyberpunkVR_NgxPassEvals;
extern "C" int32_t  CyberpunkVR_NrCreate;
extern "C" int32_t  CyberpunkVR_NrEval;
extern "C" int32_t  CyberpunkVR_NrWriteBack;
extern "C" int32_t  CyberpunkVR_NrAlternateEyes;
extern "C" int32_t  CyberpunkVR_NrReset;
extern "C" int32_t  CyberpunkVR_NrMVecScaleMode;
extern "C" int32_t  CyberpunkVR_NrInited;
extern "C" int32_t  CyberpunkVR_NrContexts;
extern "C" int32_t  CyberpunkVR_NrDisabled;
extern "C" uint32_t CyberpunkVR_NrCreateResult;
extern "C" uint32_t CyberpunkVR_NrLastResult;
extern "C" uint32_t CyberpunkVR_NrEvalsL;
extern "C" uint32_t CyberpunkVR_NrEvalsR;
extern "C" float    CyberpunkVR_NrPassesPerSec;
extern "C" int32_t  CyberpunkVR_NrPreset;
extern "C" int32_t  CyberpunkVR_NrStyle;
extern "C" float    CyberpunkVR_NrIntensity;
extern "C" float    CyberpunkVR_NrLocalTone;
extern "C" float    CyberpunkVR_NrLocalStructure;
extern "C" float    CyberpunkVR_NrSkinStructure;
extern "C" float    CyberpunkVR_NrPaperWhite;
extern "C" float    CyberpunkVR_NrHdrTransfer;
extern "C" float    CyberpunkVR_NrColorStrength;
extern "C" int32_t  CyberpunkVR_NrDepthConversion;
extern "C" float    CyberpunkVR_NrMotionScaleX;
extern "C" float    CyberpunkVR_NrMotionScaleY;

// FSR3 frame generator -- Hooks/NgxEvalPassthrough.cpp (the dispatch call), FrameGen/FrameGenFsr3.cpp
// (the FFX contexts). Still a smoke test: the interpolated frame is produced and discarded, not
// shown in the headset yet -- the panel says so rather than implying the checkbox changes anything
// you see.
extern "C" int32_t  CyberpunkVR_FrameGenEnable;
extern "C" int32_t  CyberpunkVR_FrameGenReady;
extern "C" int32_t  CyberpunkVR_FrameGenDisabled;
extern "C" uint32_t CyberpunkVR_FrameGenDispatches;
extern "C" uint32_t CyberpunkVR_FrameGenLastResult;

namespace overlay {

bool CheckboxInt(const char* label, int* value) {
    bool checked = *value != 0;
    const bool changed = ImGui::Checkbox(label, &checked);
    if (changed) {
        *value = checked ? 1 : 0;
    }
    return changed;
}

bool SliderIntClamped(const char* label, int* value, int minValue, int maxValue) {
    int temp = *value;
    const bool changed = ImGui::SliderInt(label, &temp, minValue, maxValue);
    if (changed) {
        *value = std::clamp(temp, minValue, maxValue);
    }
    return changed;
}

bool InputIntClamped(const char* label, int* value, int minValue, int maxValue) {
    int temp = *value;
    const bool changed = ImGui::InputInt(label, &temp, 1, 64);
    if (changed) {
        *value = std::clamp(temp, minValue, maxValue);
    }
    return changed;
}

bool DrawFovControl(LiveControlsUiState& state) {
    bool changed = false;
    bool useRuntime = state.xrForceFov <= 0.0f;
    if (ImGui::Checkbox("Use OpenXR runtime projection FOV", &useRuntime)) {
        state.xrForceFov = useRuntime ? 0.0f : 112.0f;
        changed = true;
    }

    if (useRuntime) {
        ImGui::BeginDisabled();
    }
    float fov = state.xrForceFov <= 0.0f ? 112.0f : state.xrForceFov;
    if (ImGui::SliderFloat("OpenXR projection layer FOV", &fov, 80.0f, 140.0f, "%.1f deg")) {
        state.xrForceFov = fov;
        changed = true;
    }
    if (useRuntime) {
        ImGui::EndDisabled();
    }
    ImGui::TextUnformatted("This changes the OpenXR projection layer FOV, not the CP2077 camera FOV.");
    return changed;
}

// In-headset VR floating-hands controls: tracking on/off, IK calibration, wrist
// alignment, and the diagnostic dump -- everything that used to live only in the
// desktop CET window. Values are published to the RED4ext arm-IK plugin through
// shared memory (OpenXRManager::SetVRHandCalib). Defaults mirror the plugin's
// baked calibration so the rig behaves identically before anything is touched.
void DrawVRHandsControls() {
    // Tracking toggle (writes shared-mem slot [32]; plugin installs hooks + arms
    // and sets g_VRBind = this value). Must be 4 = full-arm IK (the mode the CET
    // "Start VR Tracking" button uses). Mode 2 is the legacy direct bone-write
    // fallback -> stretched forearm / wrong placement, which is what this was.
    static bool s_vrHandTracking = true;   // default ON — backend's m_vrHandTrackingMode also defaults to 4
    if (ImGui::Checkbox("Start VR hand tracking", &s_vrHandTracking)) {
        OpenXRManager::Get().SetVRHandTrackingMode(s_vrHandTracking ? 4 : 0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Log VR Diag")) {
        OpenXRManager::Get().RequestVRDiag();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Dumps gizmo vs. bone poses to vrik_diag.txt (next to dxgi.dll)\n"
                          "for tuning the arm IK. Same as the CET 'Log VR Diag' button.");
    }

    ImGui::Separator();
    // Physical body rotation MOVED TO THE CONTROLS TAB. It is solved by VRIK, which is why it
    // used to live here, but a player hunting for it is hunting for a way to TURN -- and that
    // is what the Controls tab is. What stays is the tuning that only means something while it is
    // on: the free-look cone, and the live readout of how far the body is behind the head.
    {
        LiveControlsUiState st{};
        GetLiveControlsUiState(&st);
        const bool bodyRot = st.xrPhysicalBodyRotation != 0;

        // Cutscene VRIK suspend (PR #40). Picks the minimum scene tier at which the plugin fully
        // suspends the body+arm solve so the engine authored cinematic pose plays clean. Persisted
        // through the LiveControls bridge (vrport.ini xr_cutscene_suspend_tier) and republished to
        // shared[158] every tick, so a change here takes effect without a restart.
        //
        // Combo index -> stored min-tier: Never(-1), Tier2+(1), Tier3+(2), Tier4+(3), Tier5(4).
        static const int kTierValues[] = { -1, 1, 2, 3, 4 };
        static const char* kTierLabels[] = {
            "Never (VRIK always on)",
            "Staged scenes and up (Tier 2+)",
            "Tier 3 and up",
            "Cinematics (Tier 4+)  [default]",
            "Full cinematics only (Tier 5)",
        };
        int idx = 3;   // default Tier4+
        for (int i = 0; i < 5; ++i) {
            if (kTierValues[i] == st.xrCutsceneSuspendTier) { idx = i; break; }
        }
        ImGui::TextUnformatted("Suspend VRIK in cutscenes");
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::Combo("##cutsceneSuspend", &idx, kTierLabels, 5)) {
            st.xrCutsceneSuspendTier = kTierValues[idx];
            SetLiveControlsUiState(&st, 1);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "During scripted scenes the game plays a full authored body+arm animation.\n"
                "VRIK keeps solving on top of it, which looks wrong -- so above the chosen\n"
                "scene tier the avatar is left entirely to the engine's cutscene pose.\n"
                "'Cinematics (Tier 4+)' is the safe default; lower tiers also suspend during\n"
                "lighter staged/walk-and-talk moments. Vehicles use their own arms-only path.");
        }
        // The one number the feature has. Everything else -- the rate, when it starts, when it stops --
        // follows from it: whatever is outside the cone is asked for in the frame it appears.
        float cone = CyberpunkVR_BodyYawFollowDeadDeg;
        if (ImGui::SliderFloat("Free-look cone", &cone, 0.0f, 60.0f, "%.0f deg")) {
            CyberpunkVR_BodyYawFollowDeadDeg = cone;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How far your head may turn before the body starts coming around.\n"
                              "The body stops as soon as your head is back inside the cone.");
        }
        if (bodyRot) {
            ImGui::Text("realign %+.1f deg | head-vs-body %+.1f deg",
                        CyberpunkVR_DebugBodyFollowOffsetDeg, CyberpunkVR_DebugBodyFollowErrDeg);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Hand IK Calibration (per hand: R = right, L = left)");

    // Defaults mirror the plugin's baked calibration (main.cpp globals).
    static float scaleR = 1.05f, scaleL = 1.06f;   // reach scale (arm straightening)
    static float heightR = 0.0f, heightL = 0.0f; // vertical fine-tune offset (m)
    static float swingR = 1.0f,  swingL = 1.0f;    // elbow-swing gain
    static float poleR = 0.0f,   poleL = 0.0f;     // elbow pole spin (deg)
    static float wRp = 0.0f, wRy = -90.0f, wRr = 0.0f;     // right wrist euler (deg)
    static float wLp = -180.0f, wLy = -90.0f, wLr = 0.0f;  // left wrist euler (deg)

    // TWO-WAY SYNC (fixes "калибровка не сохраняется"). These statics used to be
    // one-way UI -> manager: they kept their hardcoded defaults after the manager
    // loaded vrik_calibration.ini / ran auto-calibration, so the FIRST slider touch
    // (or Apply) pushed 14 default values over the real calibration -- which the next
    // Save then wrote to disk. Pull the manager's live values into the sliders
    // whenever the user isn't actively dragging.
    if (!ImGui::IsAnyItemActive()) {
        float c[14]; OpenXRManager::Get().GetVRHandCalib(c);
        scaleR=c[0]; scaleL=c[1]; heightR=c[2]; heightL=c[3];
        swingR=c[4]; swingL=c[5]; poleR=c[6]; poleL=c[7];
        wRp=c[8]; wRy=c[9]; wRr=c[10]; wLp=c[11]; wLy=c[12]; wLr=c[13];
    }

    bool calChanged = false;
    calChanged |= ImGui::SliderFloat("Reach scale R", &scaleR, 0.80f, 1.30f, "%.3f");
    calChanged |= ImGui::SliderFloat("Reach scale L", &scaleL, 0.80f, 1.30f, "%.3f");
    calChanged |= ImGui::SliderFloat("Height R", &heightR, -0.20f, 0.50f, "%.3f m");
    calChanged |= ImGui::SliderFloat("Height L", &heightL, -0.20f, 0.50f, "%.3f m");
    calChanged |= ImGui::SliderFloat("Elbow swing R", &swingR, -3.0f, 3.0f, "%.2f");
    calChanged |= ImGui::SliderFloat("Elbow swing L", &swingL, -3.0f, 3.0f, "%.2f");
    calChanged |= ImGui::SliderFloat("Elbow pole R", &poleR, -180.0f, 180.0f, "%.1f deg");
    calChanged |= ImGui::SliderFloat("Elbow pole L", &poleL, -180.0f, 180.0f, "%.1f deg");

    ImGui::Separator();
    ImGui::TextUnformatted("Wrist rotation offset (palm/finger alignment, deg)");
    calChanged |= ImGui::SliderFloat("Wrist R pitch", &wRp, -180.0f, 180.0f, "%.1f");
    calChanged |= ImGui::SliderFloat("Wrist R yaw",   &wRy, -180.0f, 180.0f, "%.1f");
    calChanged |= ImGui::SliderFloat("Wrist R roll",  &wRr, -180.0f, 180.0f, "%.1f");
    calChanged |= ImGui::SliderFloat("Wrist L pitch", &wLp, -180.0f, 180.0f, "%.1f");
    calChanged |= ImGui::SliderFloat("Wrist L yaw",   &wLy, -180.0f, 180.0f, "%.1f");
    calChanged |= ImGui::SliderFloat("Wrist L roll",  &wLr, -180.0f, 180.0f, "%.1f");

    ImGui::Separator();
    // Auto-calibration: T-pose sample from the same controller poses that draw the gizmo hands.
    // Press "Start", stretch arms out to the sides, stand straight. We derive shoulder offsets
    // and arm scale, then save to vrik_calibration.ini next to dxgi.dll.
    int  cState = OpenXRManager::Get().GetCalibrationState();
    if (cState == 1) {
        float prog = OpenXRManager::Get().GetCalibrationProgress();
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f),
                           "AUTO-CALIBRATING: stretch arms STRAIGHT OUT to the sides,");
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.3f, 1.0f),
                           "stand straight facing forward. Hold for %.1fs (%.0f%%).",
                           (1.0f - prog) * 4.0f, prog * 100.0f);
        ImGui::ProgressBar(prog, ImVec2(280, 0));
    } else {
        if (ImGui::Button("Start Auto-Calibration (T-pose, 4s)", ImVec2(280, 0))) {
            OpenXRManager::Get().StartAutoCalibration(4.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Press once, then stretch BOTH gizmo hands OUT to the sides at shoulder height\n"
                              "and stand straight facing forward. The mod measures the visible controller\n"
                              "positions, computes shoulder pivots + arm scale, and saves the result\n"
                              "to vrik_calibration.ini.");
        }
        if (cState == 2) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Saved.");
        }
    }

    ImGui::Separator();
    // CAMERA -> HEAD alignment. The FPP camera is mounted ~0.45 m ahead of the avatar's head, so
    // the view sits in front of the body. Stand straight looking forward and press Bake: the
    // (head - camera) offset is measured and baked, shifting the view back onto the head. The
    // Tracking/Camera "Head" sliders then fine-tune ON TOP (they stay at 0).
    {
        float cb[3]; OpenXRManager::Get().GetCameraOffset(cb);
        ImGui::TextUnformatted("Camera <-> Head alignment");
        if (ImGui::Button("Bake camera onto head", ImVec2(180, 0))) {
            OpenXRManager::Get().BakeCameraOffset();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Start VR hand tracking, stand straight looking forward, then press.\n"
                              "Measures the head-bone vs FPP-camera offset and moves the view back\n"
                              "onto your avatar's head. Fine-tune with the Tracking/Camera Head sliders.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear##cambake", ImVec2(90, 0))) {
            OpenXRManager::Get().ClearCameraOffset();
            OpenXRManager::Get().SaveCalibrationToFile();
        }
        ImGui::Text("baked offset: R %.3f  Fwd %.3f  Up %.3f", cb[0], cb[1], cb[2]);
    }

    ImGui::Separator();
    bool apply  = ImGui::Button("Apply Calibration");
    ImGui::SameLine();
    bool save   = ImGui::Button("Save");
    ImGui::SameLine();
    bool load   = ImGui::Button("Load");
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults")) {
        scaleR = 1.05f; scaleL = 1.06f; heightR = 0.0f; heightL = 0.0f;
        swingR = 1.0f; swingL = 1.0f; poleR = 0.0f; poleL = 0.0f;
        wRp = 0.0f; wRy = -90.0f; wRr = 0.0f; wLp = -180.0f; wLy = -90.0f; wLr = 0.0f;
        OpenXRManager::Get().SetShoulderAnatomical(0.14f, -0.17f, 0.05f, -0.14f, -0.17f, 0.05f);
        calChanged = true;
    }

    if (calChanged || apply) {
        OpenXRManager::Get().SetVRHandCalib(scaleR, scaleL, heightR, heightL,
                                            swingR, swingL, poleR, poleL,
                                            wRp, wRy, wRr, wLp, wLy, wLr);
    }
    // AUTOSAVE: persist edits once the drag/press is released, so tweaks survive a
    // restart without requiring the user to remember the Save button.
    {
        static bool s_calDirty = false;
        if (calChanged) s_calDirty = true;
        if (s_calDirty && !ImGui::IsAnyItemActive()) {
            OpenXRManager::Get().SaveCalibrationToFile();
            s_calDirty = false;
        }
    }
    if (save) OpenXRManager::Get().SaveCalibrationToFile();
    if (load) {
        // The two-way sync above pulls the loaded values into the sliders next frame.
        OpenXRManager::Get().LoadCalibrationFromFile();
    }
}

void ReleaseGameMouseCapture() {
    ClipCursor(nullptr);
    ReleaseCapture();

    CURSORINFO cursorInfo{sizeof(CURSORINFO)};
    if (GetCursorInfo(&cursorInfo) && (cursorInfo.flags & CURSOR_SHOWING) == 0) {
        while (ShowCursor(TRUE) < 0) {
        }
    }
}

void UpdateImGuiMouseFromCursor(HWND hwnd, float backbufferWidth, float backbufferHeight) {
    ImGuiIO& io = ImGui::GetIO();

    RECT client{};
    POINT cursor{};
    if (hwnd && GetClientRect(hwnd, &client) && GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
        io.AddMousePosEvent(
            static_cast<float>(cursor.x),
            static_cast<float>(cursor.y));
    }

    io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}

// Stereo panel, carried over from the testbed overlay (testbed/src/overlay_imgui.cpp).
//
// Nothing here goes through LiveControlsUiState: the engine hooks read these globals on every
// frame, so edits apply immediately and the Save button has nothing to do with them. That is
// the same contract the testbed panel had, and it is what makes this usable for tuning IPD
// with the headset on.
void DrawStereoControls() {
    if (!CyberpunkVR_StereoModuleLoaded) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Stereo module not installed.");
        ImGui::TextWrapped("The engine hooks did not load, so everything below is inert and the "
                           "headset is on the plain mono path. Either "
                           "CyberpunkVR_StereoModuleEnable is 0, or bin\\x64\\vrport_nostereo.txt "
                           "exists. See the \"Stereo:\" line in cyberpunkvrport.log.");
        return;
    }

    // Three separate questions, reported separately, because each fails on its own:
    // is the second view rendering, did we get a colour frame from it, and did it reach
    // the headset.
    const uint32_t eyeAge = CyberpunkVR_DebugVrcamEyeAgeMs;
    const bool eyeEverProduced = eyeAge != 0xFFFFFFFFu;
    const bool eyeFresh = eyeEverProduced && eyeAge <= CyberpunkVR_StereoEyeMaxAgeMs;
    const bool submitOn = CyberpunkVR_StereoSubmit != 0;

    // Which eye VRCAM lands in is CyberpunkVR_MainIsRightEye's to decide -- the submit picks
    // eye (MainIsRightEye ? 0 : 1) for it -- so the wording is derived rather than written down.
    // It used to say "right" in four places while the default sent VRCAM to the LEFT eye, which
    // is the sort of label that costs an hour of looking in the wrong place.
    const char* kVrcamEye = CyberpunkVR_MainIsRightEye ? "LEFT" : "RIGHT";
    const char* kMainEye  = CyberpunkVR_MainIsRightEye ? "RIGHT" : "LEFT";

    if (submitOn && eyeFresh) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f),
                           "Stereo active  (%s = VRCAM, %s = MAIN)", kVrcamEye, kMainEye);
    } else if (submitOn) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "No second eye right now -> both eyes get MAIN (mono)");
        ImGui::TextDisabled("Expected in menus and while loading. If it stays here in gameplay: "
                            "the VRCAM component is off, or vrcam.json names a camera the player "
                            "entity does not carry.");
    } else {
        ImGui::TextDisabled("Stereo submit off -> both eyes get MAIN (mono)");
    }
    if (eyeEverProduced)
        ImGui::TextDisabled("vrcam eye age %u ms  (stale over %u)   eye submits %llu",
                            eyeAge, CyberpunkVR_StereoEyeMaxAgeMs,
                            static_cast<unsigned long long>(CyberpunkVR_DebugStereoEyeSubmits));
    else
        ImGui::TextDisabled("vrcam eye: never produced a frame");
    ImGui::Separator();

    bool submit = submitOn;
    char submitLabel[64];
    std::snprintf(submitLabel, sizeof(submitLabel), "Send VRCAM to the %s EYE", kVrcamEye);
    if (ImGui::Checkbox(submitLabel, &submit))
        CyberpunkVR_StereoSubmit = submit ? 1 : 0;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off = the old behaviour, MAIN duplicated into both eyes.\n"
                          "On = the %s eye gets the VRCAM view's own final colour --\n"
                          "the same image the desktop mirror shows -- re-encoded to the\n"
                          "swapchain's sRGB format and scaled to the eye size.", kVrcamEye);

    // No separate capture switch: the submit path drives CyberpunkVR_StereoEyeCapture from the
    // checkbox above, so the per-frame snapshot cost appears and disappears with the feature
    // instead of being a second thing to remember to turn off.
    ImGui::TextDisabled("snapshot copies %llu   skips %llu   vrcam RTV hits %llu",
                        static_cast<unsigned long long>(CyberpunkVR_DebugStableCopies),
                        static_cast<unsigned long long>(CyberpunkVR_DebugStableSkips),
                        static_cast<unsigned long long>(CyberpunkVR_DebugMirrorRtvHits));

    int maxAge = static_cast<int>(CyberpunkVR_StereoEyeMaxAgeMs);
    if (ImGui::SliderInt("Eye staleness limit (ms)", &maxAge, 33, 1000))
        CyberpunkVR_StereoEyeMaxAgeMs = static_cast<uint32_t>(maxAge);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How long the last VRCAM frame stays usable. Past this the %s\n"
                          "eye falls back to MAIN -- one eye frozen while the other moves\n"
                          "is far worse to look at than mono.", kVrcamEye);

    ImGui::Separator();
    // Read-only on purpose. VRCAM's upscaler mirrors MAIN's, decided at graph build from MAIN's
    // own flags -- a switch here could only ever disagree with the engine.
    ImGui::TextDisabled("VRCAM DLSS: %s  (follows MAIN's upscaler, no switch)",
                        CyberpunkVR_VrcamDlss ? "ON" : "off");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("On whenever MAIN is upscaling with DLSS: vrcam gets its own\n"
                          "Streamline viewport and renders below its target.\n"
                          "Off for DLAA / TAA / no upscaler. Change it in the game's\n"
                          "graphics settings, not here.");
    }
    bool forceCam = CyberpunkVR_ForceVrcamCam != 0;
    if (ImGui::Checkbox("Match VRCAM projection to MAIN  (fov / zoom / near / far)", &forceCam))
        CyberpunkVR_ForceVrcamCam = forceCam ? 1 : 0;

    ImGui::Separator();
    // Forces the RTT component's isEnabled through the game's RTTI (the CET side re-asserts it,
    // so it survives a reload/respawn). Off means the engine stops rendering the second view
    // entirely, not just our stereo shift -- which is the cheapest way back to plain mono.
    bool vrcamOn = CyberpunkVR_VrcamEnabled != 0;
    if (ImGui::Checkbox("VRCAM component  (RTTI Toggle: force ON/OFF)", &vrcamOn))
        CyberpunkVR_SetVrcamEnabled(vrcamOn ? 1u : 0u);
    ImGui::TextDisabled("component %s   camera %s",
                        CyberpunkVR_VrcamComponentName(), CyberpunkVR_VrcamCameraName());
    ImGui::TextDisabled("view nodes: main %llu   other %llu   vrcam %llu",
                        static_cast<unsigned long long>(CyberpunkVR_DebugViewKeyMainNodes),
                        static_cast<unsigned long long>(CyberpunkVR_DebugViewKeyOtherNodes),
                        static_cast<unsigned long long>(CyberpunkVR_DebugVrcamNodeHits));

    // Separate second swapchain + window mirroring the VRCAM eye (for OBS / desktop preview).
    // Costs a per-frame copy, so it is off unless asked for.
    bool mirrorOn = CyberpunkVR_MirrorOutput != 0;
    if (ImGui::Checkbox("VRCAM Mirror  (separate window, for capture)", &mirrorOn))
        CyberpunkVR_MirrorOutput = mirrorOn ? 1u : 0u;

    // Weapon ADS is not a toggle: the vrcam eye always follows MAIN's vertical FOV, narrowed by
    // the aim zoom. Read-only here because the numbers are the quickest way to tell a wrong FOV
    // from a stale one.
    ImGui::TextDisabled("vrcam fov %.2f  (asset %.2f)   main yy %.5f  fov %.2f   ADS x%.3f",
                        CyberpunkVR_DebugVrcamWantFov, CyberpunkVR_DebugVrcamBaseFov,
                        CyberpunkVR_DebugMainProjYY, CyberpunkVR_DebugMainCamFov,
                        CyberpunkVR_MainAdsZoomFactor);

    // Is the engine still rendering from the camera we write? `sep` is the angle between the render
    // camera's own quaternion and the head quaternion we wrote: it sits near zero all through
    // ordinary play and jumps to tens of degrees the moment a device or a scene camera takes the
    // view. ENGAGED means the FinalCamera fallback is holding that view on the head; `enters`
    // counts how many times that has happened this session, so a number that climbs while nothing
    // is being interacted with says the threshold is too tight.
    {
        // The early half: correct the camera the engine has just located, whichever one it is. This
        // is what the mod this port descends from does, and the reason its terminals are clean.
        int locMode = CyberpunkVR_CamWriteInLocate;
        ImGui::TextDisabled("Head into the LOCATED camera (upstream):");
        if (ImGui::RadioButton("off##loc", locMode == 0)) CyberpunkVR_CamWriteInLocate = 0;
        ImGui::SameLine();
        if (ImGui::RadioButton("only on scene cameras##loc", locMode == 2))
            CyberpunkVR_CamWriteInLocate = 2;
        ImGui::SameLine();
        if (ImGui::RadioButton("always##loc", locMode == 1)) CyberpunkVR_CamWriteInLocate = 1;
        // Above 1 the world shrinks, and only while the take-over holds. The hands are the thing to
        // judge it by: they are the only object whose real size the wearer already knows.
        // Near objects are the judge: they are the first thing to double when the separation is
        // wrong, and the last to fuse when it is right. Negative flips which side this eye is on.
        // The A/B for a one-frame jump that is visible on the flat monitor too: with this off the
        // located camera is left exactly as the game made it, so anything still jumping is the
        // game's own.
        bool pairFar = CyberpunkVR_SceneCamFarMainToRtt != 0;
        if (ImGui::Checkbox("   pair the eyes in scenes elsewhere", &pairFar))
            CyberpunkVR_SceneCamFarMainToRtt = pairFar ? 1 : 0;
        ImGui::TextDisabled("   scene rot: torn %llu   held %llu   no-eye %llu  (jump > %d deg)",
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneRotTorn),
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneRotHeld),
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneRotNoEye),
                            CyberpunkVR_SceneCamRotJumpDeg);
        ImGui::TextDisabled("   second eye left to the engine: %llu frames",
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneRotYielded));
        // The question this answers: is the second eye's rotation being written at its view-create
        // at all, or written and ignored? A climbing count with the eye still world-locked says the
        // field is not what that view rotates by.
        ImGui::TextDisabled("   second eye rotation written at view-create: %llu",
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneEyeQuatWrites));
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("eye separation in scenes elsewhere", &CyberpunkVR_SceneCamFarIpdScale,
                           -2.0f, 2.0f, "%.2f");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderFloat("world trim on scene cameras", &CyberpunkVR_SceneCamWorldScale,
                           0.50f, 3.00f, "%.3f");
        bool posLoc = CyberpunkVR_CamPosInLocate != 0;
        if (ImGui::Checkbox("   ...and the viewpoint too  (the 2x world)", &posLoc))
            CyberpunkVR_CamPosInLocate = posLoc ? 1 : 0;
        bool holdFov = CyberpunkVR_SceneCamHoldFov != 0;
        if (ImGui::Checkbox("   ...and hold the FOV too  (the device zoom)", &holdFov))
            CyberpunkVR_SceneCamHoldFov = holdFov ? 1 : 0;
        ImGui::TextDisabled("   scene asked %.2f deg, refused %llu times",
                            CyberpunkVR_DebugSceneFovWas,
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneFovForced));
        bool fbOn = CyberpunkVR_CamFinalFallback != 0;
        if (ImGui::Checkbox("Take over scene cameras  (late; rotates lighting)", &fbOn))
            CyberpunkVR_CamFinalFallback = fbOn ? 1 : 0;
        bool pinPos = CyberpunkVR_CamFinalFallbackPos != 0;
        if (ImGui::Checkbox("   ...and hold the viewpoint on the player", &pinPos))
            CyberpunkVR_CamFinalFallbackPos = pinPos ? 1 : 0;
        // The upstream half: adopting the scene camera at PatchCamera is what keeps the LIGHTING
        // with the view. Without it the eye turns and the lighting does not.
        bool adopt = CyberpunkVR_SceneCamAttach != 0;
        if (ImGui::Checkbox("   ...by adopting the scene camera upstream", &adopt))
            CyberpunkVR_SceneCamAttach = adopt ? 1 : 0;
        ImGui::TextDisabled("   adopted writes %llu   (match under %d deg / %d cm)",
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneCamWrites),
                            CyberpunkVR_SceneCamAttachDeg, CyberpunkVR_SceneCamAttachCm);
        bool allViews = CyberpunkVR_CamFinalFallbackAllViews != 0;
        if (ImGui::Checkbox("   ...on shadows and reflections too  (rotates the lighting)",
                            &allViews))
            CyberpunkVR_CamFinalFallbackAllViews = allViews ? 1 : 0;
        ImGui::TextDisabled("   anchor %.1f cm  (pin under %d)   cams moved %llu",
                            CyberpunkVR_DebugCamAnchorCm, CyberpunkVR_CamFinalFallbackMaxCm,
                            static_cast<unsigned long long>(CyberpunkVR_DebugCamAnchorLocates));
        {
            // THE GESTURE'S OWN GATE. The synthetic X is emitted only while menu mode is 0, so any UI
            // context -- an elevator's floor panel among them -- suppresses it while the physical X
            // still passes. This says which of the two is true at the moment the gesture fails.
            const float* shGate = GetShotShared();
            const unsigned devScr = shGate
                ? reinterpret_cast<const volatile uint32_t*>(shGate)[vrshared::kDeviceScreenOpen] : 0u;
            const bool pressLive = GetTickCount64() < g_vrInteractPressUntilMs.load(std::memory_order_relaxed);
            ImGui::TextDisabled("   interact gate: menu mode %d   device screen %u   press live %d",
                                g_menuModeValue, devScr, pressLive ? 1 : 0);
        }
        // THE POSE RING, because a view can only be reprojected with the sample that drew it.
        //
        // The match is made on the render camera's QUATERNION alone. While a scene plays the port gives
        // both eyes ONE rotation, latched -- so consecutive frames and both eyes carry the identical
        // quaternion and the ring cannot tell those samples apart. `ties` is how many it could not
        // separate; `tie hits` counts the frames where that ambiguity was real. A view reprojected with
        // a neighbour's sample lands a few centimetres away and recovers on the next frame, which is
        // visible in the headset and never on the flat monitor.
        ImGui::TextDisabled("   pose ring  main %llu/%llu miss   ties %u  tie hits %llu   age %u",
                            static_cast<unsigned long long>(CyberpunkVR_DebugFinalMatch),
                            static_cast<unsigned long long>(CyberpunkVR_DebugFinalNoMatch),
                            CyberpunkVR_DebugFinalTies,
                            static_cast<unsigned long long>(CyberpunkVR_DebugFinalTieHits),
                            CyberpunkVR_DebugFinalAge);
        {
            bool useBase = CyberpunkVR_SceneCamFarUseVrcamBase != 0;
            if (ImGui::Checkbox("Pair against the engine's own eye position  (not the field we write)",
                                &useBase))
                CyberpunkVR_SceneCamFarUseVrcamBase = useBase ? 1 : 0;
        }
        ImGui::TextDisabled("   submitted label step %.3f m   (what the compositor reprojects by)",
                            CyberpunkVR_DebugSubmitPosStep);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderInt("refuse a scene jump over (cm)", &CyberpunkVR_SceneCamFarJumpCm, 0, 30);
        // THE ADDRESS THE SHOVE LANDS ON, so a hardware write breakpoint can name whoever does it.
        // +0xE0 is the position field; the component itself moves between sessions, so it is printed
        // rather than remembered.
        ImGui::TextDisabled("   second eye component %p   (+0xE0 = position)",
                            reinterpret_cast<void*>(g_camObjVrcam.load(std::memory_order_relaxed)));
        {
            bool posAtBuild = CyberpunkVR_SceneCamFarPosAtViewBuild != 0;
            if (ImGui::Checkbox("   ...and give the steady position to the second eye too", &posAtBuild))
                CyberpunkVR_SceneCamFarPosAtViewBuild = posAtBuild ? 1 : 0;
            ImGui::TextDisabled("      second eye position written %llu",
                                static_cast<unsigned long long>(CyberpunkVR_DebugSceneEyePosWrites));
        }
        ImGui::TextDisabled("   held %llu   taken after %d frames %llu",
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneJumpHeld),
                            CyberpunkVR_SceneCamFarJumpFrames,
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneJumpTaken));
        {
            bool trace = CyberpunkVR_SceneTrace != 0;
            if (ImGui::Checkbox("Trace the scene view to bin/x64/cyberpunkvrport_bd.csv", &trace))
                CyberpunkVR_SceneTrace = trace ? 1 : 0;
            ImGui::SameLine();
            ImGui::TextDisabled("%llu rows",
                                static_cast<unsigned long long>(CyberpunkVR_DebugSceneTraceRows));
        }
        {
            bool oneSrc = CyberpunkVR_SceneCamFarDeltaOneSource != 0;
            if (ImGui::Checkbox("One head-translation source in a scene elsewhere", &oneSrc))
                CyberpunkVR_SceneCamFarDeltaOneSource = oneSrc ? 1 : 0;
            ImGui::SameLine();
            ImGui::TextDisabled("%llu frames pinned",
                                static_cast<unsigned long long>(CyberpunkVR_DebugSceneDeltaPinned));
        }
        {
            bool latchYaw = CyberpunkVR_SceneCamFarLatchYaw != 0;
            if (ImGui::Checkbox("Hold the room heading while a scene plays elsewhere", &latchYaw))
                CyberpunkVR_SceneCamFarLatchYaw = latchYaw ? 1 : 0;
            ImGui::SameLine();
            ImGui::TextDisabled("latched %llu",
                                static_cast<unsigned long long>(CyberpunkVR_DebugSceneYawLatches));
        }
        ImGui::TextDisabled("   script probe  %.0f  %.0f  %.0f  %.0f",
                            CyberpunkVR_DebugScriptProbe[0], CyberpunkVR_DebugScriptProbe[1],
                            CyberpunkVR_DebugScriptProbe[2], CyberpunkVR_DebugScriptProbe[3]);
        ImGui::TextDisabled("   room heading %.1f deg   step %.2f deg   engine yaw valid %d",
                            CyberpunkVR_DebugFlatYawDeg, CyberpunkVR_DebugFlatYawStepDeg,
                            CyberpunkVR_DebugEngineYawValid);
        ImGui::TextDisabled("   head delta step %.3f m   rebuilt from a fresh sample %llu",
                            CyberpunkVR_DebugHeadDeltaStep,
                            static_cast<unsigned long long>(CyberpunkVR_DebugDeltaRebuilt));
        ImGui::TextDisabled("   patch -> locate gap %.3f m   different object %llu times",
                            CyberpunkVR_DebugPatchToLocateM,
                            static_cast<unsigned long long>(CyberpunkVR_DebugPatchObjMismatch));
        ImGui::TextDisabled("   paired view step %.3f m   room offset %.2f m   (jump vs distance)",
                            CyberpunkVR_DebugScenePosStep, CyberpunkVR_DebugRoomOffsetM);
        ImGui::TextDisabled("   scene view: engine component step %.3f m   eye term dropped %llu",
                            CyberpunkVR_DebugSceneVrPosStep,
                            static_cast<unsigned long long>(CyberpunkVR_DebugSceneEyeDrop));
        ImGui::TextDisabled("              ambiguous settled by order %llu",
                            static_cast<unsigned long long>(CyberpunkVR_DebugFinalTieContinuity));
        ImGui::TextDisabled("              vrcam %llu/%llu miss   age %u",
                            static_cast<unsigned long long>(CyberpunkVR_DebugVrcamFinalMatch),
                            static_cast<unsigned long long>(CyberpunkVR_DebugVrcamFinalNoMatch),
                            CyberpunkVR_DebugVrcamFinalAge);
        ImGui::TextDisabled("   %s   sep %.2f deg (in %d / out %d)   %.1f cm (in %d / out %d)"
                            "   enters %llu",
                            CyberpunkVR_DebugCamFallbackActive ? "ENGAGED" : "idle",
                            CyberpunkVR_DebugCamFallbackDeg,
                            CyberpunkVR_CamFinalFallbackEnterDeg,
                            CyberpunkVR_CamFinalFallbackExitDeg,
                            CyberpunkVR_DebugCamFallbackCm,
                            CyberpunkVR_CamFinalFallbackEnterCm,
                            CyberpunkVR_CamFinalFallbackExitCm,
                            static_cast<unsigned long long>(CyberpunkVR_DebugCamFallbackEnters));
    }

    if (ImGui::CollapsingHeader("Diagnostics")) {
        ImGui::Checkbox("Compact ADS camera telemetry (in-headset)", &g_showCompactAdsTelemetry);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("A small panel that stays up after F10 is closed, reporting what the "
                              "ENGINE did to the camera when the sights came up:\n"
                              "delta and peak in centimetres, right/forward/up in the heading's own "
                              "basis, against a baseline captured while hip firing.\n"
                              "Stereo submission is unaffected.");
        }
        if (g_showCompactAdsTelemetry) {
            ImGui::Indent();
            ImGui::SliderFloat("Telemetry X", &g_compactAdsTelemetryX, 0.10f, 0.90f, "%.2f");
            ImGui::SliderFloat("Telemetry Y", &g_compactAdsTelemetryY, 0.10f, 0.90f, "%.2f");
            ImGui::TextDisabled("Normalised position in the eye image");
            ImGui::Unindent();
        }
        ImGui::Separator();

        bool slog = CyberpunkVR_StereoLog != 0;
        if (ImGui::Checkbox("Stereo logging -> cyberpunkvrport.log", &slog))
            CyberpunkVR_StereoLog = slog ? 1 : 0;

        bool stable = CyberpunkVR_StableCopy != 0;
        if (ImGui::Checkbox("Committed snapshot of the VRCAM final", &stable))
            CyberpunkVR_StableCopy = stable ? 1 : 0;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Off = read the engine's transient directly. That target is a\n"
                              "frame-graph allocation which a later pass aliases, which is what\n"
                              "made the image alternate bright/dark. Leave on.");
        bool fromTonemap = CyberpunkVR_StableFromTonemap != 0;
        if (ImGui::Checkbox("Snapshot at the tonemap node instead of RenderFinal2D", &fromTonemap))
            CyberpunkVR_StableFromTonemap = fromTonemap ? 1 : 0;

        bool prof = CyberpunkVR_ProfEnable != 0;
        if (ImGui::Checkbox("Node CPU profiler  (per-node self+incl ms)", &prof))
            CyberpunkVR_ProfEnable = prof ? 1 : 0;
        ImGui::TextDisabled("frame %.2f ms   dispatch: main %.2f (%u)  vrcam %.2f (%u)",
                            CyberpunkVR_ProfFrameMs,
                            CyberpunkVR_ProfDispMainMs, CyberpunkVR_ProfDispMainNodes,
                            CyberpunkVR_ProfDispVrcamMs, CyberpunkVR_ProfDispVrcamNodes);
        if (ImGui::Button("Dump node audit -> log  (resets window)"))
            CyberpunkVR_ProfDumpNodes();
        if (prof) {
            // Top-15 by SELF time. Self is the only rankable column: inclusive double-counts,
            // because SceneDrv contains every scene pass it dispatches.
            static uint32_t rva[15];
            static double msv[15], msm[15];
            static uint32_t cv[15], cm[15];
            const int n = CyberpunkVR_ProfSnapshotNodes(rva, msv, msm, cv, cm, 15);
            ImGui::TextDisabled("top %d by self ms/frame   (main | vrcam)", n);
            for (int i = 0; i < n; ++i) {
                const char* nm = CyberpunkVR_ProfNodeName(rva[i]);
                ImGui::Text("%-30s %6.3f | %6.3f", (nm && nm[0]) ? nm : "?", msm[i], msv[i]);
            }
        }
    }
}

extern "C" __declspec(dllimport) float CyberpunkVR_DebugPhysCrouchDrop;
extern "C" __declspec(dllimport) int32_t CyberpunkVR_DebugPhysCrouchGate;
extern "C" __declspec(dllimport) int32_t CyberpunkVR_DebugPhysCrouchSent;
extern "C" __declspec(dllimport) int32_t CyberpunkVR_DebugPhysCrouchLoco;
extern "C" __declspec(dllimport) int32_t CyberpunkVR_DebugPhysCrouchToggles;

// DLSS5 Neural Render panel. Everything here writes the live exported knobs in
// Hooks/NgxEvalPassthrough.cpp directly, so a change is picked up on the next
// evaluation. The status block reports the runtime state (armed / initialised /
// how many per-eye contexts, last NGX result, and the measured NR passes/sec)
// so the effect of a parameter change is visible without opening a log file.
void DrawDlss5NrControls() {
    // --- status ---
    const bool armed  = CyberpunkVR_NgxPassArmed != 0;
    const bool inited = CyberpunkVR_NrInited != 0;
    if (!armed) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f),
                           "NGX evaluate hook not armed yet.");
        ImGui::TextDisabled("Arms on the first DLSS evaluation. Make sure DLSS is enabled in the\n"
                            "game's graphics options and RenoDX/ReShade DLSS add-ons are OFF.");
    } else if (inited) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f),
                           "Neural render initialised  (%d eye context%s)",
                           CyberpunkVR_NrContexts, CyberpunkVR_NrContexts == 1 ? "" : "s");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "Hook armed, NR not initialised yet.");
    }
    ImGui::TextDisabled("passes/sec %.1f   L %u  R %u   lastResult 0x%08X   create 0x%08X",
                        CyberpunkVR_NrPassesPerSec,
                        CyberpunkVR_NrEvalsL, CyberpunkVR_NrEvalsR,
                        CyberpunkVR_NrLastResult, CyberpunkVR_NrCreateResult);
    if (CyberpunkVR_NrDisabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "NR was stopped by a fault (init/create/eval crashed).");
        if (ImGui::Button("Re-enable NR (clear fault)"))
            CyberpunkVR_NrDisabled = 0;
    }
    ImGui::Separator();

    // --- master switch ---
    // One checkbox drives all three of what used to be separate switches (create the feature,
    // run it, write the denoised result back): NR has no useful "on" state that leaves one of the
    // three off, so three checkboxes was three ways to end up looking broken with nothing to show
    // for it. Displayed as checked only when all three already agree, so a stale ini with them out
    // of sync reads as unchecked rather than silently wrong.
    {
        bool nrOn = (CyberpunkVR_NrCreate != 0) && (CyberpunkVR_NrEval != 0) &&
                    (CyberpunkVR_NrWriteBack != 0);
        if (ImGui::Checkbox("DLSS 5 Neural Render Enable", &nrOn)) {
            CyberpunkVR_NrCreate = nrOn ? 1 : 0;
            CyberpunkVR_NrEval = nrOn ? 1 : 0;
            CyberpunkVR_NrWriteBack = nrOn ? 1 : 0;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turns DLSS5 Neural Render on end to end: creates the per-eye NGX\n"
                              "feature 18 (Ray Reconstruction), runs it, and writes the denoised\n"
                              "result back so it is what you see in the headset.\n"
                              "Off = the game's own DLSS output, untouched.");
    }
    CheckboxInt("Alternate eyes (one eye per frame)", &CyberpunkVR_NrAlternateEyes);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Experimental performance trick: denoise only ONE eye each frame --\n"
                          "left on even frames, right on odd -- so a frame does half the NR work.\n"
                          "The other eye shows the game's own image that frame, so each eye's\n"
                          "temporal history updates at half rate. May shimmer or look uneven\n"
                          "between the eyes; the point is to measure the fps gain.");

    ImGui::Separator();
    ImGui::TextUnformatted("Model parameters");

    // --- model parameters (map to DLSSNR.* on the evaluate) ---
    // Names match RenoDX's renodx-dlss5 addon UI.
    static const char* kNrPresetNames[] = {
        "Default", "Preset #1", "Preset #2", "Preset #3",
        "Preset #4", "Preset #5", "Preset #6"
    };
    static const char* kNrStyleNames[] = { "Natural", "Cinematic" };
    {
        int preset = CyberpunkVR_NrPreset;
        if (preset < 0) preset = 0;
        if (preset > 6) preset = 6;
        if (ImGui::Combo("NR Preset", &preset, kNrPresetNames, IM_ARRAYSIZE(kNrPresetNames)))
            CyberpunkVR_NrPreset = preset;
    }
    {
        int style = CyberpunkVR_NrStyle;
        if (style < 0) style = 0;
        if (style > 1) style = 1;
        if (ImGui::Combo("NR Style", &style, kNrStyleNames, IM_ARRAYSIZE(kNrStyleNames)))
            CyberpunkVR_NrStyle = style;
    }
    ImGui::SliderFloat("NR Intensity", &CyberpunkVR_NrIntensity, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Local Tone Strength", &CyberpunkVR_NrLocalTone, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Local Structure Strength", &CyberpunkVR_NrLocalStructure, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Skin Structure Strength", &CyberpunkVR_NrSkinStructure, -1.0f, 5.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("-1 = let the model decide.");

    ImGui::Separator();
    ImGui::TextUnformatted("Motion / depth  (fix for moving colour noise)");
    CheckboxInt("Temporal reset every frame (diagnostic)", &CyberpunkVR_NrReset);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On = no temporal accumulation. If the moving noise stops, the\n"
                          "cause is the motion-vector scale below; if it stays, it is the\n"
                          "colour input (needs the tonemap codec).");
    ImGui::SliderInt("MVec scale base", &CyberpunkVR_NrMVecScaleMode, 0, 3);
    ImGui::TextDisabled("0 = pixel-space (1.0)   1 = +render   2 = -render   3 = display");
    ImGui::SliderFloat("Motion Scale X Multiplier", &CyberpunkVR_NrMotionScaleX, -2.0f, 2.0f, "%.3f");
    ImGui::SliderFloat("Motion Scale Y Multiplier", &CyberpunkVR_NrMotionScaleY, -2.0f, 2.0f, "%.3f");
    ImGui::SliderInt("Depth Conversion", &CyberpunkVR_NrDepthConversion, 0, 2);

    ImGui::Separator();
    ImGui::TextUnformatted("Color / HDR  (used by the write-back codec)");
    ImGui::SliderFloat("Color Strength", &CyberpunkVR_NrColorStrength, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Scene paper-white scale", &CyberpunkVR_NrPaperWhite, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("HDR Transfer Strength", &CyberpunkVR_NrHdrTransfer, 0.0f, 2.0f, "%.2f");
    ImGui::TextDisabled("The four values above take effect once the denoised image is written\n"
                        "back to the game (next implementation step).");
}

// FSR3 frame generator (vr_framegen branch). A real motion-vector + depth interpolated frame per
// eye -- see FrameGen/FrameGenFsr3.cpp -- as opposed to the color-only optical flow tools like
// OFXR-Bridge use. STILL A SMOKE TEST: the interpolated frame is dispatched and thrown away, not
// shown in the headset. This panel is for watching it run without a crash, not for the fps gain
// yet.
void DrawFsr3Controls() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                       "Experimental -- smoke test only. Toggling this changes nothing you see;\n"
                       "it only runs the dispatch and measures whether it stays stable.");
    ImGui::Separator();

    if (CyberpunkVR_FrameGenReady) {
        ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f), "Both eye contexts created.");
    } else if (CyberpunkVR_FrameGenEnable) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "Enabled, waiting for the first DLSS eval.");
    } else {
        ImGui::TextDisabled("Off.");
    }
    ImGui::TextDisabled("dispatches %u   lastResult %s", CyberpunkVR_FrameGenDispatches,
                        CyberpunkVR_FrameGenLastResult == 0 ? "OK" : "FAILED");
    if (CyberpunkVR_FrameGenDisabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "Stopped by a fault (the dispatch crashed and was latched off).");
        if (ImGui::Button("Re-enable (clear fault)")) CyberpunkVR_FrameGenDisabled = 0;
    }
    ImGui::Separator();

    CheckboxInt("FSR3 Frame Generator Enable", &CyberpunkVR_FrameGenEnable);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Runs a real ffxFrameInterpolationDispatch per eye every frame, using the\n"
                          "game's own motion vectors and depth (dilated first, FSR3-upscaler style)\n"
                          "rather than color-only optical flow. The result is discarded -- this does\n"
                          "not change what you see yet. Independent of DLSS5-NR: works with it on or off.");
}

bool DrawLiveControls(LiveControlsUiState& state) {
    bool changed = false;

    if (ImGui::Button("Recenter HMD (F7)")) {
        RequestLiveControlsRecenter();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SetLiveControlsUiState(&state, 1);
    }

    if (ImGui::BeginTabBar("CyberpunkVRPortTabs")) {
        if (ImGui::BeginTabItem("General")) {
            if (ImGui::CollapsingHeader("View / Resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= DrawFovControl(state);
                changed |= CheckboxInt("VR menu quad", &state.xrMenuRect);
                changed |= ImGui::SliderFloat("VR menu FOV", &state.xrMenuFov, 30.0f, 120.0f, "%.1f deg");
            }

            if (ImGui::CollapsingHeader("Stereo", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Motion prediction (ms)", &state.xrMotionPredictMs, 0.0f, 60.0f, "%.1f ms");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Forward-predicts the head pose by this many ms using head\n"
                              "velocity, hiding render-to-photon latency. 0 = off.\n"
                              "Tune up until motion feels responsive without overshoot.");
        }
        changed |= ImGui::SliderFloat("Stereo separation x", &state.xrStereoScale, 0.25f, 5.0f, "%.2fx");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Personal fine-tune on the auto IPD. 1.0 = calibrated natural\n"
                              "separation, auto-scaled to the headset's runtime IPD. Nudge\n"
                              "0.8-1.2 for taste; crank to 3-5x to exaggerate depth and make\n"
                              "the eye alternation obvious on the flat monitor for testing.");
        }
        // ── World scale + honest IPD ──
        changed |= ImGui::SliderFloat("World scale", &state.xrWorldScale, 0.20f, 3.0f, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Scales eye separation AND head translation together.\n"
                              "Lower it (e.g. 0.8) to make the world look\n"
                              "BIGGER / yourself smaller; raise to shrink the world. Use this if V\n"
                              "and NPCs feel too large.");
        }
        changed |= ImGui::SliderFloat("IPD scale", &state.xrIpdScale, 0.50f, 2.0f, "%.2fx");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Eye-separation multiplier on the runtime IPD. 1.0 = the neutral\n"
                              "baseline (+-0.033 m on a typical headset).\n"
                              "Affects stereo depth (diorama vs giant), NOT the monocular size.");
        }
        bool reuseLastFrame = state.xrReuseLastFrame != 0;
        if (ImGui::Checkbox("Reuse last clean frame", &reuseLastFrame)) {
            state.xrReuseLastFrame = reuseLastFrame ? 1 : 0;
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("On stale ticks, re-submit the last clean captured eye and let\n"
                              "the compositor reproject it, instead of warping stale content\n"
                              "again. May lower submit rate\n"
                              "toward the capture rate. Off = always warp the stale eye.");
        }
        // The "Pose pair-lock" checkbox is gone: it toggled a freeze that only meant something
        // under AER's one-camera eye alternation. The ini key still round-trips so old files load.
            }

            if (ImGui::CollapsingHeader("Tracking / Camera")) {
        ImGui::TextUnformatted("Locomotion direction is set in the Controls tab.");
        changed |= CheckboxInt("Disable Mouse Y (Pitch)", &state.xrDisableMouseY);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Suppress mouse/right-stick pitch so only the HMD controls\n"
                              "vertical look. Applied by the CET VRIK mod and the\n"
                              "XInput merge. On by default.");
        }
        // "Fix Head" removed. It switched the view to 3DoF, and it did not stop at dropping the
        // head translation -- it dropped these three offsets and the calibration bakes with it,
        // then hid the very sliders that were needed to put the view right. The offsets are
        // always live now, and they reach BOTH eyes.
        changed |= ImGui::SliderFloat("Head X right", &state.xrHeadOffsetX, -0.50f, 0.50f, "%.3f m");
        changed |= ImGui::SliderFloat("Head Y forward", &state.xrHeadOffsetY, -0.50f, 0.50f, "%.3f m");
        changed |= ImGui::SliderFloat("Head Z up", &state.xrHeadOffsetZ, -0.50f, 0.50f, "%.3f m");

        ImGui::Separator();
        ImGui::TextUnformatted("In a vehicle only (added on top of the Head sliders)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "The three sliders above are a STANDING calibration. Seated, the game's own\n"
                "vehicle camera is already where it should be -- which is why the port drops\n"
                "its two automatic bakes in a vehicle -- so a standing offset carries the view\n"
                "off the seat instead of correcting it.\n\n"
                "These three are added to the Head sliders while you are in a vehicle and\n"
                "ignored the moment you step out, so the car and the street can be tuned\n"
                "separately. Zero = the car keeps exactly the offset it has today.");
        }
        changed |= ImGui::SliderFloat("Car Head X right", &state.xrVehHeadOffsetX, -0.50f, 0.50f, "%.3f m");
        changed |= ImGui::SliderFloat("Car Head Y forward", &state.xrVehHeadOffsetY, -0.50f, 0.50f, "%.3f m");
        changed |= ImGui::SliderFloat("Car Head Z up", &state.xrVehHeadOffsetZ, -0.50f, 0.50f, "%.3f m");
        {   // Live, so a slider that is doing nothing says so instead of being blamed.
            ImGui::TextDisabled(g_isInVehicle ? "   (in a vehicle: these are live)"
                                              : "   (on foot: these are ignored)");
        }
            }

            if (ImGui::CollapsingHeader("Debug Gizmos")) {
                ImGui::TextUnformatted("Raw hand overlay / debug gizmos:");
                ImGui::Checkbox("Enable hand overlay", &g_drawHandLocator);
                ImGui::Checkbox("Draw 3D hand proxy", &g_drawHandProxy3D);
                ImGui::Checkbox("Draw debug wire/axes", &g_drawHandDebugAxes);
                ImGui::SliderFloat("Locator scale", &g_handLocatorScale, 0.50f, 2.00f, "%.2f");
            }

            if (ImGui::CollapsingHeader("DLSS / Debug")) {
        { int vl = g_verboseLog; if (CheckboxInt("Verbose log (spammy diag)", &vl)) g_verboseLog = vl; }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Off by default for a clean cyberpunkvrport.log. Enable only\n"
                              "when capturing ClipCursor / depth / hook diagnostics.");
        }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls")) {
            ImGui::TextWrapped("VR controller input is merged into XInput gamepad 0 so the game's "
                               "native gamepad bindings apply (jump = A, dodge = B, reload = X, "
                               "weapon swap = Y, fire = RT, aim = LT, grenade = RG, scanner = LG).");
            ImGui::Separator();
            ImGui::TextUnformatted("Reach for it -- point with the hand, not the head");
            changed |= CheckboxInt("Select what the hand points at", &state.xrHandInteract);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "ON  - what the game treats as \"what you are looking at\" comes from the\n"
                    "      pointing HAND: the interaction prompt, the scanner target and the\n"
                    "      look-at object all follow the palm. Point at a door and it is the\n"
                    "      door that is offered, wherever you happen to be looking.\n"
                    "OFF - vanilla: the ray leaves the camera and the head chooses.\n\n"
                    "How you then TRIGGER what the hand has selected is unchanged and already\n"
                    "part of the port: press X, lay a hand on a panel or terminal and it presses\n"
                    "itself, or put the palm over a pickup and squeeze the grip.");
            }
            // THERE IS NO "LEFT-HANDED" CHECKBOX, and there deliberately is not. It answered which
            // hand the player points with -- a question the reach answers on its own, every frame,
            // and better: a setting is a guess made once, while the measurement follows whichever
            // hand is actually doing the job. See VRUpdatePointingHand.
            // CALIBRATION, and meant to be temporary. The palm bone's own forward runs down the
            // THUMB, so the ray has to be turned onto whichever local axis the fingers run along --
            // a fact about the rig, settled by one look in the headset rather than by argument.
            // Once it is known, the default in LauncherConfig.cpp is that answer and this combo can
            // go. Point at a door and step through the entries: the right one is the one where the
            // prompt appears for what the INDEX finger is aimed at.
            if (state.xrHandInteract) {
                const char* kAxisNames[] = { "+X  (default)", "-X", "+Y  (raw bone, thumb)", "-Y", "+Z", "-Z" };
                int axis = (state.xrHandInteractAxis >= 0 && state.xrHandInteractAxis <= 5)
                               ? state.xrHandInteractAxis : 0;
                if (ImGui::Combo("Pointing axis", &axis, kAxisNames, 6)) {
                    state.xrHandInteractAxis = axis;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Which axis of the palm bone the ray leaves along.\n"
                                      "+Y is the bone's own forward, which points along the thumb --\n"
                                      "that is the one that looked wrong in the first field test.");
                }
                int hold = state.xrHandTargetHoldMs < 0 ? 0
                               : (state.xrHandTargetHoldMs > 1500 ? 1500 : state.xrHandTargetHoldMs);
                if (ImGui::SliderInt("Target hold (ms)", &hold, 0, 1500, "%d ms")) {
                    state.xrHandTargetHoldMs = hold;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How long the pointing hand KEEPS a target it has acquired when the ray\n"
                        "briefly stops hitting it (a gap between two buttons, a thin panel edge).\n"
                        "Without it the target jumps to the other hand the instant the ray misses,\n"
                        "so pointing your second hand at the same panel steals it from the first --\n"
                        "even though the first hand never moved.\n"
                        "Higher = the target sticks to the hand that got it. 0 = swap immediately.");
                }
            }
            // TICKED IS NOT THE SAME AS WORKING. The hook can be installed, reached, and still
            // decline every ray -- VRIK not solved yet, or the origin filter rejecting a ray that is
            // not the player's. Two counters tell those apart from inside the headset, which is the
            // only place this feature can actually be judged.
            if (state.xrHandInteract) {
                ImGui::Text("rays: %d moved to the hand, %d left alone",
                            g_VRLookAtPatched, g_VRLookAtSkipped);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Both still at 0 = the hook is not being reached at all.\n"
                                      "Only the second one climbing = it is being reached and\n"
                                      "refusing: no solved hand pose, or the ray is not ours.");
                }
            }

            ImGui::Separator();

            // Weapon aim: bullets/projectiles fly down the WEAPON BARREL (controller-pointed) instead
            // of the camera crosshair. Hooks the projectile launch orientation provider and feeds it
            // the game's own muzzle world transform. Writes shared[58]; the RED4ext plugin applies it.
            {
                static bool s_weaponAim = true;   // default ON — backend's m_weaponAimEnable also defaults to 1
                if (ImGui::Checkbox("Hand aim  (off = Decoupled VR Head Aim)", &s_weaponAim)) {
                    OpenXRManager::Get().SetWeaponAimEnable(s_weaponAim ? 1 : 0);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("ON (Hand Aim): the controller points the weapon and VRIK drives the arms.\n"
                                      "OFF (Decoupled VR Head Aim): the WEAPON follows your head instead, the game\n"
                                      "keeps owning its position and its ADS animations, and VRIK stands down for\n"
                                      "the weapon arm. Either way the shot leaves the real muzzle, for guns and\n"
                                      "projectiles alike, and free-look while aiming is preserved.");
                }
                ImGui::Checkbox("Weapon Aim laser dot (where the bullet hits)", &g_drawBarrelCross);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Red dot projected from the actual weapon muzzle direction through the\n"
                                      "game camera -- marks exactly where the bullet will fly.");
                }
            }
            ImGui::Separator();

            changed |= CheckboxInt("Swap X and A (interact/reload onto the right thumb)", &state.xrSwapXA);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "X is Interact AND Reload, and on Touch controllers X sits on the LEFT --\n"
                    "the same hand as the movement stick. Every reload in a firefight and every\n"
                    "prompt taken while walking costs you the stick for as long as the thumb is\n"
                    "away. This puts them on the right hand, whose stick only turns.\n\n"
                    "The cost is Jump, which moves to the left controller. Menus keep A as\n"
                    "Confirm, vehicles are untouched, and the presses the hand gestures make\n"
                    "are not swapped -- picking up a shard with the grip never jumps.");
            }
            ImGui::Separator();

            changed |= CheckboxInt("Physical body rotation", &state.xrPhysicalBodyRotation);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "OFF (default): classic VR heading -- turn with stick / snap-turn, the head only looks.\n"
                    "ON: the character physically turns to follow your head, through the game own\n"
                    "heading -- so aim, movement and collision follow it. The view stays where you are\n"
                    "looking and recentring is untouched. Vehicles are unaffected.");
            }
            ImGui::Separator();

            changed |= CheckboxInt("Physical crouch (go down in the room)", &state.xrPhysicalCrouch);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Bend your knees and the character crouches; stand up and it stands. It presses the\n"
                    "same button the right stick pushed fully down does, so it behaves exactly like that\n"
                    "gesture -- your Crouch toggle/hold setting included -- and the two can be mixed.\n\n"
                    "OFF by default because it reads your HEAD HEIGHT, which only means crouching if you\n"
                    "recentred standing up. Played seated, leaning toward the desk is the same movement\n"
                    "as a squat.\n\n"
                    "It measures the neck rather than the eyes, so looking down at your feet is not a\n"
                    "crouch. How far you must drop is the slider below.");
            }
            {
                float depth = state.xrPhysicalCrouchDepth > 0.0f ? state.xrPhysicalCrouchDepth : 0.30f;
                if (ImGui::SliderFloat("Crouch depth (m)", &depth, 0.10f, 0.50f, "%.2f")) {
                    state.xrPhysicalCrouchDepth = depth;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How far your head has to drop before the character crouches. Standing up\n"
                        "releases at 60% of it, so the crouch cannot flicker while you breathe.\n\n"
                        "Lower it if you play seated or cannot squat far -- but too low and leaning\n"
                        "forward to read something crouches you. Raise it if a normal lean already\n"
                        "counts. 0.30 is a real bend of the knees; a lean costs about 0.10.");
                }
            }
            {
                const int g = CyberpunkVR_DebugPhysCrouchGate;
                ImGui::Text("head drop %.2f m   switch:%s  context:%s  height:%s  crouched:%s",
                            CyberpunkVR_DebugPhysCrouchDrop,
                            (g & 1) ? "on" : "OFF",
                            (g & 2) ? "ok" : "BLOCKED",
                            (g & 4) ? "ok" : "MISSING",
                            (g & 8) ? "yes" : "no");
                ImGui::Text("body says crouched:%s   game locomotion state:%d%s",
                            CyberpunkVR_DebugPhysCrouchSent ? "yes" : "no",
                            CyberpunkVR_DebugPhysCrouchLoco,
                            (CyberpunkVR_DebugPhysCrouchLoco == 1
                             || CyberpunkVR_DebugPhysCrouchLoco == 12
                             || CyberpunkVR_DebugPhysCrouchLoco == 13) ? "  (crouched)" : "");
                ImGui::Text("crouch toggles sent:%d", CyberpunkVR_DebugPhysCrouchToggles);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Live, so a crouch that will not fire says why. head drop is how far your\n"
                        "neck is below where you recentred; it has to pass the slider above.\n\n"
                        "context BLOCKED = a car, a device screen, the scanner or a placed camera\n"
                        "owns the button. height MISSING = no head height is reaching the gate.");
                }
            }
            ImGui::Separator();

            changed |= CheckboxInt("Jump by flicking both hands up", &state.xrJumpGesture);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Both hands rise together from about the belly to about the shoulders, in\n"
                    "one quick unbroken movement -- the way a person mimes a hop -- and the game\n"
                    "jumps. No thumb leaves a stick, which is what pays for the X/A swap above.\n\n"
                    "It does not fire on the port's other hands-up gestures: a basketball throw\n"
                    "goes past the shoulders, a reload brings one hand to the other instead of\n"
                    "both up in parallel, and a holster reach is one hand with a grip held.\n\n"
                    "The detector runs and logs whether this is on or off, so the thresholds can\n"
                    "be read out of a normal session; the switch only decides whether it presses.");
            }
            {
                float height = state.xrJumpGestureHeight < -0.001f ? state.xrJumpGestureHeight : -0.30f;
                if (ImGui::SliderFloat("Jump hand height (m below eyes)", &height, -0.45f, -0.05f, "%.2f")) {
                    state.xrJumpGestureHeight = height;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "How high both hands have to finish for the flick to count as a jump,\n"
                        "measured from your eyes (negative = below the eyes).\n\n"
                        "Toward 0 = the hands must reach higher = harder to trigger, fewer false\n"
                        "jumps. More negative = the hands need not go as high = easier, but a\n"
                        "half-raise can set it off. -0.30 is about the shoulders for a standing\n"
                        "player; the rise and both-hands-parallel tests still have to pass too.");
                }
            }
            ImGui::Separator();

            changed |= CheckboxInt("Enable VR -> XInput merge", &state.xrXInputHook);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("OR the VR controller state into XInput gamepad 0 every poll.\n"
                                  "Off = the game only sees a physical pad / nothing.");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Weapon holsters (reach + right grip)");
            changed |= CheckboxInt("Immersive holsters", &state.xrImmersiveHolsters);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "ON  - equip is chosen by the VISUAL holster you reach for:\n"
                    "      over the right shoulder = primary weapon (rifle / sniper),\n"
                    "      hip with a katana = melee, hip with a pistol = sidearm.\n"
                    "OFF - ignore visual holsters, fixed slot per zone:\n"
                    "      over-shoulder = EquipmentSlot1, right hip = Slot2, left hip = Slot3.\n"
                    "Reach to the zone and squeeze the RIGHT grip to equip / unequip.");
            }


            ImGui::Separator();
            ImGui::TextUnformatted("Driving -- hands on the wheel");
            changed |= CheckboxInt("Grab the wheel with the grips", &state.xrWheelGrab);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "While DRIVING, bring a hand to where the driving animation holds the\n"
                    "wheel (or the handlebars) and squeeze that grip: the arm is handed back\n"
                    "to the game's own animation -- hand on the wheel, fingers wrapped around\n"
                    "it -- instead of following the controller. Release the grip and it goes\n"
                    "back to your hand.\n\n"
                    "Each hand is independent: hold the wheel with one and keep the other on\n"
                    "a gun. While a hand is at the wheel that grip does nothing else (no\n"
                    "holster equip, no magazine grab).");
            }
            {
                float r = state.xrWheelRadius > 0.0f ? state.xrWheelRadius : 0.28f;
                if (ImGui::SliderFloat("Grab radius (m)", &r, 0.08f, 0.60f, "%.2f")) {
                    state.xrWheelRadius = r;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How close your hand has to be to the animated hand before the\n"
                                      "grip counts as grabbing the wheel. Bigger = easier to catch a\n"
                                      "wheel you cannot see; too big and every grip in a car grabs.");
                }
                float m = state.xrWheelSteerMaxDeg > 0.0f ? state.xrWheelSteerMaxDeg : 90.0f;
                if (ImGui::SliderFloat("Full lock at (deg)", &m, 30.0f, 120.0f, "%.0f")) {
                    state.xrWheelSteerMaxDeg = m;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Wheel sensitivity: the controller tilt that means full stick.\n"
                                      "Two hands = tilt of the line through the controllers, one hand\n"
                                      "= tilt of that controller against the wheel centre.\n"
                                      "90 = hands vertical is full lock (a real wheel, 1:1).\n"
                                      "Lower = the same wrist movement steers more.");
                }
                float d = state.xrWheelSteerDeadDeg >= 0.0f ? state.xrWheelSteerDeadDeg : 1.5f;
                if (ImGui::SliderFloat("Steering deadzone (deg)", &d, 0.0f, 20.0f, "%.1f")) {
                    state.xrWheelSteerDeadDeg = d;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Tilt around centre that steers nothing at all.\n"
                                      "Raise it if the car drifts while you hold the wheel straight;\n"
                                      "every degree here is a degree of dead wheel off centre.\n"
                                      "The full range still ends at 'Full lock at', so widening the\n"
                                      "deadzone does not make the steering jump.");
                }

                changed |= CheckboxInt("Horn -- hand on the wheel hub", &state.xrWheelHorn);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "While DRIVING, put a hand on the MIDDLE of the wheel -- where you would\n"
                        "slap a real horn -- and the car honks for as long as it stays there.\n"
                        "No grip needed; a hand that is GRABBING the wheel never honks.");
                }
                float hr = state.xrWheelHornRadius > 0.0f ? state.xrWheelHornRadius : 0.12f;
                if (ImGui::SliderFloat("Horn hub radius (m)", &hr, 0.04f, 0.30f, "%.2f")) {
                    state.xrWheelHornRadius = hr;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How near the wheel centre the hand counts as on the hub.\n"
                                      "Bigger = easier to find the horn without seeing it; too big\n"
                                      "and it reaches the rim, so every grab honks.");
                }

                ImGui::Spacing();
                changed |= CheckboxInt("Trigger fires the gun while driving", &state.xrVehicleGunTrigger);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Draw a weapon in the driver seat and the RIGHT TRIGGER stops being the\n"
                        "throttle and becomes the gun: drive with the left hand on the wheel and\n"
                        "shoot with the right.\n\n"
                        "The throttle LATCHES at whatever it was when the weapon came out, so the\n"
                        "car keeps rolling, and the LEFT STICK forward/back trims that speed while\n"
                        "you shoot. Holster the weapon and the trigger is the throttle again.\n\n"
                        "While the weapon is out the left stick's forward/back is taken by the\n"
                        "trim: no lean / rock and no autodrive gesture until you holster.");
                }
                float tt = state.xrVehicleThrottleTrim > 0.0f ? state.xrVehicleThrottleTrim : 0.5f;
                if (ImGui::SliderFloat("Throttle trim rate (/s)", &tt, 0.05f, 3.0f, "%.2f")) {
                    state.xrVehicleThrottleTrim = tt;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How much of the throttle's full travel the left stick adds or\n"
                                      "removes per second while a weapon is out.\n"
                                      "0.5 = two seconds held to go from idle to floored.");
                }

                // LIVE STATE, so "it did not grab" and "it steers the wrong way" are both answerable
                // without a log: the armed mask is raised by proximity, the blends by the grab itself,
                // and the angle is what the stick is being driven from.
                {
                    const int mask = static_cast<int>(
                        OpenXRManager::Get().GetSharedSlot(vrshared::kWheelArmedMask));
                    const int horn = cvr::anim::g_wheelHornMask.load(std::memory_order_relaxed);
                    ImGui::Text("driving %d   at wheel  L %d R %d   hold  L %.2f R %.2f",
                                g_isDriving.load(std::memory_order_relaxed) ? 1 : 0,
                                (mask & vrshared::kWheelArmedLeftBit) ? 1 : 0,
                                (mask & vrshared::kWheelArmedRightBit) ? 1 : 0,
                                cvr::anim::g_wheelBlendLeft.load(std::memory_order_relaxed),
                                cvr::anim::g_wheelBlendRight.load(std::memory_order_relaxed));
                    ImGui::Text("steer  %+.1f deg  ->  stick %+.2f     horn  L %d R %d",
                                cvr::anim::g_wheelSteerDeg.load(std::memory_order_relaxed),
                                cvr::anim::g_wheelSteer.load(std::memory_order_relaxed),
                                (horn & vrshared::kWheelArmedLeftBit) ? 1 : 0,
                                (horn & vrshared::kWheelArmedRightBit) ? 1 : 0);
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Locomotion direction");
            const char* moveSrcNames[] = { "Game (camera)", "HMD (head)", "Left hand", "Right hand" };
            int moveSrc = state.xrMovementSource;
            if (moveSrc < 0 || moveSrc > 3) moveSrc = state.xrMovementControl != 0 ? 1 : 0;
            if (ImGui::Combo("Move source", &moveSrc, moveSrcNames, IM_ARRAYSIZE(moveSrcNames))) {
                state.xrMovementSource = moveSrc;
                state.xrMovementControl = moveSrc != 0 ? 1 : 0;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Game     - left stick walks the way the camera faces (vanilla).\n"
                                  "HMD      - left stick walks the way the headset faces.\n"
                                  "Left/Right hand - walks the way the chosen controller points.\n"
                                  "Vehicles always keep game heading.");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Turning (right stick)");
            changed |= CheckboxInt("Snap turn", &state.xrSnapTurn);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Convert the right-stick X axis into discrete snap pulses\n"
                                  "instead of smooth rotation. Helps with motion sickness.");
            }
            if (state.xrSnapTurn != 0) {
                changed |= ImGui::SliderFloat("Snap angle", &state.xrSnapTurnAngleDeg, 10.0f, 90.0f, "%.0f deg");

                // WHY ONE DIRECTION CAN SNAP AND THE OTHER NOT.
                //
                // The snap fires at a deflection measured AFTER the 0.18 deadzone is rescaled out,
                // so the raw axis has to reach fire * (1 - 0.18) + 0.18 -- 0.918 at the default. The
                // decision itself is symmetric (rx > fire / rx < -fire, same constant), so a stick
                // that snaps one way and not the other is a stick that does not travel equally, and
                // this read-out is how you see that instead of guessing at it.
                //
                // Push the stick fully each way and read the peaks: any side below the number in
                // "needs" cannot snap, and the fix is a lower threshold, not a change here.
                const float fire  = CyberpunkVR_SnapTurnStickFire > 0.05f ? CyberpunkVR_SnapTurnStickFire : 0.90f;
                const float needRaw = fire * (1.0f - 0.18f) + 0.18f;
                ImGui::Text("right stick  raw %+.3f   after deadzone %+.3f", 
                            CyberpunkVR_DebugRightStickRaw, CyberpunkVR_DebugRightStickX);
                ImGui::Text("peak raw     right %.3f   left %.3f   needs %.3f",
                            CyberpunkVR_DebugRightStickPeakR, CyberpunkVR_DebugRightStickPeakL, needRaw);
                if (CyberpunkVR_DebugRightStickPeakR > 0.01f && CyberpunkVR_DebugRightStickPeakR < needRaw) {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                                       "RIGHT never reaches the threshold -- lower it below %.2f",
                                       (CyberpunkVR_DebugRightStickPeakR - 0.18f) / (1.0f - 0.18f));
                }
                if (CyberpunkVR_DebugRightStickPeakL > 0.01f && CyberpunkVR_DebugRightStickPeakL < needRaw) {
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                                       "LEFT never reaches the threshold -- lower it below %.2f",
                                       (CyberpunkVR_DebugRightStickPeakL - 0.18f) / (1.0f - 0.18f));
                }
                if (ImGui::Button("Reset stick peaks")) CyberpunkVR_DebugRightStickReset = 1;
                ImGui::SameLine();
                if (ImGui::SliderFloat("Snap fires at", &CyberpunkVR_SnapTurnStickFire, 0.40f, 1.00f, "%.2f")) {
                    // The re-arm has to stay below the fire point or the snap could never re-arm.
                    if (CyberpunkVR_SnapTurnStickRearm >= CyberpunkVR_SnapTurnStickFire) {
                        CyberpunkVR_SnapTurnStickRearm = CyberpunkVR_SnapTurnStickFire * 0.55f;
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How far the right stick must go before a snap fires, measured\n"
                                      "after the deadzone. 0.90 (default) is the hardware stop; lower it\n"
                                      "if one side never reaches it. NOT saved -- it is a live probe, so\n"
                                      "find the number here and tell me to make it the default.");
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Current binding (on foot):");
            ImGui::BulletText("Left stick    - walk / jog | FULL forward, HELD 0.2 s = sprint");
            ImGui::BulletText("Right stick X - turn camera (Y = pitch unless Disable Mouse Y is on)");
            ImGui::BulletText("Right stick FULL up   - DASH / dodge (once per push)");
            ImGui::BulletText("Right stick FULL down - crouch (R3)");
            ImGui::BulletText("Right thumb click - slide release: racks the weapon");
            ImGui::BulletText("Right A       - JUMP");
            ImGui::BulletText("Right B       - drop the magazine (weapon in hand)");
            ImGui::BulletText("                holstered it is the game's B again -- close the phone, back out");
            ImGui::BulletText("Left  X       - reload / interact");
            ImGui::BulletText("Left  Y       - weapon switch");
            ImGui::BulletText("Right trigger - fire | Left trigger - aim / melee block");
            ImGui::BulletText("Right grip    - holster equip / unequip (reach to the holster first)");
            ImGui::BulletText("Left  grip    - grab the magazine during a reload");
            ImGui::BulletText("Left  grip at the LEFT EAR - scanner, TOGGLED: squeeze to open,");
            ImGui::BulletText("                squeeze again to close. The hand is free in between");
            ImGui::BulletText("Left  menu button - pause menu");
            ImGui::Spacing();
            ImGui::TextUnformatted("While the scanner is open, the same hand works it:");
            ImGui::BulletText("Left stick UP / DOWN, to the stop - page the quickhack list");
            ImGui::BulletText("                below the stop the stick still walks; only a full push pages");
            ImGui::BulletText("Left  X       - apply the selected hack (a plain press)");
            ImGui::BulletText("Right trigger - tag the target. It does NOT fire while the scanner is up");
            ImGui::BulletText("Right stick click - change the scanner tab");
            ImGui::BulletText("Left trigger + right stick - zoom in / out");
            ImGui::Spacing();
            ImGui::TextUnformatted("D-Pad, as a chord: HOLD the LEFT stick click, pick with the RIGHT stick");
            ImGui::BulletText("Right stick UP / DOWN / LEFT / RIGHT -> D-Pad UP / DOWN / LEFT / RIGHT");
            ImGui::BulletText("                to the stop, like every other gesture here -- a resting");
            ImGui::BulletText("                thumb must not step a list");
            ImGui::BulletText("Released with no direction = the vanilla left stick click (L3)");
            ImGui::Spacing();
            ImGui::TextUnformatted("In a vehicle (the gestures above do not apply):");
            ImGui::BulletText("The camera is HELD IN FIRST PERSON: the perspective toggle does nothing,");
            ImGui::BulletText("                and a car entered in third person is put back");
            ImGui::BulletText("HOLD B        - get out. A tap does nothing, so no stray press ejects you");
            ImGui::BulletText("X             - the horn, as in flat (a hand on the wheel hub honks too)");
            ImGui::BulletText("LEFT stick click + RIGHT grip - radio on / off (RB)");
            ImGui::BulletText("RIGHT stick click - AUTODRIVE (the look-back camera is a head turn in VR)");
            ImGui::BulletText("                    moved off the LEFT click, which was too easy to hit");
            ImGui::BulletText("Left trigger  - brake | Right trigger - throttle (see the Vehicle section)");

            ImGui::TextWrapped("Buttons follow each runtime's interaction profile (Touch / Index / "
                               "Vive / WMR). Customize the actual key bindings in the game's "
                               "in-engine \"Key Bindings -> Controller\" menu.");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stereo")) {
            DrawStereoControls();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("DLSS5-NR")) {
            DrawDlss5NrControls();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("FSR3")) {
            DrawFsr3Controls();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("VRIK")) {
            DrawVRHandsControls();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    return changed;
}

}  // namespace overlay
using namespace overlay;
