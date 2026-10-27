#pragma once

// One temporal-upscaler history per eye for the upscalers that keep a single engine-wide context:
// FSR 3.0 (ffx_fsr3_x64.dll), FSR 3.1 / FSR 4 (amd_fidelityfx_dx12.dll) and XeSS (libxess.dll).
// src/Stereo/UpscalerViews.cpp has the reasoning.
namespace cvr::stereo::upscaler_views {
// Hooks the libraries that are already loaded. Idempotent; called with the early engine detours.
void Install();
}
