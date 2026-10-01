#pragma once
#include <array>
#include <cstdint>
struct ID3D12Device;
namespace cvr::framegen {
struct HardwareStatistics {
    uint64_t sequence{};
    double cpuUsage=-1,gpuUsage=-1,gpuTemperature=-1,coreMaximum=-1,sampleMs{};
    uint64_t ramUsed{},ramTotal{},appWorkingSet{},appCommit{};
    uint64_t gpuMemoryUsed{},gpuMemoryTotal{},gpuProcessUsed{},gpuProcessBudget{};
    bool gpuMemoryValid{},gpuProcessMemoryValid{};
    uint32_t coreCount{};
    std::array<float,128> cores{};
    char gpuName[96]{},cpuName[96]{};
};
void ConfigureHardwareTelemetry(ID3D12Device*);
void WakeHardwareTelemetry();
void StopHardwareTelemetry();
HardwareStatistics GetHardwareStatistics();
}
