#pragma once
#include <d3d12.h>
#include <cstdint>
namespace cvr::framegen {
// Observe the depth and motion resources of the native TAA node and of the temporal upscaler nodes
// (FSR 2, FSR 3.0, FSR 3.1+, XeSS) while they own them. See NativeInputs.cpp.
void ObserveNativeInput(uint32_t logical,uint32_t physical);
// The node dispatcher's work context, before a consumer node runs: records that view's camera.
void ObserveNativeView(const void* workContext);
// TAA and FSR 2 record engine passes: captured at the node's next draw or dispatch.
void CaptureNativeInputs(ID3D12GraphicsCommandList*);
// FSR 3 and XeSS are libraries: captured when the library is called, before it touches the inputs.
void CaptureUpscalerInputs(ID3D12GraphicsCommandList*);
}
