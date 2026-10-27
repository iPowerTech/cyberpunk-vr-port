#pragma once
#include "Framegen/Generator.hpp"

namespace cvr::framegen {
// Diagnostic: with CyberpunkVR_FramegenCapture set to 1, the next three consecutive real frames are
// written to <game>\bin\x64\framegen-capture\<time>\ -- both eyes' colour, the engine depth and motion
// vectors, the engine camera and the eye cameras -- so every generator can be replayed offline
// against a real frame in between (tools/framegen_tests: replay). The export reads 2 when written,
// -1 on failure. Costs nothing while disarmed.
void CaptureFrame(ID3D12Device*,ID3D12CommandQueue*,ID3D12Resource* const colors[2],
    const std::shared_ptr<const Inputs> (&inputs)[2],const EyePair (&cameras)[2]);
}
