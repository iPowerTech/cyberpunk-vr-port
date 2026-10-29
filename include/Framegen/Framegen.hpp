#pragma once
#include <atomic>
#include <cstdint>
#include "Framegen/HardwareTelemetry.hpp"
#include "Framegen/History.hpp"

namespace cvr::framegen {
struct Settings {
    bool overlay=false;
    int overlayCorner=1;
    int overlayLayout=0,overlayFollow=1,historySeconds=30;
    float overlayCone=60,overlayScale=1,overlayDistance=1.25f,overlayX=0,overlayY=0;
};
struct Statistics {
    double realFps{},generatedFps{},outputFps{},repeatFps{};
    double cpuMs{},cpuPeakMs{},gpuMs{},gpuPeakMs{},generationMs{};
    double averageFps{},minimumFps{},maximumFps{},low1Fps{},low01Fps{},cpuAverageMs{},gpuAverageMs{},cpuP99Ms{},cpuP999Ms{},gpuP99Ms{},gpuP999Ms{};
    double elapsedSeconds{},budgetMs{},repeatPercent{};
    uint32_t historySamples{};
    std::array<float,GraphPoints> cpuGraph{},gpuGraph{};
    HardwareStatistics hardware;
    uint64_t realFrames{},generatedFrames{},repeatedFrames{},skipped{},vramBytes{};
    uint64_t realSubmissions{};
    uint64_t inputFrames[2]{};
    uint32_t renderWidth[2]{},renderHeight[2]{};
    bool active{},gpuValid{},inputsReady{};
    char status[160]="Disabled";
};
Settings GetSettings();
// Depth submission uses the camera-labelled depth snapshots; FSR3 (Framegen/Fsr3.hpp) the motion
// vectors too.
bool DepthCaptureEnabled();
bool MotionCaptureEnabled();
bool CaptureInputsEnabled();
void SetDepthCaptureEnabled(bool enabled);
void SetMotionCaptureEnabled(bool enabled);
bool MetricsEnabled();
void SetSettings(Settings value);
Statistics GetStatistics();
void ResetStatistics();
void OnRealFrame(uint64_t serial,double nowMs);
void OnSubmitted(uint64_t serial,bool generated,bool repeat,double nowMs);
void OnGpuFrame(double milliseconds);
void OnGeneration(double milliseconds);
void OnInput(unsigned eye,uint32_t width,uint32_t height);
void OnSkip();
void SetVram(uint64_t bytes);
double NowMs();
void DisplayPeriod(int64_t nanoseconds);
bool ParseSetting(const char*,Settings&);
void WriteSettings(void* file,const Settings&);
}
