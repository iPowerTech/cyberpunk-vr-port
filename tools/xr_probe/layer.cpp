// XR_APILAYER_CPVR_probe - records everything an application asks the OpenXR runtime for and
// everything it submits back.
//
// WHY A LAYER AND NOT A HOOK INTO THE MOD.
//
// Observe the application's OpenXR calls: reported FOV and resolution, requested allocations,
// per-eye poses and FOV, and swapchain rectangles. The versioned OpenXR ABI lets the same probe
// work across application builds without relying on private addresses or pattern scans.
//
// Applications resolve entry points through xrGetInstanceProcAddr, so a layer sees every call, with no
// risk of missing one that was inlined or reached through a vtable.
//
// WHAT IT COSTS. Nothing measurable at the default rate. Per-frame calls (xrLocateViews,
// xrWaitFrame, xrEndFrame) are sampled: the first XRPROBE_FRAMES_FULL frames go out in full so the
// startup transient is complete, then one frame in every XRPROBE_FRAME_STRIDE. Static facts
// (system properties, view configurations, swapchain creates) are always logged in full, and any
// change in a value that is supposed to be static is logged even between samples -- a runtime that
// silently changes the reported FOV mid-session is exactly the kind of thing this is for.
//
// OUTPUT. Two files per run, in %LOCALAPPDATA%\xrprobe (override with XRPROBE_DIR):
//   xrprobe-<exe>-<pid>.jsonl   one JSON object per event, for machine reading
//   xrprobe-<exe>-<pid>.txt     a human summary, rewritten on every static change and at exit
//
// ENVIRONMENT
//   XRPROBE_DIR           output directory                       (default %LOCALAPPDATA%\xrprobe)
//   XRPROBE_FRAME_STRIDE  sample 1 frame in N after warm-up      (default 90)
//   XRPROBE_FRAMES_FULL   log the first N frames in full         (default 120)
//   XRPROBE_ALL           1 = log every frame, no sampling       (default 0)

// XR_USE_PLATFORM_WIN32 / WIN32_LEAN_AND_MEAN / NOMINMAX come from the target definitions.
#include <windows.h>
#include <share.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char* kLayerName = "XR_APILAYER_CPVR_probe";

// ── math ─────────────────────────────────────────────────────────────────────────────────────
constexpr float kRad2Deg = 57.2957795131f;

struct Euler { float yawDeg, pitchDeg, rollDeg; };

// Quaternion -> yaw (about Y), pitch (about X), roll (about Z), in the OpenXR right-handed,
// Y-up, -Z-forward convention.
Euler QuatToEuler(const XrQuaternionf& q) {
    const float sinp = 2.0f * (q.w * q.x - q.y * q.z);
    const float pitch = (sinp >= 1.0f) ? 1.57079633f
                      : (sinp <= -1.0f) ? -1.57079633f
                      : std::asin(sinp);
    const float yaw  = std::atan2(2.0f * (q.w * q.y + q.z * q.x),
                                  1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    const float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                                  1.0f - 2.0f * (q.z * q.z + q.x * q.x));
    return { yaw * kRad2Deg, pitch * kRad2Deg, roll * kRad2Deg };
}

float Dist3(const XrVector3f& a, const XrVector3f& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ── configuration ────────────────────────────────────────────────────────────────────────────
int EnvInt(const char* name, int fallback) {
    char buf[64] = {};
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return fallback;
    return std::atoi(buf);
}

std::string EnvStr(const char* name) {
    char buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return {};
    return std::string(buf);
}

struct Config {
    int  frameStride = 90;
    int  framesFull  = 120;
    bool all         = false;
    std::string dir;
} g_cfg;

// ── output ───────────────────────────────────────────────────────────────────────────────────
std::mutex  g_ioMutex;
FILE*       g_jsonl   = nullptr;
std::string g_txtPath;
std::string g_exeName = "app";
uint64_t    g_eventNo = 0;

std::string JsonEscape(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p) {
        switch (*p) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(*p) < 0x20) {
                char esc[8];
                std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned char>(*p));
                out += esc;
            } else {
                out += *p;
            }
        }
    }
    return out;
}

// One JSON object per line. Caller supplies the body without the outer braces; the event number,
// timestamp and thread id are prepended here so every record is self-locating.
void Emit(const char* type, const std::string& body) {
    std::lock_guard<std::mutex> lock(g_ioMutex);
    if (!g_jsonl) return;
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    std::fprintf(g_jsonl, "{\"n\":%llu,\"qpc\":%lld,\"tid\":%lu,\"ev\":\"%s\"%s%s}\n",
                 static_cast<unsigned long long>(++g_eventNo),
                 static_cast<long long>(qpc.QuadPart),
                 GetCurrentThreadId(),
                 type,
                 body.empty() ? "" : ",",
                 body.c_str());
    // Flushed every record: a game that crashes mid-session is a normal outcome here, and a
    // truncated last line loses exactly the frame we would most want to see.
    std::fflush(g_jsonl);
}

std::string Fmt(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return {};
    return std::string(buf, (n < static_cast<int>(sizeof(buf))) ? n : sizeof(buf) - 1);
}

std::string FmtPose(const XrPosef& p) {
    const Euler e = QuatToEuler(p.orientation);
    return Fmt("{\"pos\":[%.6f,%.6f,%.6f],\"quat\":[%.6f,%.6f,%.6f,%.6f],"
               "\"yawDeg\":%.4f,\"pitchDeg\":%.4f,\"rollDeg\":%.4f}",
               p.position.x, p.position.y, p.position.z,
               p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w,
               e.yawDeg, e.pitchDeg, e.rollDeg);
}

// FOV is emitted three ways on purpose. Radians are the ABI truth; degrees are what a human reads;
// tangents are what every projection matrix is actually built from.
std::string FmtFov(const XrFovf& f) {
    return Fmt("{\"radL\":%.6f,\"radR\":%.6f,\"radU\":%.6f,\"radD\":%.6f,"
               "\"degL\":%.4f,\"degR\":%.4f,\"degU\":%.4f,\"degD\":%.4f,"
               "\"tanL\":%.6f,\"tanR\":%.6f,\"tanU\":%.6f,\"tanD\":%.6f,"
               "\"hfovDeg\":%.4f,\"vfovDeg\":%.4f,"
               "\"asymHDeg\":%.4f,\"asymVDeg\":%.4f}",
               f.angleLeft, f.angleRight, f.angleUp, f.angleDown,
               f.angleLeft * kRad2Deg, f.angleRight * kRad2Deg,
               f.angleUp * kRad2Deg, f.angleDown * kRad2Deg,
               std::tan(f.angleLeft), std::tan(f.angleRight),
               std::tan(f.angleUp), std::tan(f.angleDown),
               (f.angleRight - f.angleLeft) * kRad2Deg,
               (f.angleUp - f.angleDown) * kRad2Deg,
               // Zero on a symmetric frustum. Non-zero is the off-axis term that a canted panel
               // produces and that "parallel projections" exists to remove.
               (f.angleRight + f.angleLeft) * kRad2Deg,
               (f.angleUp + f.angleDown) * kRad2Deg);
}

// ── accumulated session facts, for the human summary ─────────────────────────────────────────
struct ViewConfigView {
    uint32_t recW = 0, recH = 0, maxW = 0, maxH = 0, recSamples = 0, maxSamples = 0;
};

struct SwapchainFact {
    uint64_t handle = 0;
    int64_t  format = 0;
    uint32_t w = 0, h = 0, arraySize = 0, mipCount = 0, sampleCount = 0, faceCount = 0;
    uint64_t usage = 0;
};

struct Summary {
    std::mutex mutex;

    std::string appName, engineName, runtimeName, systemName;
    uint32_t    appVersion = 0, engineVersion = 0;
    uint64_t    runtimeVersion = 0;
    uint32_t    vendorId = 0;
    uint32_t    maxSwapchainW = 0, maxSwapchainH = 0, maxLayerCount = 0;
    bool        orientationTracking = false, positionTracking = false;
    std::vector<std::string> extensions;
    std::vector<ViewConfigView> viewConfigViews;
    std::vector<SwapchainFact>  swapchains;
    std::vector<std::string>    referenceSpaces;
    std::string blendMode;

    bool     haveViews = false;
    XrPosef  eyePose[2]{};
    XrFovf   eyeFov[2]{};
    float    ipd = 0.0f;

    bool     haveSubmit = false;
    XrFovf   submitFov[2]{};
    XrPosef  submitPose[2]{};
    int32_t  submitRect[2][4]{};       // x, y, w, h
    uint32_t submitArrayIndex[2]{};
    uint64_t submitSwapchain[2]{};
    uint32_t projLayerCount = 0, quadLayerCount = 0, otherLayerCount = 0;
    bool     submitDepth = false;

    uint64_t frames = 0;
    double   periodMsFromRuntime = 0.0;
    double   periodMsMeasured = 0.0;
    XrTime   lastDisplayTime = 0;
} g_sum;

const char* ViewConfigName(XrViewConfigurationType t) {
    switch (t) {
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO:                    return "PRIMARY_MONO";
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO:                  return "PRIMARY_STEREO";
    case XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO:              return "PRIMARY_QUAD_VARJO";
    case XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT:
                                                                     return "SECONDARY_MONO_FPO";
    default:                                                         return "UNKNOWN";
    }
}

const char* RefSpaceName(XrReferenceSpaceType t) {
    switch (t) {
    case XR_REFERENCE_SPACE_TYPE_VIEW:            return "VIEW";
    case XR_REFERENCE_SPACE_TYPE_LOCAL:           return "LOCAL";
    case XR_REFERENCE_SPACE_TYPE_STAGE:           return "STAGE";
    case XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT:  return "UNBOUNDED_MSFT";
    case XR_REFERENCE_SPACE_TYPE_COMBINED_EYE_VARJO: return "COMBINED_EYE_VARJO";
    default:                                      return "UNKNOWN";
    }
}

const char* BlendModeName(XrEnvironmentBlendMode m) {
    switch (m) {
    case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:      return "OPAQUE";
    case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:    return "ADDITIVE";
    case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND: return "ALPHA_BLEND";
    default:                                    return "UNKNOWN";
    }
}

// The graphics-binding struct types, spelled numerically so the layer does not have to pull in
// d3d11.h / d3d12.h / vulkan.h just to name what it saw.
const char* GraphicsBindingName(XrStructureType t) {
    switch (static_cast<uint64_t>(t)) {
    case 1000023000ull: return "OPENGL_WIN32";
    case 1000025000ull: return "VULKAN";
    case 1000027000ull: return "D3D11";
    case 1000028000ull: return "D3D12";
    default:            return "OTHER";
    }
}

// DXGI formats are the only ones we are likely to meet here; anything else is printed numerically.
const char* DxgiFormatName(int64_t f) {
    switch (f) {
    case 2:  return "R32G32B32A32_FLOAT";
    case 10: return "R16G16B16A16_FLOAT";
    case 24: return "R10G10B10A2_UNORM";
    case 28: return "R8G8B8A8_UNORM";
    case 29: return "R8G8B8A8_UNORM_SRGB";
    case 87: return "B8G8R8A8_UNORM";
    case 91: return "B8G8R8A8_UNORM_SRGB";
    case 40: return "D32_FLOAT";
    case 45: return "D24_UNORM_S8_UINT";
    case 20: return "D32_FLOAT_S8X24_UINT";
    default: return nullptr;
    }
}

// ── the human summary ────────────────────────────────────────────────────────────────────────
// Rewritten in full whenever a static fact changes and once more at instance destruction. Cheap
// (a few kilobytes) and it means the file is complete even if the process is killed.
void WriteSummary() {
    std::lock_guard<std::mutex> lock(g_sum.mutex);
    if (g_txtPath.empty()) return;

    FILE* f = _fsopen(g_txtPath.c_str(), "w", _SH_DENYNO);
    if (!f) return;

    std::fprintf(f, "OpenXR probe summary\n");
    std::fprintf(f, "====================\n\n");
    std::fprintf(f, "application     : %s (v%u)\n", g_sum.appName.c_str(), g_sum.appVersion);
    std::fprintf(f, "engine          : %s (v%u)\n", g_sum.engineName.c_str(), g_sum.engineVersion);
    std::fprintf(f, "runtime         : %s (v%llu.%llu.%llu)\n", g_sum.runtimeName.c_str(),
                 static_cast<unsigned long long>(XR_VERSION_MAJOR(g_sum.runtimeVersion)),
                 static_cast<unsigned long long>(XR_VERSION_MINOR(g_sum.runtimeVersion)),
                 static_cast<unsigned long long>(XR_VERSION_PATCH(g_sum.runtimeVersion)));
    std::fprintf(f, "system          : %s (vendorId 0x%08x)\n",
                 g_sum.systemName.c_str(), g_sum.vendorId);
    std::fprintf(f, "tracking        : orientation=%d position=%d\n",
                 g_sum.orientationTracking ? 1 : 0, g_sum.positionTracking ? 1 : 0);
    std::fprintf(f, "max swapchain   : %ux%u, max layers %u\n",
                 g_sum.maxSwapchainW, g_sum.maxSwapchainH, g_sum.maxLayerCount);
    std::fprintf(f, "blend mode      : %s\n", g_sum.blendMode.c_str());
    std::fprintf(f, "frames observed : %llu\n", static_cast<unsigned long long>(g_sum.frames));
    if (g_sum.periodMsFromRuntime > 0.0) {
        std::fprintf(f, "display period  : %.3f ms reported (%.1f Hz)",
                     g_sum.periodMsFromRuntime, 1000.0 / g_sum.periodMsFromRuntime);
        if (g_sum.periodMsMeasured > 0.0) {
            std::fprintf(f, ", %.3f ms measured between submitted frames (%.1f Hz)",
                         g_sum.periodMsMeasured, 1000.0 / g_sum.periodMsMeasured);
        }
        std::fprintf(f, "\n");
    }

    std::fprintf(f, "\nExtensions requested by the application (%zu)\n", g_sum.extensions.size());
    for (const auto& e : g_sum.extensions) std::fprintf(f, "  %s\n", e.c_str());

    std::fprintf(f, "\nReference spaces created\n");
    for (const auto& s : g_sum.referenceSpaces) std::fprintf(f, "  %s\n", s.c_str());

    std::fprintf(f, "\nView configuration -- what the headset asks for\n");
    for (size_t i = 0; i < g_sum.viewConfigViews.size(); ++i) {
        const ViewConfigView& v = g_sum.viewConfigViews[i];
        std::fprintf(f, "  eye %zu  recommended %ux%u (x%u samples)   max %ux%u (x%u samples)\n",
                     i, v.recW, v.recH, v.recSamples, v.maxW, v.maxH, v.maxSamples);
    }

    std::fprintf(f, "\nSwapchains the application actually created (%zu)\n", g_sum.swapchains.size());
    for (const SwapchainFact& s : g_sum.swapchains) {
        const char* fn = DxgiFormatName(s.format);
        std::fprintf(f, "  %ux%u  array %u  mips %u  samples %u  format %lld%s%s%s  usage 0x%llx\n",
                     s.w, s.h, s.arraySize, s.mipCount, s.sampleCount,
                     static_cast<long long>(s.format),
                     fn ? " (" : "", fn ? fn : "", fn ? ")" : "",
                     static_cast<unsigned long long>(s.usage));
    }
    // The ratio between what the application allocated and what the runtime recommended is the
    // supersampling factor it chose -- the single number most worth copying.
    if (!g_sum.viewConfigViews.empty() && !g_sum.swapchains.empty()) {
        const ViewConfigView& v = g_sum.viewConfigViews[0];
        const SwapchainFact* biggest = &g_sum.swapchains[0];
        for (const SwapchainFact& s : g_sum.swapchains) {
            if (static_cast<uint64_t>(s.w) * s.h > static_cast<uint64_t>(biggest->w) * biggest->h) {
                biggest = &s;
            }
        }
        if (v.recW && v.recH) {
            std::fprintf(f, "  largest / recommended = %.3fx horizontally, %.3fx vertically\n",
                         static_cast<double>(biggest->w) / v.recW,
                         static_cast<double>(biggest->h) / v.recH);
        }
    }

    if (g_sum.haveViews) {
        std::fprintf(f, "\nEye geometry from xrLocateViews\n");
        for (int e = 0; e < 2; ++e) {
            const XrFovf& v = g_sum.eyeFov[e];
            const Euler eu = QuatToEuler(g_sum.eyePose[e].orientation);
            std::fprintf(f, "  %s eye\n", e == 0 ? "left " : "right");
            std::fprintf(f, "    fov rad   L %+.6f  R %+.6f  U %+.6f  D %+.6f\n",
                         v.angleLeft, v.angleRight, v.angleUp, v.angleDown);
            std::fprintf(f, "    fov deg   L %+.3f  R %+.3f  U %+.3f  D %+.3f   (H %.3f, V %.3f)\n",
                         v.angleLeft * kRad2Deg, v.angleRight * kRad2Deg,
                         v.angleUp * kRad2Deg, v.angleDown * kRad2Deg,
                         (v.angleRight - v.angleLeft) * kRad2Deg,
                         (v.angleUp - v.angleDown) * kRad2Deg);
            std::fprintf(f, "    fov tan   L %.6f  R %.6f  U %.6f  D %.6f\n",
                         std::tan(v.angleLeft), std::tan(v.angleRight),
                         std::tan(v.angleUp), std::tan(v.angleDown));
            std::fprintf(f, "    asymmetry H %+.4f deg   V %+.4f deg\n",
                         (v.angleRight + v.angleLeft) * kRad2Deg,
                         (v.angleUp + v.angleDown) * kRad2Deg);
            std::fprintf(f, "    pose      pos (%+.4f, %+.4f, %+.4f)   yaw %+.3f  pitch %+.3f  roll %+.3f deg\n",
                         g_sum.eyePose[e].position.x, g_sum.eyePose[e].position.y,
                         g_sum.eyePose[e].position.z, eu.yawDeg, eu.pitchDeg, eu.rollDeg);
        }
        std::fprintf(f, "  IPD (eye separation) = %.4f m = %.1f mm\n", g_sum.ipd, g_sum.ipd * 1000.0f);

        // CANT IS REPORTED TWO DIFFERENT WAYS BY DIFFERENT RUNTIMES, AND BOTH HAVE TO BE READ.
        //
        // A physically canted panel can be described either by rotating the eye pose and keeping
        // the frustum symmetric, or by keeping the pose straight ahead and skewing the frustum.
        // They are geometrically equivalent and runtimes disagree about which to use -- the Meta
        // XR Simulator's Quest 3 profile, for instance, reports zero eye yaw and a 14-degree
        // horizontal FOV asymmetry, so anything looking only at the pose concludes "symmetric"
        // and gets the projection wrong by seven degrees per eye.
        //
        // pose cant    = the eye's own yaw.
        // frustum cant = half the horizontal asymmetry. For a frustum rotated by c, angleLeft and
        //                angleRight shift in opposite directions, so angleRight + angleLeft = -2c.
        const Euler l = QuatToEuler(g_sum.eyePose[0].orientation);
        const Euler r = QuatToEuler(g_sum.eyePose[1].orientation);
        // The DIFFERENCE between the two eye yaws, halved -- not the average of their magnitudes.
        // Both eyes carry the head's own yaw, so averaging magnitudes reports wherever the player
        // happened to be looking as if it were cant: a Pimax log came back claiming 33.3 deg of
        // pose cant with the two orientations bit-identical, which sent a whole investigation at
        // a mechanism that was not there. Cant is what the eyes disagree about, nothing else.
        float yawDelta = l.yawDeg - r.yawDeg;
        while (yawDelta > 180.0f) yawDelta -= 360.0f;
        while (yawDelta < -180.0f) yawDelta += 360.0f;
        const float poseCant = 0.5f * std::fabs(yawDelta);
        const float asymL = (g_sum.eyeFov[0].angleRight + g_sum.eyeFov[0].angleLeft) * kRad2Deg;
        const float asymR = (g_sum.eyeFov[1].angleRight + g_sum.eyeFov[1].angleLeft) * kRad2Deg;
        const float frustumCant = 0.25f * (std::fabs(asymL) + std::fabs(asymR));
        const float totalCant = poseCant + frustumCant;

        std::fprintf(f, "  cant, as eye yaw       = %.3f deg\n", poseCant);
        std::fprintf(f, "  cant, as FOV asymmetry = %.3f deg per eye  (half of %.3f deg H asymmetry)\n",
                     frustumCant, 0.5f * (std::fabs(asymL) + std::fabs(asymR)));
        std::fprintf(f, "  -> panels are %s (%.3f deg total)\n",
                     (totalCant > 0.2f) ? "CANTED" : "parallel/symmetric", totalCant);
        if (frustumCant > 0.2f && poseCant <= 0.2f) {
            std::fprintf(f, "     NOTE: this runtime expresses the cant entirely in the frustum, not in the\n");
            std::fprintf(f, "     eye pose. Code that detects canting from the eye orientation alone will\n");
            std::fprintf(f, "     miss it and build a symmetric projection that is %.1f deg off per eye.\n",
                         frustumCant);
        } else if (poseCant > 0.2f && frustumCant <= 0.2f) {
            std::fprintf(f, "     NOTE: the cant is entirely in the eye pose; the frustum is already on-axis.\n");
        }
    }

    if (g_sum.haveSubmit) {
        std::fprintf(f, "\nWhat the application submits (last sampled xrEndFrame)\n");
        std::fprintf(f, "  layers: %u projection, %u quad, %u other%s\n",
                     g_sum.projLayerCount, g_sum.quadLayerCount, g_sum.otherLayerCount,
                     g_sum.submitDepth ? ", with depth" : "");
        for (int e = 0; e < 2; ++e) {
            const XrFovf& v = g_sum.submitFov[e];
            std::fprintf(f, "  %s eye  rect x%d y%d %dx%d  arrayIndex %u  swapchain 0x%llx\n",
                         e == 0 ? "left " : "right",
                         g_sum.submitRect[e][0], g_sum.submitRect[e][1],
                         g_sum.submitRect[e][2], g_sum.submitRect[e][3],
                         g_sum.submitArrayIndex[e],
                         static_cast<unsigned long long>(g_sum.submitSwapchain[e]));
            std::fprintf(f, "          fov deg L %+.3f R %+.3f U %+.3f D %+.3f   asym H %+.4f V %+.4f\n",
                         v.angleLeft * kRad2Deg, v.angleRight * kRad2Deg,
                         v.angleUp * kRad2Deg, v.angleDown * kRad2Deg,
                         (v.angleRight + v.angleLeft) * kRad2Deg,
                         (v.angleUp + v.angleDown) * kRad2Deg);
            const Euler eu = QuatToEuler(g_sum.submitPose[e].orientation);
            std::fprintf(f, "          pose (%+.4f, %+.4f, %+.4f)  yaw %+.3f pitch %+.3f roll %+.3f\n",
                         g_sum.submitPose[e].position.x, g_sum.submitPose[e].position.y,
                         g_sum.submitPose[e].position.z, eu.yawDeg, eu.pitchDeg, eu.rollDeg);
        }
        // The interesting comparison for a canted headset: the runtime hands out asymmetric FOV,
        // and a mod doing "parallel projections" submits a symmetric one plus a rotated pose.
        if (g_sum.haveViews) {
            const float locAsym = (g_sum.eyeFov[0].angleRight + g_sum.eyeFov[0].angleLeft) * kRad2Deg;
            const float subAsym = (g_sum.submitFov[0].angleRight + g_sum.submitFov[0].angleLeft) * kRad2Deg;
            std::fprintf(f, "  left-eye H asymmetry: runtime reported %+.4f deg, application submitted %+.4f deg%s\n",
                         locAsym, subAsym,
                         (std::fabs(locAsym) > 0.05f && std::fabs(subAsym) < 0.05f)
                             ? "  <- cant removed, projection made symmetric" : "");
        }
    }

    std::fprintf(f, "\nFull event stream: the .jsonl file next to this one.\n");
    std::fclose(f);
}

// ── per-instance dispatch table ──────────────────────────────────────────────────────────────
struct Dispatch {
    PFN_xrGetInstanceProcAddr             GetInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance                 DestroyInstance = nullptr;
    PFN_xrGetInstanceProperties           GetInstanceProperties = nullptr;
    PFN_xrGetSystemProperties             GetSystemProperties = nullptr;
    PFN_xrEnumerateViewConfigurationViews EnumerateViewConfigurationViews = nullptr;
    PFN_xrGetViewConfigurationProperties  GetViewConfigurationProperties = nullptr;
    PFN_xrEnumerateEnvironmentBlendModes  EnumerateEnvironmentBlendModes = nullptr;
    PFN_xrCreateSession                   CreateSession = nullptr;
    PFN_xrBeginSession                    BeginSession = nullptr;
    PFN_xrCreateReferenceSpace            CreateReferenceSpace = nullptr;
    PFN_xrEnumerateSwapchainFormats       EnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain                 CreateSwapchain = nullptr;
    PFN_xrWaitFrame                       WaitFrame = nullptr;
    PFN_xrLocateViews                     LocateViews = nullptr;
    PFN_xrLocateSpace                     LocateSpace = nullptr;
    PFN_xrEndFrame                        EndFrame = nullptr;
};

std::mutex                                  g_instMutex;
std::unordered_map<XrInstance, Dispatch>    g_instances;

// The dispatch of the most recently created instance. Session-level entry points (xrWaitFrame,
// xrLocateViews, xrEndFrame and friends) do not carry their XrInstance, so they need some other
// way back into the chain, and the newest instance is the one a live session belongs to.
Dispatch g_sessionDispatch{};
bool     g_sessionDispatchValid = false;

// BOTH HELPERS COPY THE TABLE OUT UNDER THE LOCK instead of handing back a pointer into the map.
// An application can recreate its runtime environment, so xrDestroyInstance can erase an entry on
// one thread while the render thread is mid-call. A pointer into an unordered_map does not survive
// that erase, and a frame loop inside a game is the worst possible place to discover it.
bool CopyTable(XrInstance instance, Dispatch* out) {
    std::lock_guard<std::mutex> lock(g_instMutex);
    auto it = g_instances.find(instance);
    if (it == g_instances.end()) return false;
    *out = it->second;
    return true;
}

bool CopySessionTable(Dispatch* out) {
    std::lock_guard<std::mutex> lock(g_instMutex);
    if (!g_sessionDispatchValid) return false;
    *out = g_sessionDispatch;
    return true;
}

// ── frame sampling ───────────────────────────────────────────────────────────────────────────
std::atomic<uint64_t> g_frameCounter{0};

bool SampleThisFrame(uint64_t frame) {
    if (g_cfg.all) return true;
    if (frame < static_cast<uint64_t>(g_cfg.framesFull)) return true;
    const int stride = (g_cfg.frameStride > 0) ? g_cfg.frameStride : 1;
    return (frame % static_cast<uint64_t>(stride)) == 0;
}

// ── hooks ────────────────────────────────────────────────────────────────────────────────────

XrResult XRAPI_CALL Probe_GetInstanceProperties(XrInstance instance,
                                                XrInstanceProperties* properties) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.GetInstanceProperties) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.GetInstanceProperties(instance, properties);
    if (XR_SUCCEEDED(r) && properties) {
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.runtimeName    = properties->runtimeName;
            g_sum.runtimeVersion = properties->runtimeVersion;
        }
        Emit("instanceProperties",
             Fmt("\"runtimeName\":\"%s\",\"runtimeVersion\":\"%llu.%llu.%llu\"",
                 JsonEscape(properties->runtimeName).c_str(),
                 static_cast<unsigned long long>(XR_VERSION_MAJOR(properties->runtimeVersion)),
                 static_cast<unsigned long long>(XR_VERSION_MINOR(properties->runtimeVersion)),
                 static_cast<unsigned long long>(XR_VERSION_PATCH(properties->runtimeVersion))));
        WriteSummary();
    }
    return r;
}

XrResult XRAPI_CALL Probe_GetSystemProperties(XrInstance instance, XrSystemId systemId,
                                              XrSystemProperties* properties) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.GetSystemProperties) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.GetSystemProperties(instance, systemId, properties);
    if (XR_SUCCEEDED(r) && properties) {
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.systemName          = properties->systemName;
            g_sum.vendorId            = properties->vendorId;
            g_sum.maxSwapchainW       = properties->graphicsProperties.maxSwapchainImageWidth;
            g_sum.maxSwapchainH       = properties->graphicsProperties.maxSwapchainImageHeight;
            g_sum.maxLayerCount       = properties->graphicsProperties.maxLayerCount;
            g_sum.orientationTracking = properties->trackingProperties.orientationTracking != XR_FALSE;
            g_sum.positionTracking    = properties->trackingProperties.positionTracking != XR_FALSE;
        }
        Emit("systemProperties",
             Fmt("\"systemName\":\"%s\",\"vendorId\":%u,\"systemId\":%llu,"
                 "\"maxSwapchainImageWidth\":%u,\"maxSwapchainImageHeight\":%u,\"maxLayerCount\":%u,"
                 "\"orientationTracking\":%s,\"positionTracking\":%s",
                 JsonEscape(properties->systemName).c_str(),
                 properties->vendorId,
                 static_cast<unsigned long long>(systemId),
                 properties->graphicsProperties.maxSwapchainImageWidth,
                 properties->graphicsProperties.maxSwapchainImageHeight,
                 properties->graphicsProperties.maxLayerCount,
                 properties->trackingProperties.orientationTracking ? "true" : "false",
                 properties->trackingProperties.positionTracking ? "true" : "false"));
        WriteSummary();
    }
    return r;
}

XrResult XRAPI_CALL Probe_EnumerateViewConfigurationViews(
        XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType,
        uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrViewConfigurationView* views) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.EnumerateViewConfigurationViews) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.EnumerateViewConfigurationViews(instance, systemId, viewConfigurationType,
                                                          viewCapacityInput, viewCountOutput, views);
    // Two-call idiom: the sizing call has no data, only the filling call is worth recording.
    if (XR_SUCCEEDED(r) && views && viewCapacityInput > 0 && viewCountOutput) {
        std::string body = Fmt("\"viewConfig\":\"%s\",\"count\":%u,\"views\":[",
                               ViewConfigName(viewConfigurationType), *viewCountOutput);
        std::vector<ViewConfigView> got;
        for (uint32_t i = 0; i < *viewCountOutput; ++i) {
            const XrViewConfigurationView& v = views[i];
            body += Fmt("%s{\"recommendedImageRectWidth\":%u,\"recommendedImageRectHeight\":%u,"
                        "\"maxImageRectWidth\":%u,\"maxImageRectHeight\":%u,"
                        "\"recommendedSwapchainSampleCount\":%u,\"maxSwapchainSampleCount\":%u}",
                        i ? "," : "",
                        v.recommendedImageRectWidth, v.recommendedImageRectHeight,
                        v.maxImageRectWidth, v.maxImageRectHeight,
                        v.recommendedSwapchainSampleCount, v.maxSwapchainSampleCount);
            got.push_back({ v.recommendedImageRectWidth, v.recommendedImageRectHeight,
                            v.maxImageRectWidth, v.maxImageRectHeight,
                            v.recommendedSwapchainSampleCount, v.maxSwapchainSampleCount });
        }
        body += "]";
        Emit("viewConfigurationViews", body);
        if (viewConfigurationType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.viewConfigViews = std::move(got);
        }
        WriteSummary();
    }
    return r;
}

XrResult XRAPI_CALL Probe_GetViewConfigurationProperties(
        XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType,
        XrViewConfigurationProperties* configurationProperties) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.GetViewConfigurationProperties) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.GetViewConfigurationProperties(instance, systemId, viewConfigurationType,
                                                         configurationProperties);
    if (XR_SUCCEEDED(r) && configurationProperties) {
        Emit("viewConfigurationProperties",
             Fmt("\"viewConfig\":\"%s\",\"fovMutable\":%s",
                 ViewConfigName(configurationProperties->viewConfigurationType),
                 configurationProperties->fovMutable ? "true" : "false"));
    }
    return r;
}

XrResult XRAPI_CALL Probe_EnumerateEnvironmentBlendModes(
        XrInstance instance, XrSystemId systemId, XrViewConfigurationType viewConfigurationType,
        uint32_t capacityInput, uint32_t* countOutput, XrEnvironmentBlendMode* modes) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.EnumerateEnvironmentBlendModes) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.EnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType,
                                                         capacityInput, countOutput, modes);
    if (XR_SUCCEEDED(r) && modes && capacityInput > 0 && countOutput) {
        std::string body = "\"modes\":[";
        for (uint32_t i = 0; i < *countOutput; ++i) {
            body += Fmt("%s\"%s\"", i ? "," : "", BlendModeName(modes[i]));
        }
        body += "]";
        Emit("environmentBlendModes", body);
    }
    return r;
}

XrResult XRAPI_CALL Probe_CreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                        XrSession* session) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.CreateSession) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.CreateSession(instance, createInfo, session);
    if (createInfo) {
        const char* binding = "none";
        for (const XrBaseInStructure* s = static_cast<const XrBaseInStructure*>(createInfo->next);
             s != nullptr; s = s->next) {
            const char* n = GraphicsBindingName(s->type);
            if (std::strcmp(n, "OTHER") != 0) { binding = n; break; }
        }
        Emit("createSession",
             Fmt("\"systemId\":%llu,\"graphicsBinding\":\"%s\",\"result\":%d",
                 static_cast<unsigned long long>(createInfo->systemId), binding, r));
    }
    return r;
}

XrResult XRAPI_CALL Probe_BeginSession(XrSession session, const XrSessionBeginInfo* beginInfo) {
    // The session handle does not carry its instance, so the dispatch is looked up from the single
    // instance we track. Applications with more than one live instance are not a real case here.
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.BeginSession) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.BeginSession(session, beginInfo);
    if (beginInfo) {
        Emit("beginSession",
             Fmt("\"primaryViewConfig\":\"%s\",\"result\":%d",
                 ViewConfigName(beginInfo->primaryViewConfigurationType), r));
    }
    return r;
}

XrResult XRAPI_CALL Probe_CreateReferenceSpace(XrSession session,
                                               const XrReferenceSpaceCreateInfo* createInfo,
                                               XrSpace* space) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.CreateReferenceSpace) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.CreateReferenceSpace(session, createInfo, space);
    if (XR_SUCCEEDED(r) && createInfo) {
        Emit("createReferenceSpace",
             Fmt("\"type\":\"%s\",\"poseInReferenceSpace\":%s,\"space\":%llu",
                 RefSpaceName(createInfo->referenceSpaceType),
                 FmtPose(createInfo->poseInReferenceSpace).c_str(),
                 space ? reinterpret_cast<unsigned long long>(*space) : 0ull));
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.referenceSpaces.push_back(RefSpaceName(createInfo->referenceSpaceType));
        }
        WriteSummary();
    }
    return r;
}

XrResult XRAPI_CALL Probe_EnumerateSwapchainFormats(XrSession session, uint32_t capacityInput,
                                                    uint32_t* countOutput, int64_t* formats) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.EnumerateSwapchainFormats) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.EnumerateSwapchainFormats(session, capacityInput, countOutput, formats);
    if (XR_SUCCEEDED(r) && formats && capacityInput > 0 && countOutput) {
        std::string body = "\"formats\":[";
        for (uint32_t i = 0; i < *countOutput; ++i) {
            const char* n = DxgiFormatName(formats[i]);
            body += Fmt("%s{\"value\":%lld,\"name\":\"%s\"}", i ? "," : "",
                        static_cast<long long>(formats[i]), n ? n : "?");
        }
        body += "]";
        Emit("swapchainFormats", body);
    }
    return r;
}

XrResult XRAPI_CALL Probe_CreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo,
                                          XrSwapchain* swapchain) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.CreateSwapchain) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.CreateSwapchain(session, createInfo, swapchain);
    if (XR_SUCCEEDED(r) && createInfo) {
        const char* n = DxgiFormatName(createInfo->format);
        Emit("createSwapchain",
             Fmt("\"swapchain\":%llu,\"width\":%u,\"height\":%u,\"format\":%lld,\"formatName\":\"%s\","
                 "\"arraySize\":%u,\"mipCount\":%u,\"faceCount\":%u,\"sampleCount\":%u,"
                 "\"usageFlags\":%llu,\"createFlags\":%llu",
                 swapchain ? reinterpret_cast<unsigned long long>(*swapchain) : 0ull,
                 createInfo->width, createInfo->height,
                 static_cast<long long>(createInfo->format), n ? n : "?",
                 createInfo->arraySize, createInfo->mipCount, createInfo->faceCount,
                 createInfo->sampleCount,
                 static_cast<unsigned long long>(createInfo->usageFlags),
                 static_cast<unsigned long long>(createInfo->createFlags)));
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.swapchains.push_back({
                swapchain ? reinterpret_cast<uint64_t>(*swapchain) : 0ull,
                createInfo->format, createInfo->width, createInfo->height,
                createInfo->arraySize, createInfo->mipCount, createInfo->sampleCount,
                createInfo->faceCount, createInfo->usageFlags });
        }
        WriteSummary();
    }
    return r;
}

XrResult XRAPI_CALL Probe_WaitFrame(XrSession session, const XrFrameWaitInfo* frameWaitInfo,
                                    XrFrameState* frameState) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.WaitFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.WaitFrame(session, frameWaitInfo, frameState);
    if (XR_SUCCEEDED(r) && frameState) {
        const uint64_t frame = g_frameCounter.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.frames = frame + 1;
            g_sum.periodMsFromRuntime = frameState->predictedDisplayPeriod / 1.0e6;
        }
        if (SampleThisFrame(frame)) {
            Emit("waitFrame",
                 Fmt("\"frame\":%llu,\"predictedDisplayTime\":%lld,\"predictedDisplayPeriod\":%lld,"
                     "\"periodMs\":%.4f,\"shouldRender\":%s",
                     static_cast<unsigned long long>(frame),
                     static_cast<long long>(frameState->predictedDisplayTime),
                     static_cast<long long>(frameState->predictedDisplayPeriod),
                     frameState->predictedDisplayPeriod / 1.0e6,
                     frameState->shouldRender ? "true" : "false"));
        }
    }
    return r;
}

XrResult XRAPI_CALL Probe_LocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo,
                                      XrViewState* viewState, uint32_t viewCapacityInput,
                                      uint32_t* viewCountOutput, XrView* views) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.LocateViews) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.LocateViews(session, viewLocateInfo, viewState, viewCapacityInput,
                                      viewCountOutput, views);
    if (!XR_SUCCEEDED(r) || !views || viewCapacityInput == 0 || !viewCountOutput) return r;

    const uint32_t n = *viewCountOutput;

    // The static geometry is captured once regardless of sampling: a headset's FOV does not change
    // per frame, and losing it because the first locate fell between samples would be absurd.
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_sum.mutex);
        if (n >= 2) {
            if (!g_sum.haveViews ||
                std::fabs(g_sum.eyeFov[0].angleLeft - views[0].fov.angleLeft) > 1e-5f ||
                std::fabs(g_sum.eyeFov[1].angleRight - views[1].fov.angleRight) > 1e-5f) {
                changed = true;
            }
            g_sum.eyeFov[0]  = views[0].fov;
            g_sum.eyeFov[1]  = views[1].fov;
            g_sum.eyePose[0] = views[0].pose;
            g_sum.eyePose[1] = views[1].pose;
            g_sum.ipd        = Dist3(views[0].pose.position, views[1].pose.position);
            g_sum.haveViews  = true;
        }
    }

    const uint64_t frame = g_frameCounter.load(std::memory_order_relaxed);
    if (changed || SampleThisFrame(frame)) {
        std::string body = Fmt("\"frame\":%llu,\"displayTime\":%lld,\"viewConfig\":\"%s\","
                               "\"viewStateFlags\":%llu,\"count\":%u,\"views\":[",
                               static_cast<unsigned long long>(frame),
                               viewLocateInfo ? static_cast<long long>(viewLocateInfo->displayTime) : 0ll,
                               viewLocateInfo ? ViewConfigName(viewLocateInfo->viewConfigurationType) : "?",
                               viewState ? static_cast<unsigned long long>(viewState->viewStateFlags) : 0ull,
                               n);
        for (uint32_t i = 0; i < n; ++i) {
            body += Fmt("%s{\"eye\":%u,\"pose\":%s,\"fov\":%s}", i ? "," : "", i,
                        FmtPose(views[i].pose).c_str(), FmtFov(views[i].fov).c_str());
        }
        body += "]";
        if (n >= 2) {
            body += Fmt(",\"ipd\":%.6f", Dist3(views[0].pose.position, views[1].pose.position));
        }
        Emit("locateViews", body);
    }
    if (changed) WriteSummary();
    return r;
}

XrResult XRAPI_CALL Probe_LocateSpace(XrSpace space, XrSpace baseSpace, XrTime time,
                                      XrSpaceLocation* location) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.LocateSpace) return XR_ERROR_FUNCTION_UNSUPPORTED;
    const XrResult r = d.LocateSpace(space, baseSpace, time, location);
    if (XR_SUCCEEDED(r) && location) {
        const uint64_t frame = g_frameCounter.load(std::memory_order_relaxed);
        if (SampleThisFrame(frame)) {
            Emit("locateSpace",
                 Fmt("\"frame\":%llu,\"space\":%llu,\"baseSpace\":%llu,\"time\":%lld,"
                     "\"locationFlags\":%llu,\"pose\":%s",
                     static_cast<unsigned long long>(frame),
                     reinterpret_cast<unsigned long long>(space),
                     reinterpret_cast<unsigned long long>(baseSpace),
                     static_cast<long long>(time),
                     static_cast<unsigned long long>(location->locationFlags),
                     FmtPose(location->pose).c_str()));
        }
    }
    return r;
}

XrResult XRAPI_CALL Probe_EndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    Dispatch d{};
    if (!CopySessionTable(&d) || !d.EndFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;

    const uint64_t frame = g_frameCounter.load(std::memory_order_relaxed);
    const bool sample = SampleThisFrame(frame);

    if (frameEndInfo) {
        // Measured period between submitted frames -- the honest refresh rate, as opposed to the
        // one the runtime advertises. They differ whenever the application misses frames.
        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            if (g_sum.lastDisplayTime != 0 && frameEndInfo->displayTime > g_sum.lastDisplayTime) {
                const double ms = (frameEndInfo->displayTime - g_sum.lastDisplayTime) / 1.0e6;
                g_sum.periodMsMeasured = (g_sum.periodMsMeasured > 0.0)
                                       ? (g_sum.periodMsMeasured * 0.95 + ms * 0.05)
                                       : ms;
            }
            g_sum.lastDisplayTime = frameEndInfo->displayTime;
        }

        uint32_t proj = 0, quad = 0, other = 0;
        bool depth = false;
        std::string layerBody;

        for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
            const XrCompositionLayerBaseHeader* base = frameEndInfo->layers[i];
            if (!base) continue;
            if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                const auto* p = reinterpret_cast<const XrCompositionLayerProjection*>(base);
                ++proj;
                if (sample) {
                    layerBody += Fmt("%s{\"type\":\"projection\",\"layerFlags\":%llu,\"space\":%llu,"
                                     "\"viewCount\":%u,\"views\":[",
                                     layerBody.empty() ? "" : ",",
                                     static_cast<unsigned long long>(p->layerFlags),
                                     reinterpret_cast<unsigned long long>(p->space),
                                     p->viewCount);
                }
                for (uint32_t v = 0; v < p->viewCount; ++v) {
                    const XrCompositionLayerProjectionView& pv = p->views[v];
                    for (const XrBaseInStructure* s =
                             static_cast<const XrBaseInStructure*>(pv.next);
                         s != nullptr; s = s->next) {
                        if (s->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) depth = true;
                    }
                    if (sample) {
                        layerBody += Fmt("%s{\"eye\":%u,\"pose\":%s,\"fov\":%s,"
                                         "\"swapchain\":%llu,\"imageArrayIndex\":%u,"
                                         "\"rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}}",
                                         v ? "," : "", v,
                                         FmtPose(pv.pose).c_str(), FmtFov(pv.fov).c_str(),
                                         reinterpret_cast<unsigned long long>(pv.subImage.swapchain),
                                         pv.subImage.imageArrayIndex,
                                         pv.subImage.imageRect.offset.x,
                                         pv.subImage.imageRect.offset.y,
                                         pv.subImage.imageRect.extent.width,
                                         pv.subImage.imageRect.extent.height);
                    }
                    if (v < 2) {
                        std::lock_guard<std::mutex> lock(g_sum.mutex);
                        g_sum.submitPose[v]       = pv.pose;
                        g_sum.submitFov[v]        = pv.fov;
                        g_sum.submitRect[v][0]    = pv.subImage.imageRect.offset.x;
                        g_sum.submitRect[v][1]    = pv.subImage.imageRect.offset.y;
                        g_sum.submitRect[v][2]    = pv.subImage.imageRect.extent.width;
                        g_sum.submitRect[v][3]    = pv.subImage.imageRect.extent.height;
                        g_sum.submitArrayIndex[v] = pv.subImage.imageArrayIndex;
                        g_sum.submitSwapchain[v]  = reinterpret_cast<uint64_t>(pv.subImage.swapchain);
                        g_sum.haveSubmit          = true;
                    }
                }
                if (sample) layerBody += "]}";
            } else if (base->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
                const auto* q = reinterpret_cast<const XrCompositionLayerQuad*>(base);
                ++quad;
                if (sample) {
                    layerBody += Fmt("%s{\"type\":\"quad\",\"eyeVisibility\":%d,\"pose\":%s,"
                                     "\"sizeM\":[%.4f,%.4f],\"swapchain\":%llu,"
                                     "\"rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}}",
                                     layerBody.empty() ? "" : ",",
                                     static_cast<int>(q->eyeVisibility),
                                     FmtPose(q->pose).c_str(),
                                     q->size.width, q->size.height,
                                     reinterpret_cast<unsigned long long>(q->subImage.swapchain),
                                     q->subImage.imageRect.offset.x,
                                     q->subImage.imageRect.offset.y,
                                     q->subImage.imageRect.extent.width,
                                     q->subImage.imageRect.extent.height);
                }
            } else {
                ++other;
                if (sample) {
                    layerBody += Fmt("%s{\"type\":\"other\",\"structureType\":%d}",
                                     layerBody.empty() ? "" : ",", static_cast<int>(base->type));
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_sum.mutex);
            g_sum.projLayerCount  = proj;
            g_sum.quadLayerCount  = quad;
            g_sum.otherLayerCount = other;
            g_sum.submitDepth     = depth;
            g_sum.blendMode       = BlendModeName(frameEndInfo->environmentBlendMode);
        }

        if (sample) {
            Emit("endFrame",
                 Fmt("\"frame\":%llu,\"displayTime\":%lld,\"blendMode\":\"%s\",\"layerCount\":%u,"
                     "\"depth\":%s,\"layers\":[%s]",
                     static_cast<unsigned long long>(frame),
                     static_cast<long long>(frameEndInfo->displayTime),
                     BlendModeName(frameEndInfo->environmentBlendMode),
                     frameEndInfo->layerCount,
                     depth ? "true" : "false",
                     layerBody.c_str()));
        }

        // REFRESH THE SUMMARY WHILE THE SESSION RUNS.
        //
        // It was otherwise only rewritten when a static fact changed, which in a steady session
        // means never: every capture taken so far stopped at the last swapchain create and had no
        // "What the application submits" section at all -- the one thing the capture exists for.
        // The data was in the .jsonl the whole time, but nobody should have to go and find it.
        static std::atomic<uint32_t> s_sinceSummary{0};
        if (sample && (s_sinceSummary.fetch_add(1, std::memory_order_relaxed) % 20) == 19) {
            WriteSummary();
        }
    }

    return d.EndFrame(session, frameEndInfo);
}

XrResult XRAPI_CALL Probe_DestroyInstance(XrInstance instance) {
    Dispatch d{};
    if (!CopyTable(instance, &d) || !d.DestroyInstance) return XR_ERROR_FUNCTION_UNSUPPORTED;
    Emit("destroyInstance", "");
    WriteSummary();
    const XrResult r = d.DestroyInstance(instance);
    {
        std::lock_guard<std::mutex> lock(g_instMutex);
        g_instances.erase(instance);
        // Repoint the session fallback at whatever is left rather than leaving it on a dead entry.
        if (g_instances.empty()) {
            g_sessionDispatchValid = false;
        } else {
            g_sessionDispatch = g_instances.begin()->second;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_ioMutex);
        if (g_jsonl) { std::fflush(g_jsonl); }
    }
    return r;
}

// ── proc-address dispatch ────────────────────────────────────────────────────────────────────
#define PROBE_INTERCEPT(fn)                                                     \
    if (std::strcmp(name, "xr" #fn) == 0) {                                     \
        *function = reinterpret_cast<PFN_xrVoidFunction>(Probe_##fn);           \
        return XR_SUCCESS;                                                      \
    }

XrResult XRAPI_CALL ProbeGetInstanceProcAddr(XrInstance instance, const char* name,
                                             PFN_xrVoidFunction* function) {
    if (!name || !function) return XR_ERROR_VALIDATION_FAILURE;

    if (instance != XR_NULL_HANDLE) {
        PROBE_INTERCEPT(DestroyInstance)
        PROBE_INTERCEPT(GetInstanceProperties)
        PROBE_INTERCEPT(GetSystemProperties)
        PROBE_INTERCEPT(EnumerateViewConfigurationViews)
        PROBE_INTERCEPT(GetViewConfigurationProperties)
        PROBE_INTERCEPT(EnumerateEnvironmentBlendModes)
        PROBE_INTERCEPT(CreateSession)
        PROBE_INTERCEPT(BeginSession)
        PROBE_INTERCEPT(CreateReferenceSpace)
        PROBE_INTERCEPT(EnumerateSwapchainFormats)
        PROBE_INTERCEPT(CreateSwapchain)
        PROBE_INTERCEPT(WaitFrame)
        PROBE_INTERCEPT(LocateViews)
        PROBE_INTERCEPT(LocateSpace)
        PROBE_INTERCEPT(EndFrame)
    }

    Dispatch d{};
    if (CopyTable(instance, &d) && d.GetInstanceProcAddr) {
        return d.GetInstanceProcAddr(instance, name, function);
    }
    // Before xrCreateInstance completes there is no per-instance table yet; fall back to the most
    // recent chain the loader handed us at negotiation time.
    if (CopySessionTable(&d) && d.GetInstanceProcAddr) {
        return d.GetInstanceProcAddr(instance, name, function);
    }
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}
#undef PROBE_INTERCEPT

// ── file setup ───────────────────────────────────────────────────────────────────────────────
void OpenOutput() {
    std::lock_guard<std::mutex> lock(g_ioMutex);
    if (g_jsonl) return;

    g_cfg.frameStride = EnvInt("XRPROBE_FRAME_STRIDE", 90);
    g_cfg.framesFull  = EnvInt("XRPROBE_FRAMES_FULL", 120);
    g_cfg.all         = EnvInt("XRPROBE_ALL", 0) != 0;
    g_cfg.dir         = EnvStr("XRPROBE_DIR");

    if (g_cfg.dir.empty()) {
        char local[MAX_PATH] = {};
        if (GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local)) > 0) {
            g_cfg.dir = std::string(local) + "\\xrprobe";
        } else {
            g_cfg.dir = ".";
        }
    }
    CreateDirectoryA(g_cfg.dir.c_str(), nullptr);

    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, sizeof(exePath)) > 0) {
        const char* slash = std::strrchr(exePath, '\\');
        g_exeName = slash ? (slash + 1) : exePath;
        const size_t dot = g_exeName.rfind('.');
        if (dot != std::string::npos) g_exeName.resize(dot);
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    const std::string stem = Fmt("%s\\xrprobe-%s-%04u%02u%02u-%02u%02u%02u-%lu",
                                 g_cfg.dir.c_str(), g_exeName.c_str(),
                                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                                 GetCurrentProcessId());
    const std::string jsonlPath = stem + ".jsonl";
    g_txtPath = stem + ".txt";

    // _fsopen with _SH_DENYNO, not fopen_s: fopen_s denies sharing, so the stream cannot be read
    // while the game that is producing it runs -- which is exactly when you want to read it.
    g_jsonl = _fsopen(jsonlPath.c_str(), "w", _SH_DENYNO);
    if (g_jsonl) {
        std::fprintf(g_jsonl,
                     "{\"n\":0,\"ev\":\"probeStart\",\"layer\":\"%s\",\"exe\":\"%s\",\"pid\":%lu,"
                     "\"frameStride\":%d,\"framesFull\":%d,\"all\":%s,\"summary\":\"%s\"}\n",
                     kLayerName, JsonEscape(g_exeName.c_str()).c_str(), GetCurrentProcessId(),
                     g_cfg.frameStride, g_cfg.framesFull, g_cfg.all ? "true" : "false",
                     JsonEscape(g_txtPath.c_str()).c_str());
        std::fflush(g_jsonl);
    }
}

// ── layer instance creation ──────────────────────────────────────────────────────────────────
XrResult XRAPI_CALL ProbeCreateApiLayerInstance(const XrInstanceCreateInfo* info,
                                                const XrApiLayerCreateInfo* apiLayerInfo,
                                                XrInstance* instance) {
    if (!apiLayerInfo || !apiLayerInfo->nextInfo || !info || !instance) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    OpenOutput();

    // Walk one link down the layer chain before calling through, exactly as the loader spec
    // requires: the next layer must see its own nextInfo at the head, not ours.
    XrApiLayerCreateInfo chained = *apiLayerInfo;
    chained.nextInfo = apiLayerInfo->nextInfo->next;

    const PFN_xrGetInstanceProcAddr nextGipa = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    const PFN_xrCreateApiLayerInstance nextCreate = apiLayerInfo->nextInfo->nextCreateApiLayerInstance;
    if (!nextGipa || !nextCreate) return XR_ERROR_INITIALIZATION_FAILED;

    const XrResult r = nextCreate(info, &chained, instance);
    if (!XR_SUCCEEDED(r)) {
        Emit("createInstanceFailed", Fmt("\"result\":%d", r));
        return r;
    }

    Dispatch d{};
    d.GetInstanceProcAddr = nextGipa;

#define PROBE_RESOLVE(fn)                                                                    \
    do {                                                                                     \
        PFN_xrVoidFunction p = nullptr;                                                      \
        if (XR_SUCCEEDED(nextGipa(*instance, "xr" #fn, &p))) {                               \
            d.fn = reinterpret_cast<PFN_xr##fn>(p);                                          \
        }                                                                                    \
    } while (0)

    PROBE_RESOLVE(DestroyInstance);
    PROBE_RESOLVE(GetInstanceProperties);
    PROBE_RESOLVE(GetSystemProperties);
    PROBE_RESOLVE(EnumerateViewConfigurationViews);
    PROBE_RESOLVE(GetViewConfigurationProperties);
    PROBE_RESOLVE(EnumerateEnvironmentBlendModes);
    PROBE_RESOLVE(CreateSession);
    PROBE_RESOLVE(BeginSession);
    PROBE_RESOLVE(CreateReferenceSpace);
    PROBE_RESOLVE(EnumerateSwapchainFormats);
    PROBE_RESOLVE(CreateSwapchain);
    PROBE_RESOLVE(WaitFrame);
    PROBE_RESOLVE(LocateViews);
    PROBE_RESOLVE(LocateSpace);
    PROBE_RESOLVE(EndFrame);
#undef PROBE_RESOLVE

    {
        std::lock_guard<std::mutex> lock(g_instMutex);
        g_instances[*instance] = d;
        g_sessionDispatch = d;
        g_sessionDispatchValid = true;
    }

    std::string exts = "[";
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        exts += Fmt("%s\"%s\"", i ? "," : "", JsonEscape(info->enabledExtensionNames[i]).c_str());
    }
    exts += "]";

    {
        std::lock_guard<std::mutex> lock(g_sum.mutex);
        g_sum.appName       = info->applicationInfo.applicationName;
        g_sum.appVersion    = info->applicationInfo.applicationVersion;
        g_sum.engineName    = info->applicationInfo.engineName;
        g_sum.engineVersion = info->applicationInfo.engineVersion;
        g_sum.extensions.clear();
        for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
            g_sum.extensions.emplace_back(info->enabledExtensionNames[i]);
        }
    }

    Emit("createInstance",
         Fmt("\"applicationName\":\"%s\",\"applicationVersion\":%u,\"engineName\":\"%s\","
             "\"engineVersion\":%u,\"apiVersion\":\"%llu.%llu.%llu\",\"extensions\":%s",
             JsonEscape(info->applicationInfo.applicationName).c_str(),
             info->applicationInfo.applicationVersion,
             JsonEscape(info->applicationInfo.engineName).c_str(),
             info->applicationInfo.engineVersion,
             static_cast<unsigned long long>(XR_VERSION_MAJOR(info->applicationInfo.apiVersion)),
             static_cast<unsigned long long>(XR_VERSION_MINOR(info->applicationInfo.apiVersion)),
             static_cast<unsigned long long>(XR_VERSION_PATCH(info->applicationInfo.apiVersion)),
             exts.c_str()));
    WriteSummary();
    return r;
}

}  // namespace

// ── loader negotiation ───────────────────────────────────────────────────────────────────────
extern "C" __declspec(dllexport) XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(const XrNegotiateLoaderInfo* loaderInfo,
                                   const char* layerName,
                                   XrNegotiateApiLayerRequest* apiLayerRequest) {
    if (!loaderInfo || !apiLayerRequest) return XR_ERROR_INITIALIZATION_FAILED;
    if (layerName && std::strcmp(layerName, kLayerName) != 0) return XR_ERROR_INITIALIZATION_FAILED;

    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (XR_CURRENT_LOADER_API_LAYER_VERSION < loaderInfo->minInterfaceVersion ||
        XR_CURRENT_LOADER_API_LAYER_VERSION > loaderInfo->maxInterfaceVersion) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    apiLayerRequest->layerApiVersion       = XR_CURRENT_API_VERSION;
    apiLayerRequest->getInstanceProcAddr   = ProbeGetInstanceProcAddr;
    apiLayerRequest->createApiLayerInstance = ProbeCreateApiLayerInstance;
    return XR_SUCCESS;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) {
        // Best effort: a graceful xrDestroyInstance already wrote the summary, but a process that
        // exits without one still leaves a complete file behind.
        WriteSummary();
    }
    return TRUE;
}
