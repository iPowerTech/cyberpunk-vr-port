// Bisection step 1 for the DLSS neural-render port: a PURE pass-through hook on
// the game's DLSS evaluation.
//
// The neural-render architecture (Architecture A) rides the game's own DLSS
// EvaluateFeature: right after the game evaluates DLSS for a view, its
// Color/MotionVectors/Depth/Output are bound to that command list, and we could
// run feature 18 (DLSS-D neural render) on them. Every later step builds on a
// MinHook detour of that function.
//
// So the first thing to isolate is the detour itself. This file installs
// exactly that detour and NOTHING else: it counts the calls and immediately
// re-enters the original. If the game already shows the tonality artifacts with
// only this in place, the culprit is hooking the function at all, and the whole
// approach has to change. If the game stays clean, hooking is safe and the next
// step (reading the bound resources) can be added.
//
// The target is the CORE NGX dispatcher _nvngx.dll!NVSDK_NGX_D3D12_EvaluateFeature.
// CP2077 drives DLSS through Streamline (sl.interposer.dll), which forwards each
// evaluation into the core; hooking the core is where both eyes were observed to
// pass through. MinHook is used (not the hand-rolled E9 in Ngx.cpp, which cannot
// relocate this prologue) and is already initialised by the plugin bootstrap.
//
// Signature (no NGX SDK dependency, x64 so the calling convention is fixed):
//   NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature(
//       ID3D12GraphicsCommandList* cmdList,
//       const NVSDK_NGX_Handle*    featureHandle,
//       const NVSDK_NGX_Parameter* params,
//       PFN_NVSDK_NGX_ProgressCallback callback);

#include "Hooks/NgxEvalPassthrough.hpp"
#include "Stereo/StereoInternal.hpp"   // ::cvr::detail::g_game_device
#include "FrameGen/FrameGenFsr3.hpp"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <mutex>

extern void Log(const char* fmt, ...);
extern volatile int g_verboseLog;

// Arm switch: default on. If turned off before the first successful arm, the
// hook is never installed (a clean way to A/B this step at runtime). Once
// installed the detour always passes through.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NgxPassthrough = 1;

// Diagnostics the overlay / a debugger can read: 1 once armed, and the running
// evaluation count so we can confirm the detour actually fires.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NgxPassArmed = 0;
extern "C" __declspec(dllexport) uint64_t CyberpunkVR_NgxPassEvals = 0;

// Bisection step 2: read the bound resources read-only. Default on; a runtime
// A/B switch. When 0 the detour is exactly the step-1 pass-through.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NgxReadResources = 1;

// Bisection step 3: initialise NGX for neural render and, per DLSS handle,
// create feature 18 + its R10G10B10A2 textures and KEEP them alive alongside
// the game's DLSS. NOTHING is evaluated, encoded, copied, or written to any
// game resource -- this only tests whether a live feature 18 next to the game's
// own DLSS disturbs the image. Default on for the A/B run.
// DEFAULT OFF. The create/eval path can crash the game (a known NR-on crash), so
// it must be opt-in from the DLSS5-NR tab, never armed at startup -- a bad build
// then cannot brick the game's boot. Steps 1-2 (pass-through + read-only) stay on
// and are proven clean.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrCreate = 0;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrInited = 0;      // 1 once Init_Ext ok
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrContexts = 0;    // live NR contexts created
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_NrCreateResult = 0; // last CreateFeature HR
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrDisabled = 0;    // 1 = a fault stopped NR

// Bisection step 4a: evaluate feature 18 into OUR OWN output texture. No codec,
// no copy/decode back to the game -- the game's image is never written, only our
// c->output is, then discarded. This isolates the neural pass's frame cost (the
// measured ~5 fps "eval only") from any write-back color artifacts, and proves
// the game stays clean while NR runs.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrEval = 0;        // 4a: run the eval (opt-in)

// The full DLSS5-NR parameter surface, the same names RenoDX's ReShade panel
// exposes. Defaults match the add-on's; a value of -1 means "let the model
// decide". Wired into the eval where it maps directly; the HDR/codec ones
// (PaperWhite/HdrTransfer/ColorStrength/DepthConversion) take effect once the
// write-back codec is added.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrPreset = 0;          // NR Preset
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrStyle = 1;           // NR Style
extern "C" __declspec(dllexport) float   CyberpunkVR_NrIntensity = 2.0f;    // NR Intensity
extern "C" __declspec(dllexport) float   CyberpunkVR_NrLocalTone = 2.0f;    // Local Tone Strength
extern "C" __declspec(dllexport) float   CyberpunkVR_NrLocalStructure = 2.0f; // Local Structure Strength
extern "C" __declspec(dllexport) float   CyberpunkVR_NrSkinStructure = -1.0f; // Skin Structure Strength
extern "C" __declspec(dllexport) float   CyberpunkVR_NrPaperWhite = 1.0f;   // Scene paper-white scale
extern "C" __declspec(dllexport) float   CyberpunkVR_NrHdrTransfer = 1.0f;  // HDR Transfer Strength
extern "C" __declspec(dllexport) float   CyberpunkVR_NrColorStrength = 1.0f;// Color Strength
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrDepthConversion = 0; // Depth Conversion
extern "C" __declspec(dllexport) float   CyberpunkVR_NrMotionScaleX = 1.0f; // Motion Scale X Multiplier
extern "C" __declspec(dllexport) float   CyberpunkVR_NrMotionScaleY = 1.0f; // Motion Scale Y Multiplier
// ALTERNATE EYES: 1 = denoise only one eye per frame (left on even frames, right
// on odd), halving the NR work. The eye not denoised that frame shows the game's
// own image. Experimental -- the per-eye temporal history then updates at half
// rate, which may shimmer; the point is to measure the fps gain.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrAlternateEyes = 0;

extern "C" __declspec(dllexport) uint32_t CyberpunkVR_NrEvalsL = 0;
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_NrEvalsR = 0;
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_NrLastResult = 0;
// Write-back: copy the game's Output into our color, denoise, and copy the
// denoised result back onto the game's Output so it is the frame presented.
// Off by default -- turning it on is what makes the neural render visible.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrWriteBack = 0;
// Diagnostics for the "moving multicolour noise": Reset=1 disables temporal
// accumulation (splits temporal-divergence from a bad colour input); MVecScaleMode
// picks the motion-vector scale base (0=1, 1=+render, 2=-render, 3=display).
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrReset = 0;
extern "C" __declspec(dllexport) int32_t CyberpunkVR_NrMVecScaleMode = 1;
extern "C" __declspec(dllexport) float    CyberpunkVR_NrPassesPerSec = 0.0f; // last measured rate

// FSR3 frame-generation SMOKE TEST (vr_framegen branch): runs a real ffxFrameInterpolationDispatch
// per eye using Color/MotionVectors/Depth read the same way NR reads them, INDEPENDENT of the NR
// switches above (so it can be tested without NR on). The interpolated result is written to a
// scratch texture and discarded -- see FrameGen_DispatchSmokeTest's doc comment. Default OFF.
extern "C" __declspec(dllexport) int32_t CyberpunkVR_FrameGenEnable = 0;
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_FrameGenDispatches = 0;
extern "C" __declspec(dllexport) uint32_t CyberpunkVR_FrameGenLastResult = 0;

namespace {

using EvalFeatureFn = uint32_t(*)(void* /*cmdList*/, const void* /*handle*/,
                                  const void* /*params*/, void* /*callback*/);
EvalFeatureFn g_origEval = nullptr;
std::atomic<uint64_t> g_calls{0};

// A minimal, trivially-copyable view of the NGX parameter object. Only the
// getters step 2 needs; the vtable offsets are the empirically measured ones
// (CP2077 + Streamline), NOT the SDK declaration order. GetResource is +0x48,
// GetInt +0x60. No destructor, so this is safe to use inside an SEH frame.
struct NgxParamsRO {
    void* vtbl;
    void* GetResource(const char* n) {
        void* out = nullptr;
        using Fn = unsigned (__fastcall*)(void*, const char*, void**);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x48))(this, n, &out);
        return out;
    }
    unsigned GetInt(const char* n, int* out) {
        using Fn = unsigned (__fastcall*)(void*, const char*, int*);
        return (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x60))(this, n, out);
    }
};

// Fetch one bound resource by NGX key and read its dimensions/format -- entirely
// inside an SEH frame, since both the vtable getter and GetDesc run on a foreign
// object. READ ONLY: NGX getters return the stored pointer without AddRef, and
// GetDesc changes no refcount, so nothing is released. Returns false and touches
// nothing on any fault or an absent key.
bool ReadResDims(void* params, const char* key, void** res, int* w, int* h, unsigned* fmt) {
    if (!params) return false;
    __try {
        NgxParamsRO* p = reinterpret_cast<NgxParamsRO*>(params);
        void* r = p->GetResource(key);
        if (!r) return false;
        D3D12_RESOURCE_DESC d = reinterpret_cast<ID3D12Resource*>(r)->GetDesc();
        *res = r;
        *w = static_cast<int>(d.Width);
        *h = static_cast<int>(d.Height);
        *fmt = static_cast<unsigned>(d.Format);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Per-handle "already logged its resource layout" table. A DLSS instance per
// eye, so a handful of entries is plenty.
std::mutex g_seenMutex;
void* g_seenHandles[8] = {};
unsigned g_seenN = 0;

// Log Color/MotionVectors/Depth/Output for a handle the first time it is seen.
void LogResourcesOnce(void* handle, void* params) {
    {
        std::lock_guard<std::mutex> lock(g_seenMutex);
        for (unsigned i = 0; i < g_seenN; ++i) {
            if (g_seenHandles[i] == handle) return;  // already logged
        }
        if (g_seenN >= 8) return;
        g_seenHandles[g_seenN++] = handle;
    }
    static const char* const kKeys[] = { "Color", "MotionVectors", "Depth", "Output" };
    Log("[ngxpass] handle=%p resource layout:\n", handle);
    for (const char* key : kKeys) {
        void* res = nullptr; int w = 0, h = 0; unsigned fmt = 0;
        if (ReadResDims(params, key, &res, &w, &h, &fmt)) {
            Log("[ngxpass]   %-13s res=%p  %dx%d fmt=%u\n", key, res, w, h, fmt);
        } else {
            Log("[ngxpass]   %-13s (absent or unreadable)\n", key);
        }
    }
}

// The core NGX modules that export the D3D12 evaluate entry point. _nvngx.dll is
// the real dispatcher CP2077 reaches via Streamline; nvngx.dll is the redirect
// name some driver versions use. The per-feature DLLs (nvngx_dlss.dll etc.) are
// NOT hooked -- the dispatcher is the single choke point for every view.
const wchar_t* const kNgxModules[] = {
    L"_nvngx.dll", L"nvngx.dll", L"nvngx_dlss.dll", L"nvngx_dlssd.dll",
};

// =============================================================================
// STEP 3: create and KEEP feature 18 + textures per handle. No evaluate, no
// codec, no copy. Ported from the proven "port can drive NGX feature 18" recipe
// (branch dlss-neural-render, commit 712728a), stripped to creation only.
// =============================================================================

// A separate log file so the NR create trace stays out of the main plugin log.
void TestLog(const char* fmt, ...) {
    FILE* f = nullptr;
    if (fopen_s(&f, "cyberpunkvrport_ngx.txt", "a") != 0 || !f) return;
    va_list args; va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

// The NGX surface: four function shapes and one enum value, declared not
// included (the SDK is not a dependency).
using InitExtFn      = unsigned int (__stdcall*)(unsigned long long, const wchar_t*, ID3D12Device*,
                                                 unsigned int, const void*);
using AllocParamsFn  = unsigned int (__stdcall*)(void**);
using DestroyParamsFn= unsigned int (__stdcall*)(void*);
using CreateFeatFn   = unsigned int (__stdcall*)(ID3D12GraphicsCommandList*, unsigned int, void*, void**);
using ReleaseFeatFn  = unsigned int (__stdcall*)(void*);
using EvalFeatFn2    = unsigned int (__stdcall*)(ID3D12GraphicsCommandList*, void*, void*, void*);

constexpr unsigned int        kNgxVersionApi = 0x15;
constexpr unsigned long long  kAppId         = 0x876232C;
constexpr unsigned int        kFeatureDlssNr = 18;

bool NgxFailed(unsigned r) { return (r & 0xFFF00000u) == 0xBAD00000u; }

// Full parameter view: setters for create + the getters the scaling callback
// needs. Empirically measured vtable offsets (NOT SDK order). POD, no dtor.
struct NgxParams {
    void* vtbl;
    void SetPtr(const char* n, void* v) {
        using Fn = void (__fastcall*)(void*, const char*, void*);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x00))(this, n, v);
    }
    void SetResource(const char* n, void* res) {
        using Fn = void (__fastcall*)(void*, const char*, void*);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x08))(this, n, res);
    }
    void SetUInt(const char* n, unsigned v) {
        using Fn = void (__fastcall*)(void*, const char*, unsigned);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x18))(this, n, v);
    }
    void SetInt(const char* n, int v) {
        using Fn = void (__fastcall*)(void*, const char*, int);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x20))(this, n, v);
    }
    void SetFloat(const char* n, float v) {
        using Fn = void (__fastcall*)(void*, const char*, float);
        (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x30))(this, n, v);
    }
    unsigned GetUInt(const char* n, unsigned* out) {
        using Fn = unsigned (__fastcall*)(void*, const char*, unsigned*);
        return (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x58))(this, n, out);
    }
    unsigned GetInt(const char* n, int* out) {
        using Fn = unsigned (__fastcall*)(void*, const char*, int*);
        return (*reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + 0x60))(this, n, out);
    }
};

// The runtime calls this while deciding how much to scale (add-on FUN_18001740).
unsigned __fastcall ComputeScalingRatioCallback(NgxParams* p) {
    if (!p) return 0xBAD00005u;
    unsigned upscaling = 0;
    if (NgxFailed(p->GetUInt("DLSSNR.Upscaling", &upscaling))) upscaling = 0;
    int inW = 0, outW = 0;
    if (NgxFailed(p->GetInt("DLSSNR.InputWidth", &inW))) inW = 0;
    if (NgxFailed(p->GetInt("DLSSNR.OutputWidth", &outW))) outW = 0;
    float ratio = 1.0f;
    if (upscaling && inW && outW) ratio = static_cast<float>(inW) / static_cast<float>(outW);
    p->SetFloat("DLSSNR.ScalingRatio", ratio);
    return 1;
}

// The create parameters. Feature 18 is an upscaler+denoiser meant to run the
// transformer at RENDER resolution and produce the DISPLAY-resolution output --
// that is what makes it affordable. Running it native at display res (rW==dW)
// was the ~5 fps case; the render->display contract is the fast one.
void FillCreateParams(NgxParams* p, int rW, int rH, int dW, int dH) {
    const bool upscale = (rW != dW) || (rH != dH);
    // Matches the add-on's create (FUN_180019a50): Width/Height = render input,
    // OutWidth/Height = display, but DLSSNR.Width/Height = DISPLAY (the output),
    // and DLSSNR.InputWidth/Height = render.
    const float ratio = upscale ? (dW ? (float)rW / (float)dW : 1.0f) : 1.0f;
    p->SetInt("Width", rW);                      p->SetInt("Height", rH);       // input (render)
    p->SetInt("OutWidth", dW);                   p->SetInt("OutHeight", dH);    // output (display)
    p->SetInt("DLSSNR.Width", dW);               p->SetInt("DLSSNR.Height", dH);        // = display
    p->SetInt("DLSSNR.InputWidth", rW);          p->SetInt("DLSSNR.InputHeight", rH);   // = render
    p->SetInt("DLSSNR.OutputWidth", dW);         p->SetInt("DLSSNR.OutputHeight", dH);
    p->SetInt("DLSSNR.Output.Width", dW);        p->SetInt("DLSSNR.Output.Height", dH);
    p->SetFloat("DLSSNR.ScalingRatio", ratio);
    p->SetFloat("DLSSNR.Scale", ratio);
    p->SetUInt("DLSSNR.Upscaling", upscale ? 1u : 0u);
    p->SetPtr("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ComputeScalingRatioCallback));
    p->SetInt("DLSSNR.Hint.Render.Preset", CyberpunkVR_NrPreset);
    p->SetUInt("PerfQualityValue", 0);
    p->SetUInt("DLSS.Feature.Create.Flags", 0x4B);   // measured add-on value
    p->SetInt("CreationNodeMask", 1);
    p->SetInt("VisibilityNodeMask", 1);
}

// GetModuleFileNameW spoof, scoped by InitSpoof around Init_Ext AND create: the
// signed snippet vets its caller on every entry point and accepts only a name
// ending in nvngx.dll, so we answer "nvngx.dll" for our own module handle.
using GetModFileNameFn = DWORD (WINAPI*)(HMODULE, LPWSTR, DWORD);
GetModFileNameFn g_realGetModFileName = nullptr;
std::atomic<bool> g_spoofActive{false};
HMODULE g_ourModule = nullptr;

DWORD WINAPI Hooked_GetModuleFileNameW(HMODULE mod, LPWSTR out, DWORD size) {
    // Spoof ONLY for our own module handle -- the one the snippet passes when it
    // walks back to its caller to vet the name. Every other query (including the
    // NULL/exe-path one our own code uses to find the game directory) passes
    // straight through, which is what makes leaving this hook permanently armed
    // safe.
    if (out && size >= 10 && g_ourModule && mod == g_ourModule) {
        static const wchar_t kName[] = L"nvngx.dll";
        memcpy(out, kName, sizeof(kName));
        return 9;
    }
    return g_realGetModFileName ? g_realGetModFileName(mod, out, size)
                                : GetModuleFileNameW(mod, out, size);
}

// Arm the caller-name spoof ONCE and leave it in place, the way RenoDX patches
// the snippet's IAT permanently. The previous per-call RAII toggled the MinHook
// on/off around every Init/Create/Evaluate, and MH_Enable/DisableHook SUSPENDS
// EVERY THREAD in the process each time -- four all-thread suspends per frame,
// which was a large part of the ~100 ms-per-eval cost. Arming once removes it.
std::atomic<bool> g_spoofArmed{false};
void EnsureSpoof() {
    if (g_spoofArmed.load(std::memory_order_acquire)) return;
    if (!g_ourModule) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Hooked_GetModuleFileNameW), &g_ourModule);
    }
    void* target = reinterpret_cast<void*>(&GetModuleFileNameW);
    MH_Initialize();
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hooked_GetModuleFileNameW),
                      reinterpret_cast<void**>(&g_realGetModFileName)) == MH_OK &&
        MH_EnableHook(target) == MH_OK) {
        g_spoofActive.store(true, std::memory_order_release);
        g_spoofArmed.store(true, std::memory_order_release);
        TestLog("[nr] caller-name spoof armed once (persistent).\n");
    } else {
        TestLog("[nr] caller-name spoof could not be armed.\n");
    }
}

// A committed display-res texture usable as an NGX UAV resource.
ID3D12Resource* MakeNrTexture(ID3D12Device* dev, int w, int h, DXGI_FORMAT fmt) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)w; rd.Height = (UINT)h;
    rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res)))) return nullptr;
    return res;
}

// Per-handle NR context: feature 18 + its textures + a param object, kept alive.
struct NrContext {
    void*           handle = nullptr;
    void*           nrFeature = nullptr;
    ID3D12Resource* color = nullptr;    // display-res scratch (unused in render-res path)
    ID3D12Resource* output = nullptr;   // display-res NR result
    void*           params = nullptr;
    int             rw = 0, rh = 0;     // render (input) res
    int             w = 0, h = 0;       // display (output) res
    bool            broken = false;
    bool            viaCore = false;    // created by core (use core eval) vs snippet
    bool            justCreated = false; // force a Reset on this handle's first eval
};

NrContext g_nr[8] = {};
unsigned  g_nrN = 0;
std::mutex g_nrMutex;

CreateFeatFn    g_nrCreate    = nullptr; // snippet's CreateFeature
EvalFeatFn2     g_nrEvaluate  = nullptr; // snippet's EvaluateFeature
CreateFeatFn    g_coreCreate  = nullptr; // core's CreateFeature (RenoDX's primary path)
EvalFeatFn2     g_coreEvaluate= nullptr; // core's EvaluateFeature
AllocParamsFn   g_nrAlloc     = nullptr; // core's AllocateParameters
DestroyParamsFn g_nrDestroy   = nullptr; // core's DestroyParameters
bool            g_nrInited    = false;   // Init_Ext done once

// A resource state transition on the game's command list.
void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
             D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to || !res) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    list->ResourceBarrier(1, &b);
}

// ---------------------------------------------------------------------------------------------
// COLOUR CODEC. The DLSS-D model wants a display-encoded, [0,1]-range input; the game's Output is
// scene-linear HDR FP16 (values well past 1.0), which makes the model diverge into moving colour
// noise. So: ENCODE the game Output into a tonemapped SDR proxy before NR, DECODE the NR result
// back to scene-linear HDR after -- the same idea as the add-on's "SDR proxy" codec.
static const char* kCodecHLSL = R"HLSL(
cbuffer K : register(b0) { uint W; uint H; float PaperWhite; float ColorStrength; };
Texture2D<float4>   Src : register(t0);
RWTexture2D<float4> Dst : register(u0);

float3 SrgbEncode(float3 c){ c=saturate(c); return (c<=0.0031308)?(c*12.92):(1.055*pow(c,1.0/2.4)-0.055); }
float3 SrgbDecode(float3 c){ return (c<=0.04045)?(c/12.92):pow((c+0.055)/1.055,2.4); }

[numthreads(8,8,1)]
void Encode(uint3 id : SV_DispatchThreadID) {
    if (id.x >= W || id.y >= H) return;
    float pw = max(1e-4, PaperWhite);
    float3 c = max(0.0.xxx, Src[id.xy].rgb) / pw;
    c = c / (1.0 + c);                       // Reinhard tonemap -> [0,1)
    Dst[id.xy] = float4(SrgbEncode(c), 1.0); // display-encoded SDR proxy for NR
}

[numthreads(8,8,1)]
void Decode(uint3 id : SV_DispatchThreadID) {
    if (id.x >= W || id.y >= H) return;
    float pw = max(1e-4, PaperWhite);
    float3 n = SrgbDecode(saturate(Src[id.xy].rgb));   // decoded model output, [0,1)
    float4 o = Dst[id.xy];                              // still the game's original at this point
    // Degenerate-proxy guard: the DLSSNR SDR proxy can collapse to near-black for
    // some HDR inputs (highlights/reflections); without this a degenerate frame
    // writes near-zero straight over the game's Output. Keep the original instead.
    float ny = dot(n, float3(0.2126, 0.7152, 0.0722));
    if (ny <= 1e-5) { Dst[id.xy] = o; return; }
    float3 c = (n / max(1e-4, 1.0 - n)) * pw;          // inverse Reinhard back to scene-linear HDR
    // Blow-up clamp: as n approaches 1.0 the inverse Reinhard denominator approaches
    // 0 and c explodes toward +inf; cap the luma ratio against the original like the
    // RenoDX add-on does (clamp(neural/original, 0, 4)) instead of writing unbounded
    // HDR values that corrupt whatever consumes Output next (exposure/history/TAA).
    float oy = dot(max(0.0.xxx, o.rgb), float3(0.2126, 0.7152, 0.0722));
    float cy = dot(max(0.0.xxx, c), float3(0.2126, 0.7152, 0.0722));
    if (oy > 1e-5 && cy > 1e-5) {
        float ratio = clamp(cy / oy, 0.0, 4.0);
        c *= (ratio * oy) / cy;   // rescale luma to the clamped ratio, keep chroma
    }
    Dst[id.xy] = float4(lerp(o.rgb, c, saturate(ColorStrength)), o.a);
}
)HLSL";

struct NrCodec {
    ID3D12RootSignature*  rootSig = nullptr;
    ID3D12PipelineState*  psoEncode = nullptr;
    ID3D12PipelineState*  psoDecode = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT descSize = 0;
    UINT ring = 0;
    static const UINT kRing = 256;
    bool ready = false;

    bool CompileCS(const char* entry, ID3DBlob** out) {
        ID3DBlob* err = nullptr;
        HRESULT hr = D3DCompile(kCodecHLSL, strlen(kCodecHLSL), nullptr, nullptr, nullptr,
                                entry, "cs_5_0", 0, 0, out, &err);
        if (FAILED(hr)) {
            TestLog("[nr] codec %s compile 0x%08X %.*s\n", entry, hr,
                    err ? (int)err->GetBufferSize() : 0,
                    err ? (const char*)err->GetBufferPointer() : "");
            if (err) err->Release();
            return false;
        }
        return true;
    }

    bool Init(ID3D12Device* dev) {
        if (ready) return true;
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 2;
        params[1].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rs = {}; rs.NumParameters = 2; rs.pParameters = params;
        ID3DBlob* rsBlob = nullptr; ID3DBlob* rsErr = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr))) {
            if (rsErr) rsErr->Release(); TestLog("[nr] codec root sig failed\n"); return false;
        }
        HRESULT hr = dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&rootSig));
        rsBlob->Release();
        if (FAILED(hr)) { TestLog("[nr] codec CreateRootSignature 0x%08X\n", hr); return false; }

        ID3DBlob* enc = nullptr; ID3DBlob* dec = nullptr;
        if (!CompileCS("Encode", &enc) || !CompileCS("Decode", &dec)) {
            if (enc) enc->Release(); if (dec) dec->Release(); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = rootSig;
        pd.CS = { enc->GetBufferPointer(), enc->GetBufferSize() };
        hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&psoEncode));
        pd.CS = { dec->GetBufferPointer(), dec->GetBufferSize() };
        HRESULT hr2 = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&psoDecode));
        enc->Release(); dec->Release();
        if (FAILED(hr) || FAILED(hr2)) { TestLog("[nr] codec PSO failed\n"); return false; }

        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kRing;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) {
            TestLog("[nr] codec heap failed\n"); return false;
        }
        descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ready = true;
        TestLog("[nr] codec ready (Reinhard/sRGB tonemap, FP16)\n");
        return true;
    }

    // One dispatch: src (SRV) -> dst (UAV). Caller barriers the resources into SRV/UAV state first.
    void Run(ID3D12Device* dev, ID3D12GraphicsCommandList* list, bool encode,
             ID3D12Resource* src, ID3D12Resource* dst, int w, int h,
             float paperWhite, float colorStrength) {
        if (!ready) return;
        UINT slot = (ring % (kRing / 2)) * 2; ++ring;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += (SIZE_T)slot * descSize;
        gpu.ptr += (UINT64)slot * descSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(src, &sv, cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = cpu; cpu2.ptr += descSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(dst, nullptr, &uv, cpu2);

        ID3D12DescriptorHeap* heaps[] = { heap };
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(rootSig);
        list->SetPipelineState(encode ? psoEncode : psoDecode);
        UINT k[4]; k[0] = (UINT)w; k[1] = (UINT)h;
        memcpy(&k[2], &paperWhite, 4); memcpy(&k[3], &colorStrength, 4);
        list->SetComputeRoot32BitConstants(0, 4, k, 0);
        list->SetComputeRootDescriptorTable(1, gpu);
        list->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    }
};
NrCodec g_codec;

// Get the device that owns a foreign resource, in its own SEH frame.
bool SafeGetDevice(void* res, ID3D12Device** out) {
    __try {
        return SUCCEEDED(reinterpret_cast<ID3D12Resource*>(res)->GetDevice(IID_PPV_ARGS(out)));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// NGX logging sink: NGX calls this with every internal message, including the
// exact reason an EvaluateFeature returns InvalidParameter. Wired in through the
// FeatureCommonInfo passed to Init_Ext.
void __cdecl NgxLogSink(const char* message, int loggingLevel, int sourceComponent) {
    if (message) TestLog("[ngx] (%d/%d) %s%s", loggingLevel, sourceComponent, message,
                         (message[0] && message[strlen(message)-1] == '\n') ? "" : "\n");
}

// NVSDK_NGX_FeatureCommonInfo (zeroed, generously sized). Layout:
//   +0x00 PathListInfo.Path (const wchar_t**)
//   +0x08 PathListInfo.Length (uint)
//   +0x10 LoggingInfo.LoggingCallback (fn ptr)
//   +0x18 LoggingInfo.MinimumLoggingLevel (int)  -- 0=OFF 1=ON 2=VERBOSE
//   +0x1C LoggingInfo.DisableOtherLoggingSinks (bool)
alignas(16) unsigned char g_ngxFeatureInfo[256] = {};
void BuildFeatureInfo() {
    memset(g_ngxFeatureInfo, 0, sizeof(g_ngxFeatureInfo));
    *reinterpret_cast<void**>(g_ngxFeatureInfo + 0x10) = reinterpret_cast<void*>(&NgxLogSink);
    *reinterpret_cast<int*>(g_ngxFeatureInfo + 0x18) = 2;   // VERBOSE
}

// The snippet's Init_Ext and CreateFeature run foreign, signed code that can
// fault; each gets its own SEH frame so a crash there disables NR instead of
// taking the game down. 0xDEAD0002/3 are our sentinels for "faulted". The 5th
// arg is the FeatureCommonInfo (with our logging sink), so NGX's own reason for
// any failure lands in the log.
unsigned SafeCallInitExt(InitExtFn fn, unsigned long long app, const wchar_t* dir,
                         ID3D12Device* dev, unsigned ver) {
    __try { return fn(app, dir, dev, ver, g_ngxFeatureInfo); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0xDEAD0002u; }
}
unsigned SafeCallCreate(CreateFeatFn fn, ID3D12GraphicsCommandList* list, unsigned id,
                        void* p, void** out) {
    __try { return fn(list, id, p, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (out) *out = nullptr; return 0xDEAD0003u; }
}

// Resolve the NGX surface and Init_Ext once. Requires the game device.
void InitNeuralRender() {
    if (g_nrInited) return;
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *(slash + 1) = L'\0';
    wcsncat_s(path, L"nvngx_dlssnr.dll", _TRUNCATE);
    HMODULE snip = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (!snip) snip = LoadLibraryW(path);
    if (!snip) { TestLog("[nr] nvngx_dlssnr.dll not found at %ls\n", path); return; }

    auto initExt = reinterpret_cast<InitExtFn>(GetProcAddress(snip, "NVSDK_NGX_D3D12_Init_Ext"));
    g_nrCreate   = reinterpret_cast<CreateFeatFn>(GetProcAddress(snip, "NVSDK_NGX_D3D12_CreateFeature"));
    g_nrEvaluate = reinterpret_cast<EvalFeatFn2>(GetProcAddress(snip, "NVSDK_NGX_D3D12_EvaluateFeature"));
    for (const wchar_t* nm : kNgxModules) {
        HMODULE h = GetModuleHandleW(nm);
        if (!h) continue;
        auto a = reinterpret_cast<AllocParamsFn>(GetProcAddress(h, "NVSDK_NGX_D3D12_AllocateParameters"));
        auto d = reinterpret_cast<DestroyParamsFn>(GetProcAddress(h, "NVSDK_NGX_D3D12_DestroyParameters"));
        if (a && d) {
            g_nrAlloc = a; g_nrDestroy = d;
            // RenoDX's primary path: if this core module also exports Create/Evaluate,
            // it uses the CORE for feature 18, snippet only as a fallback.
            g_coreCreate   = reinterpret_cast<CreateFeatFn>(GetProcAddress(h, "NVSDK_NGX_D3D12_CreateFeature"));
            g_coreEvaluate = reinterpret_cast<EvalFeatFn2>(GetProcAddress(h, "NVSDK_NGX_D3D12_EvaluateFeature"));
            TestLog("[nr] core module %ls: create=%p eval=%p (snippet create=%p eval=%p)\n",
                    nm, g_coreCreate, g_coreEvaluate, g_nrCreate, g_nrEvaluate);
            break;
        }
    }
    if (!initExt || !g_nrCreate || !g_nrEvaluate || !g_nrAlloc || !g_nrDestroy) {
        TestLog("[nr] missing export: init=%p create=%p eval=%p alloc=%p destroy=%p\n",
                initExt, g_nrCreate, g_nrEvaluate, g_nrAlloc, g_nrDestroy);
        return;
    }
    ID3D12Device* dev = ::cvr::detail::g_game_device;
    if (!dev) return;
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dir, L'\\')) *slash = L'\0';
    EnsureSpoof();
    BuildFeatureInfo();   // logging sink so NGX reports its own failure reasons
    unsigned ir = SafeCallInitExt(initExt, kAppId, dir, dev, kNgxVersionApi);
    if (ir == 0xDEAD0002u) {
        CyberpunkVR_NrDisabled = 1;
        TestLog("[nr] Init_Ext FAULTED -- NR disabled.\n");
        return;
    }
    if (!NgxFailed(ir)) {
        g_nrInited = true;
        CyberpunkVR_NrInited = 1;
        TestLog("[nr] Init_Ext ok -- neural render initialised.\n");
    } else {
        TestLog("[nr] Init_Ext failed 0x%08X.\n", ir);
    }
}

// Create feature 18 + textures for a handle the first time it is seen, and KEEP
// them. No evaluate, no codec, no copy: this only measures the effect of a live
// feature 18 on the game's own DLSS output.
NrContext* NrCreateForHandle(void* handle, ID3D12Device* dev, ID3D12GraphicsCommandList* cmdList,
                             int rW, int rH, int dW, int dH) {
    std::lock_guard<std::mutex> lock(g_nrMutex);
    for (unsigned i = 0; i < g_nrN; ++i) if (g_nr[i].handle == handle) return &g_nr[i];  // existing
    if (g_nrN >= 8) return nullptr;
    NrContext& c = g_nr[g_nrN];
    c.handle = handle; c.rw = rW; c.rh = rH; c.w = dW; c.h = dH;

    // ONE parameter object for the life of the context, used for BOTH create and
    // every evaluate -- exactly as the add-on does (it stores the object at
    // feature-create and re-fills it each frame). A fresh object at evaluate time
    // was rejected with 0xBAD00005: the runtime ties the param object to the
    // feature it was created with.
    if (NgxFailed(g_nrAlloc(&c.params)) || !c.params) {
        c.broken = true; ++g_nrN; return &g_nr[g_nrN-1];
    }
    NgxParams* p = reinterpret_cast<NgxParams*>(c.params);
    // NATIVE path (Upscaling=0): Color/Output both at DISPLAY res, MVec/Depth stay
    // render-res via subrects. The add-on falls back to exactly this when the
    // upscaling color is "not shader-readable"; the upscaling contract was
    // rejected with 0xBAD00005.
    FillCreateParams(p, dW, dH, dW, dH);
    void* feat = nullptr;
    EnsureSpoof();
    // RenoDX order: try the CORE first, fall back to the snippet. Evaluate must
    // then go through the same module that created (tracked by c.viaCore).
    unsigned cr = 0xBAD0000Bu;
    if (g_coreCreate && g_coreEvaluate) {
        cr = SafeCallCreate(g_coreCreate, cmdList, kFeatureDlssNr, c.params, &feat);
        if (!NgxFailed(cr) && feat && cr != 0xDEAD0003u) c.viaCore = true;
    }
    if ((NgxFailed(cr) || !feat) && cr != 0xDEAD0003u && g_nrCreate) {
        feat = nullptr;
        cr = SafeCallCreate(g_nrCreate, cmdList, kFeatureDlssNr, c.params, &feat);
        c.viaCore = false;
    }
    CyberpunkVR_NrCreateResult = cr;
    if (cr == 0xDEAD0003u) {
        c.broken = true; ++g_nrN;
        CyberpunkVR_NrDisabled = 1;
        TestLog("[nr] CreateFeature(18) FAULTED for handle=%p -- NR disabled.\n", handle);
        return &g_nr[g_nrN-1];
    }
    if (NgxFailed(cr) || !feat) {
        c.broken = true; ++g_nrN;
        TestLog("[nr] CreateFeature(18) FAILED for handle=%p -> 0x%08X (render %dx%d -> display %dx%d)\n",
                handle, cr, rW, rH, dW, dH);
        return &g_nr[g_nrN-1];
    }
    c.nrFeature = feat;
    // Color is the render-res INPUT, Output the display-res result -- exactly the
    // two textures the add-on owns (DAT_18007f108 "SDR proxy" / DAT_18007f110
    // "raw neural output"). BOTH are R16G16B16A16_FLOAT (measured in the add-on's
    // texture creation, FUN_180019290, format 10) -- NOT R10G10B10A2, which the
    // runtime rejected with 0xBAD00005 (InvalidParameter / unsupported format).
    c.color  = MakeNrTexture(dev, dW, dH, DXGI_FORMAT_R16G16B16A16_FLOAT);   // native: display res
    c.output = MakeNrTexture(dev, dW, dH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    if (!c.color || !c.output) c.broken = true;
    c.justCreated = true;   // force Reset=1 on this handle's first eval (fresh temporal history)
    g_codec.Init(dev);   // tonemap codec for the write-back (HDR <-> SDR proxy)
    ++g_nrN;
    CyberpunkVR_NrContexts = (int)g_nrN;
    TestLog("[nr] context handle=%p feature=%p via=%s  render %dx%d -> display %dx%d%s\n",
            handle, feat, c.viaCore ? "CORE" : "snippet", rW, rH, dW, dH, c.broken ? "  BROKEN" : "");
    return &g_nr[g_nrN-1];
}

// SEH-guarded desc read on a foreign resource (render dims for MVec/Depth subrects).
bool SafeDesc(void* res, int* w, int* h) {
    __try {
        D3D12_RESOURCE_DESC d = reinterpret_cast<ID3D12Resource*>(res)->GetDesc();
        *w = (int)d.Width; *h = (int)d.Height;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// STEP 5: evaluate feature 18 at RENDER resolution into our OWN display-res
// c->output. Color input is the game's render-res Color; MVec/Depth are the
// game's render-res guides; Upscaling=1 so the transformer runs at render res
// (the fast contract). NOTHING is written back to the game's Output -- this
// measures whether render-res eval recovers the framerate while the game image
// stays exactly its own.
void RunNrEvalOnly(ID3D12GraphicsCommandList* cmdList, NrContext* c,
                   void* gameColor, void* gameMVec, void* gameDepth, void* gameOutput, bool isRight) {
    if (!c || c->broken || !c->nrFeature || !c->output || !c->color || !c->params) return;
    if (!g_nrEvaluate || !gameColor) return;

    const int dW = c->w, dH = c->h;             // display (output = color, native)
    int rW = c->rw, rH = c->rh;                 // render (MVec/Depth guide res)
    if (rW <= 0 || rH <= 0) { if (!SafeDesc(gameMVec, &rW, &rH) || rW <= 0) { rW = dW; rH = dH; } }

    // WRITE-BACK IN (ENCODE): tonemap the game's scene-linear HDR Output into our
    // SDR proxy color, so NR gets the [0,1] display-encoded input it expects
    // instead of raw HDR (which made it diverge into moving colour noise).
    ID3D12Device* cdev = nullptr;
    const bool writeBack = CyberpunkVR_NrWriteBack && gameOutput && g_codec.ready;
    if (writeBack) {
        reinterpret_cast<ID3D12Resource*>(c->color)->GetDevice(IID_PPV_ARGS(&cdev));
    }
    if (writeBack && cdev) {
        ID3D12Resource* go = reinterpret_cast<ID3D12Resource*>(gameOutput);
        Barrier(cmdList, go, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_codec.Run(cdev, cmdList, true, go, c->color, dW, dH, CyberpunkVR_NrPaperWhite, 1.0f);
        Barrier(cmdList, go, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_RESOURCE_BARRIER uav = {}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uav.UAV.pResource = c->color; cmdList->ResourceBarrier(1, &uav);
    }

    NgxParams* p = reinterpret_cast<NgxParams*>(c->params);
    // NATIVE contract: Color and Output both at DISPLAY res; only MVec/Depth are
    // the smaller render-res guides (via their subrects). Upscaling=0.
    p->SetResource("DLSSNR.Color",  c->color);
    p->SetResource("DLSSNR.Output", c->output);
    p->SetResource("DLSSNR.MVec",   gameMVec);
    p->SetResource("DLSSNR.Depth",  gameDepth);
    p->SetInt("Width", dW);              p->SetInt("Height", dH);
    p->SetInt("OutWidth", dW);           p->SetInt("OutHeight", dH);
    p->SetInt("DLSSNR.ColorSubrectBaseX", 0);   p->SetInt("DLSSNR.ColorSubrectBaseY", 0);
    p->SetInt("DLSSNR.ColorSubrectWidth", dW);  p->SetInt("DLSSNR.ColorSubrectHeight", dH);
    p->SetInt("DLSSNR.OutputSubrectBaseX", 0);  p->SetInt("DLSSNR.OutputSubrectBaseY", 0);
    p->SetInt("DLSSNR.OutputSubrectWidth", dW); p->SetInt("DLSSNR.OutputSubrectHeight", dH);
    p->SetInt("DLSSNR.DepthSubrectBaseX", 0);   p->SetInt("DLSSNR.DepthSubrectBaseY", 0);
    p->SetInt("DLSSNR.DepthSubrectWidth", rW);  p->SetInt("DLSSNR.DepthSubrectHeight", rH);
    p->SetInt("DLSSNR.MVecSubrectBaseX", 0);    p->SetInt("DLSSNR.MVecSubrectBaseY", 0);
    p->SetInt("DLSSNR.MVecSubrectWidth", rW);   p->SetInt("DLSSNR.MVecSubrectHeight", rH);
    p->SetInt("DLSSNR.InputWidth", dW);         p->SetInt("DLSSNR.InputHeight", dH);
    p->SetInt("DLSSNR.OutputWidth", dW);        p->SetInt("DLSSNR.OutputHeight", dH);
    p->SetFloat("DLSSNR.ScalingRatio", 1.0f);
    p->SetFloat("DLSSNR.Scale", 1.0f);
    p->SetUInt("DLSSNR.Upscaling", 0u);
    p->SetPtr("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ComputeScalingRatioCallback));
    // Motion-vector scale base: DLSS-D reprojects with MVec * scale; a wrong scale
    // makes the temporal history diverge into moving colour noise.
    float baseX = 1.0f, baseY = 1.0f;
    switch (CyberpunkVR_NrMVecScaleMode) {
        case 1: baseX =  (float)rW; baseY =  (float)rH; break;   // +render
        case 2: baseX = -(float)rW; baseY = -(float)rH; break;   // -render
        case 3: baseX =  (float)dW; baseY =  (float)dH; break;   // display
        default: baseX = 1.0f; baseY = 1.0f; break;              // pixel-space MVs
    }
    p->SetFloat("DLSSNR.MVecScaleX", baseX * CyberpunkVR_NrMotionScaleX);
    p->SetFloat("DLSSNR.MVecScaleY", baseY * CyberpunkVR_NrMotionScaleY);
    // Depth Conversion knob: 2 -> inverted, 1 -> not inverted, 0 -> CP2077 default
    // (reversed depth). The add-on derives this from the game's depth flags.
    const unsigned depthInv = (CyberpunkVR_NrDepthConversion == 2) ? 1u
                            : (CyberpunkVR_NrDepthConversion == 1) ? 0u : 1u;
    p->SetUInt("DLSSNR.DepthInverted", depthInv);
    p->SetUInt("DLSSNR.Enabled", 1);
    // Force a reset on this handle's very first eval: the freshly created feature
    // has no temporal history yet, so evaluating without Reset accumulates against
    // uninitialised state (matches the add-on's own create->reset-next-eval order).
    const bool forceReset = c->justCreated;
    c->justCreated = false;
    p->SetUInt("DLSSNR.Reset", (CyberpunkVR_NrReset || forceReset) ? 1u : 0u);
    p->SetFloat("DLSSNR.Intensity", CyberpunkVR_NrIntensity);
    p->SetFloat("DLSSNR.LocalToneStrength", CyberpunkVR_NrLocalTone);
    p->SetFloat("DLSSNR.LocalStructureStrength", CyberpunkVR_NrLocalStructure);
    p->SetFloat("DLSSNR.SkinStructureStrength", CyberpunkVR_NrSkinStructure);
    p->SetUInt("DLSSNR.UseAutoMask", 0);
    p->SetInt("DLSSNR.Style", CyberpunkVR_NrStyle);
    p->SetUInt("DLSSNR.UICorrection", 0);
    // The two indicator params the add-on always sets last; without them the
    // runtime returned 0xBAD00005 (InvalidParameter).
    p->SetUInt("DLSS.Indicator.Invert.X.Axis", 0);
    p->SetUInt("DLSS.Indicator.Invert.Y.Axis", 0);

    EnsureSpoof();
    // Evaluate through the SAME module that created the feature.
    EvalFeatFn2 ev = c->viaCore ? g_coreEvaluate : g_nrEvaluate;
    unsigned er = ev ? ev(cmdList, c->nrFeature, c->params, nullptr) : 0xBAD00005u;
    CyberpunkVR_NrLastResult = er;
    static unsigned s_evalLogged = 0;
    if (s_evalLogged < 6) { ++s_evalLogged;
        TestLog("[nr] eval handle=%p -> 0x%08X (render %dx%d -> display %dx%d, color=%p mv=%p dp=%p)\n",
                c->handle, er, rW, rH, dW, dH, gameColor, gameMVec, gameDepth);
    }
    if (NgxFailed(er)) return;
    if (isRight) ++CyberpunkVR_NrEvalsR; else ++CyberpunkVR_NrEvalsL;

    // WRITE-BACK OUT (DECODE): the NR result is a denoised SDR proxy; tonemap it
    // back to scene-linear HDR onto the game's Output so it is the frame
    // presented. ColorStrength blends against the game's original.
    if (writeBack && cdev) {
        ID3D12Resource* go = reinterpret_cast<ID3D12Resource*>(gameOutput);
        Barrier(cmdList, c->output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        g_codec.Run(cdev, cmdList, false, c->output, go, dW, dH,
                    CyberpunkVR_NrPaperWhite, CyberpunkVR_NrColorStrength);
        Barrier(cmdList, c->output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (cdev) cdev->Release();

    // Rate line every ~2 s: NR passes/sec tells whether the fps floor is the two
    // full-res passes (rate ~= 2x fps) or too many passes per frame.
    static uint64_t s_windowMs = 0; static unsigned s_windowEvals = 0;
    ++s_windowEvals;
    const uint64_t now = GetTickCount64();
    if (s_windowMs == 0) s_windowMs = now;
    if (now - s_windowMs >= 2000) {
        const double rate = s_windowEvals * 1000.0 / (now - s_windowMs);
        CyberpunkVR_NrPassesPerSec = (float)rate;
        TestLog("[nr] %.1f NR passes/sec (L %u R %u)\n",
                rate, CyberpunkVR_NrEvalsL, CyberpunkVR_NrEvalsR);
        s_windowMs = now; s_windowEvals = 0;
    }
}

// SEH wrapper: a fault in the NR path must not take the game down, and __try
// cannot sit in the detour (it holds C++ locks / objects).
bool RunNrEvalGuarded(ID3D12GraphicsCommandList* cmdList, NrContext* c,
                      void* col, void* mv, void* dp, void* out, bool r) {
    __try { RunNrEvalOnly(cmdList, c, col, mv, dp, out, r); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool s_logged = false;
        if (!s_logged) { s_logged = true; TestLog("[nr] eval FAULTED (SEH) -- NR path stopped.\n"); }
        return false;
    }
}



// One frame boundary shared by both eyes: incremented once per Present (see
// NgxPassthroughOnPresent), so the two eval calls of a rendered frame read the
// same parity for the alternate-eye mode.
std::atomic<uint32_t> g_nrPresentFrame{0};

uint32_t Hooked_Eval(void* cmdList, const void* handle, const void* params, void* callback) {
    const uint64_t n = g_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    CyberpunkVR_NgxPassEvals = n;
    if (g_verboseLog && (n <= 3 || (n % 1800) == 0)) {
        Log("[ngxpass] EvaluateFeature pass-through #%llu cmdList=%p handle=%p params=%p\n",
            static_cast<unsigned long long>(n), cmdList, handle, params);
    }
    // Step 2: read the bound resources read-only, logging each handle's layout
    // once. GetResource/GetDesc only -- no AddRef/Release, no writes, no NR.
    if (CyberpunkVR_NgxReadResources) {
        LogResourcesOnce(const_cast<void*>(handle), const_cast<void*>(params));
    }

    // Run the game's DLSS FIRST: its Output/MVec/Depth must be filled before NR
    // reads them, and NR work must be appended to the same still-recording list.
    const unsigned gr = g_origEval ? g_origEval(cmdList, handle, params, callback) : 0;

    // FSR3 frame-gen smoke test: independent of every NR switch below, so it can be armed and
    // measured without DLSS5-NR on at all. Reads Color/MotionVectors/Depth the same read-only way
    // NR does (no barrier touched), dispatches into a scratch texture, discards the result.
    if (CyberpunkVR_FrameGenEnable && !NgxFailed(gr) && params) {
        void* colRes = nullptr; int cw = 0, ch = 0; unsigned cfmt = 0;
        void* mv = nullptr; int mw = 0, mh = 0; unsigned mf = 0;
        void* dp = nullptr; int dw = 0, dh = 0; unsigned df = 0;
        const bool haveAll =
            ReadResDims(const_cast<void*>(params), "Color", &colRes, &cw, &ch, &cfmt) &&
            ReadResDims(const_cast<void*>(params), "MotionVectors", &mv, &mw, &mh, &mf) &&
            ReadResDims(const_cast<void*>(params), "Depth", &dp, &dw, &dh, &df) &&
            colRes && mv && dp && cw > 0 && ch > 0;
        if (haveAll) {
            ID3D12Device* dev = nullptr;
            if (SafeGetDevice(colRes, &dev) && dev) {
                // Eye index by first-seen handle order, independent of NR's own g_nr table so this
                // works whether or not NR is armed.
                static void* s_fgHandles[2] = {};
                static unsigned s_fgN = 0;
                int eyeIdx = -1;
                for (unsigned i = 0; i < s_fgN; ++i) {
                    if (s_fgHandles[i] == handle) { eyeIdx = static_cast<int>(i); break; }
                }
                if (eyeIdx < 0 && s_fgN < 2) {
                    s_fgHandles[s_fgN] = const_cast<void*>(handle);
                    eyeIdx = static_cast<int>(s_fgN);
                    ++s_fgN;
                }
                static bool s_fgInited = false;
                if (eyeIdx >= 0) {
                    if (!s_fgInited) {
                        s_fgInited = FrameGen_Init(dev, static_cast<unsigned>(cw),
                                                   static_cast<unsigned>(ch),
                                                   static_cast<unsigned>(cw),
                                                   static_cast<unsigned>(ch));
                    }
                    if (s_fgInited) {
                        static uint64_t s_lastTickMs[2] = {0, 0};
                        const uint64_t nowMs = GetTickCount64();
                        const float dtMs = (s_lastTickMs[eyeIdx] != 0)
                            ? static_cast<float>(nowMs - s_lastTickMs[eyeIdx]) : 16.6f;
                        s_lastTickMs[eyeIdx] = nowMs;
                        // Reuses the DLSS-NR depth-convention default (CP2077 ships reversed
                        // depth) and its motion-vector scale controls, so the two features agree
                        // on units without a second set of knobs yet.
                        const bool invertedDepth = (CyberpunkVR_NrDepthConversion != 1);
                        const float mvBase = static_cast<float>(cw);  // "+render" base, MVecScaleMode default
                        const bool ok = FrameGen_DispatchSmokeTest(
                            reinterpret_cast<ID3D12GraphicsCommandList*>(cmdList), eyeIdx,
                            reinterpret_cast<ID3D12Resource*>(colRes),
                            reinterpret_cast<ID3D12Resource*>(dp),
                            reinterpret_cast<ID3D12Resource*>(mv), cw, ch,
                            mvBase * CyberpunkVR_NrMotionScaleX, mvBase * CyberpunkVR_NrMotionScaleY,
                            invertedDepth,
                            /*cameraNearMeters=*/0.1f, /*cameraFarMeters=*/2000.0f,
                            /*cameraFovYRadians=*/1.39626f /* ~80 deg, placeholder */, dtMs,
                            /*reset=*/false);
                        ++CyberpunkVR_FrameGenDispatches;
                        CyberpunkVR_FrameGenLastResult = ok ? 0 : 1;
                    }
                }
                dev->Release();
            }
        }
    }

    // Step 3 (create+keep) and step 4a (evaluate into our own texture). The game's
    // Output is NEVER written here -- eval writes only c->output, which is then
    // discarded, so the presented image stays exactly the game's own.
    if (CyberpunkVR_NrCreate && !CyberpunkVR_NrDisabled && !NgxFailed(gr) && params) {
        InitNeuralRender();
        if (g_nrInited && g_nrCreate && g_nrEvaluate && g_nrAlloc) {
            void* outRes = nullptr; int ow = 0, oh = 0; unsigned ofmt = 0;
            void* colRes = nullptr; int cw = 0, ch = 0; unsigned cfmt = 0;
            const bool haveOut = ReadResDims(const_cast<void*>(params), "Output",
                                             &outRes, &ow, &oh, &ofmt) && outRes && ow > 0 && oh > 0;
            const bool haveCol = ReadResDims(const_cast<void*>(params), "Color",
                                             &colRes, &cw, &ch, &cfmt) && colRes && cw > 0 && ch > 0;
            if (haveOut && haveCol) {
                ID3D12Device* dev = nullptr;
                if (SafeGetDevice(outRes, &dev) && dev) {
                    // render (Color) -> display (Output).
                    NrContext* c = NrCreateForHandle(
                        const_cast<void*>(handle), dev,
                        reinterpret_cast<ID3D12GraphicsCommandList*>(cmdList), cw, ch, ow, oh);
                    dev->Release();
                    static unsigned s_branchLogged = 0;
                    if (s_branchLogged < 4) { ++s_branchLogged;
                        TestLog("[nr] eval branch: NrEval=%d c=%p broken=%d\n",
                                CyberpunkVR_NrEval, (void*)c, c ? (int)c->broken : -1);
                    }
                    if (CyberpunkVR_NrEval && c && !c->broken) {
                        void* mv = nullptr; void* dp = nullptr;
                        int mw = 0, mh = 0, dw = 0, dh = 0; unsigned mf = 0, df = 0;
                        ReadResDims(const_cast<void*>(params), "MotionVectors", &mv, &mw, &mh, &mf);
                        ReadResDims(const_cast<void*>(params), "Depth", &dp, &dw, &dh, &df);
                        const bool isRight = (c >= &g_nr[1]);   // eye by creation order
                        // ALTERNATE EYES: denoise only one eye per frame -- left on even
                        // frames, right on odd -- so a frame does half the NR work. The
                        // skipped eye keeps the game's own image this frame.
                        bool doThisEye = true;
                        if (CyberpunkVR_NrAlternateEyes) {
                            const uint32_t f = g_nrPresentFrame.load(std::memory_order_relaxed);
                            doThisEye = (isRight == ((f & 1u) != 0u));
                        }
                        if (doThisEye &&
                            !RunNrEvalGuarded(reinterpret_cast<ID3D12GraphicsCommandList*>(cmdList),
                                              c, colRes, mv, dp, outRes, isRight)) {
                            CyberpunkVR_NrLastResult = 0xDEAD0001;  // fault -> path stops for safety
                            CyberpunkVR_NrDisabled = 1;             // latch: stop trying, no crash loop
                        }
                    }
                }
            }
        }
    }
    return gr;
}

std::atomic<bool> g_armed{false};

}  // namespace

void NgxPassthroughOnPresent() {
    g_nrPresentFrame.fetch_add(1, std::memory_order_relaxed);
}

bool NgxPassthroughTryArm() {
    if (g_armed.load(std::memory_order_acquire)) return true;
    if (CyberpunkVR_NgxPassthrough == 0) return false;  // disabled: keep retrying only if re-enabled

    void* target = nullptr;
    const wchar_t* found = nullptr;
    for (const wchar_t* nm : kNgxModules) {
        HMODULE h = GetModuleHandleW(nm);
        if (!h) continue;
        target = reinterpret_cast<void*>(GetProcAddress(h, "NVSDK_NGX_D3D12_EvaluateFeature"));
        if (target) { found = nm; break; }
    }
    if (!target) return false;  // NGX not loaded yet; caller retries next frame

    // MH_Initialize is already done by the plugin bootstrap; tolerate either.
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        Log("[ngxpass] MH_Initialize failed %d\n", static_cast<int>(st));
        return false;
    }
    if (MH_CreateHook(target, reinterpret_cast<void*>(&Hooked_Eval),
                      reinterpret_cast<void**>(&g_origEval)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("[ngxpass] failed to hook NVSDK_NGX_D3D12_EvaluateFeature at %p\n", target);
        g_origEval = nullptr;
        return false;
    }
    g_armed.store(true, std::memory_order_release);
    CyberpunkVR_NgxPassArmed = 1;
    Log("[ngxpass] pass-through hook armed on %ls!NVSDK_NGX_D3D12_EvaluateFeature target=%p\n",
        found, target);
    return true;
}
