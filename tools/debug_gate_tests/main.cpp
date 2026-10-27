#include "Utils/DebugGate.hpp"
#include <cstdint>
#include <stdexcept>
#include <iostream>

// Storage-only substitutes for unrelated probes; run the production gate table.
extern "C" {
__declspec(dllexport) int CyberpunkVR_XrRateLog=0;
__declspec(dllexport) int CyberpunkVR_XrDeepDiag=0;
__declspec(dllexport) int CyberpunkVR_VrikRateLog=0;
__declspec(dllexport) int32_t CyberpunkVR_TemporalScan=0;
__declspec(dllexport) int32_t CyberpunkVR_WideCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_CascFitProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_BlockDiffSize=0;
__declspec(dllexport) int32_t CyberpunkVR_StageProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_LumaProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CapCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_NodeCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_DrawCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_DispatchCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_IndirectCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_LightCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_CullCountProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_TileProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_ExpoProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_PsoProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_RtMapProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CbvProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CamCbProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CascSampleProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CaptureMarkers=0;
__declspec(dllexport) int32_t CyberpunkVR_CmdListHunt=0;
__declspec(dllexport) int32_t CyberpunkVR_CascSideCensus=0;
__declspec(dllexport) int32_t CyberpunkVR_ViewRectProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_EnvPtrProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_HudNodeProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_VolumeNodeProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_GradeCbProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_GradeUpProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_GradingProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_SkyProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_CloudLightProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_SightAxisProbe=0;
__declspec(dllexport) int32_t CyberpunkVR_SightPsDump=0;
__declspec(dllexport) int32_t CyberpunkVR_VisionDump=0;
__declspec(dllexport) int32_t CyberpunkVR_VisionMap=0;
__declspec(dllexport) int32_t CyberpunkVR_ViewDataDiff=0;
__declspec(dllexport) int32_t CyberpunkVR_DebugRtvPickLog=0;
__declspec(dllexport) uint32_t CyberpunkVR_LodThreshOverrideEnable=0;
}
volatile int g_verboseLog=0;
static int launcherDebug=0;
extern "C" int GetLauncherDebug() {return launcherDebug;}
extern "C" void ApplyLauncherDebugGate();
void Log(const char*,...) {}
void Check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
int main() try {
    Check(!cvr::RuntimeDiagnosticsEnabled(),"diagnostics must start disabled");
    launcherDebug=1;ApplyLauncherDebugGate();
    Check(cvr::RuntimeDiagnosticsEnabled() && g_verboseLog,"DEBUG on did not enable runtime diagnostics");
    Check(CyberpunkVR_WideCensus && CyberpunkVR_TemporalScan && CyberpunkVR_CascFitProbe,"upload/shadow probes missing from the gate");
    Check(CyberpunkVR_XrRateLog && CyberpunkVR_XrDeepDiag && CyberpunkVR_VrikRateLog,"XR/VRIK reports missing from the gate");
    Check(CyberpunkVR_BlockDiffSize==384 && CyberpunkVR_ViewDataDiff==2 && CyberpunkVR_DebugRtvPickLog==48,"non-boolean probe settings lost");
    launcherDebug=0;ApplyLauncherDebugGate();
    Check(!cvr::RuntimeDiagnosticsEnabled() && !g_verboseLog,"DEBUG off left runtime diagnostics running");
    Check(!CyberpunkVR_WideCensus && !CyberpunkVR_TemporalScan && !CyberpunkVR_CascFitProbe,"DEBUG off left upload/shadow scans running");
    Check(!CyberpunkVR_XrRateLog && !CyberpunkVR_XrDeepDiag && !CyberpunkVR_VrikRateLog,"DEBUG off left frame reports running");
    CyberpunkVR_RuntimeDiagnostics=1;
    Check(cvr::RuntimeDiagnosticsEnabled() && !CyberpunkVR_WideCensus && !CyberpunkVR_XrDeepDiag,"focused diagnostics unnecessarily enabled heavy probes");
    std::cout<<"PASS launcher_debug_gate\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
