#pragma once

// Diagnostic snapshots/counters only. Never gate frame identities, ownership,
// fences, pose publication or the user-enabled FPS overlay with this switch.
extern "C" __declspec(dllexport) extern int CyberpunkVR_RuntimeDiagnostics;

namespace cvr {
inline bool RuntimeDiagnosticsEnabled() { return CyberpunkVR_RuntimeDiagnostics != 0; }
}

// A statement wrapper keeps existing if/else control flow intact and does not
// evaluate diagnostic-only arguments (clock reads, scans, copies) while off.
#define CVR_DIAGNOSTIC(...) \
    do { if (::cvr::RuntimeDiagnosticsEnabled()) { __VA_ARGS__; } } while (false)
