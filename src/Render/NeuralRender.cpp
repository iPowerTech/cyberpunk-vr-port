// NeuralRender -- DLSS 5's neural-render pass on each eye after the game's DLSS (include/Render/NeuralRender.hpp).
//
// WHERE IT RIDES. A MinHook on the core NGX dispatcher, _nvngx.dll!NVSDK_NGX_D3D12_EvaluateFeature: Streamline's
// DLSS reaches every evaluate through it. When the game's evaluate returns, its Output, MotionVectors and Depth are
// filled and the command list is still recording, so NR and its colour codec are appended to the same list. The
// hook goes in only once the switch is on; before that nothing here runs.
//
// PER EYE. Under engine stereo DLSS runs once per eye (Stereo/EngineStereoDlss.cpp): the left eye writes its half of
// the side-by-side output, the right eye a target of its own that is copied into its half afterwards. EvaluatingEye
// and EvaluatingAreas say which eye it is and where its output, depth and motion are in the resources NGX was given,
// so NR reads and writes that eye's area only, with one NR feature (one history) per DLSS handle, and so per eye. An
// evaluate under engine stereo outside an eye's (another feature, the per-eye split off) is left alone; without
// engine stereo the whole resources are the one picture.
//
// THE SNIPPET. nvngx_dlssnr.dll vets its caller's module name and accepts only one ending in nvngx.dll. The hooked
// GetModuleFileNameW answers that for this plugin's module, and only on a thread inside one of our own NGX calls --
// anything else asking for this module's name (RED4ext, a crash report, our own paths) gets the real one.
//
// COLOUR. The model wants a display-encoded [0,1] input; the game's Output is scene-linear HDR (R11G11B10 float on
// 2.5, RGBA16F before). So the eye's output is tonemapped (Reinhard, sRGB) into an SDR proxy before NR and decoded
// back after, with the result clamped
// against the original: a model output blown up toward infinity or collapsed to black written onto Output frame
// after frame is what a driver eventually reports as a GPU hang.
//
// FAULTS STOP IT. Init, create and evaluate run foreign signed code in SEH frames; a fault, a missing snippet or an
// output this codec cannot read latches NR off with the reason (CyberpunkVR_NrFault) until the menu's Retry.
//
// The vtable offsets of NGX's parameter object and the NR parameter names are the ones measured from CP2077 and the
// RenoDX DLSS 5 add-on (the fork's DLSS5-NR, 2026-09); the NGX SDK is not a dependency.
#include "Render/NeuralRender.hpp"
#include "Stereo/EngineStereo.hpp"
#include "Stereo/EngineStereoDlss.hpp"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <MinHook.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>

extern void Log(const char* fmt, ...);

extern "C" {
// THE SWITCH, live from the DLSS5-NR page. Not saved: a build that crashes with it on cannot crash the next start.
__declspec(dllexport) int32_t CyberpunkVR_NrEnable = 0;
// Model parameters, the names of the RenoDX add-on's panel; -1 = the model decides.
__declspec(dllexport) int32_t CyberpunkVR_NrPreset = 0;
__declspec(dllexport) int32_t CyberpunkVR_NrStyle = 1;              // 0 natural, 1 cinematic
__declspec(dllexport) float   CyberpunkVR_NrIntensity = 2.0f;
__declspec(dllexport) float   CyberpunkVR_NrLocalTone = 2.0f;
__declspec(dllexport) float   CyberpunkVR_NrLocalStructure = 2.0f;
__declspec(dllexport) float   CyberpunkVR_NrSkinStructure = -1.0f;
// The codec: the scene's paper white before the tonemap, and how much of the NR result replaces the original.
__declspec(dllexport) float   CyberpunkVR_NrPaperWhite = 1.0f;
__declspec(dllexport) float   CyberpunkVR_NrColorStrength = 1.0f;
// Motion and depth. Scale base: 0 = the vectors as they are, 1 = the eye's DLSS scale times the render size (what
// DLSS itself gets), 2 = the same negated, 3 = the output size; the multipliers fine-tune it. Depth: 0 = the game's
// (reversed), 1 = not inverted, 2 = inverted. Reset = no temporal history at all (a diagnostic).
__declspec(dllexport) int32_t CyberpunkVR_NrMVecScaleMode = 1;
__declspec(dllexport) float   CyberpunkVR_NrMotionScaleX = 1.0f;
__declspec(dllexport) float   CyberpunkVR_NrMotionScaleY = 1.0f;
__declspec(dllexport) int32_t CyberpunkVR_NrDepthConversion = 0;
__declspec(dllexport) int32_t CyberpunkVR_NrReset = 0;
// One eye per frame (left on even Presents, right on odd): half the NR work, each eye's history at half rate.
__declspec(dllexport) int32_t CyberpunkVR_NrAlternateEyes = 0;
// Status. Fault: 0 none, 1 nvngx_dlssnr.dll missing, 2 an NGX export missing, 3 Init_Ext failed, 4 Init_Ext faulted,
// 5 CreateFeature faulted, 6 the evaluate faulted, 7 an output format the codec does not read, 8 the hook would not
// go in. Disabled = a fault stopped NR (the menu's Retry clears both).
__declspec(dllexport) int32_t  CyberpunkVR_NrArmed = 0;
__declspec(dllexport) int32_t  CyberpunkVR_NrInited = 0;
__declspec(dllexport) int32_t  CyberpunkVR_NrContexts = 0;
__declspec(dllexport) int32_t  CyberpunkVR_NrDisabled = 0;
__declspec(dllexport) int32_t  CyberpunkVR_NrFault = 0;
__declspec(dllexport) uint32_t CyberpunkVR_NrCreateResult = 0;
__declspec(dllexport) uint32_t CyberpunkVR_NrLastResult = 0;
__declspec(dllexport) uint32_t CyberpunkVR_NrEvals[2] = {};   // NR passes, left / right (or the one picture)
__declspec(dllexport) float    CyberpunkVR_NrPassesPerSec = 0.0f;
}

namespace cvr::neural_render {
namespace {

// Inside one of our own NGX calls on this thread: the evaluate hook passes it straight through (our NR evaluate goes
// through the hooked core function too), and GetModuleFileNameW names this module nvngx.dll for the snippet.
thread_local bool t_ours = false;

void Fault(int reason) { CyberpunkVR_NrFault = reason; CyberpunkVR_NrDisabled = 1; }

// Our own trace, out of the plugin log: bin\x64\cyberpunkvrport_ngx.txt.
void TestLog(const char* fmt, ...) {
    FILE* f = nullptr;
    if (fopen_s(&f, "cyberpunkvrport_ngx.txt", "a") != 0 || !f) return;
    va_list args; va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

using EvalFeatFn      = unsigned (*)(void* cmdList, const void* handle, const void* params, void* callback);
using InitExtFn       = unsigned (__stdcall*)(unsigned long long, const wchar_t*, ID3D12Device*, unsigned, const void*);
using AllocParamsFn   = unsigned (__stdcall*)(void**);
using CreateFeatFn    = unsigned (__stdcall*)(ID3D12GraphicsCommandList*, unsigned, void*, void**);
using EvalFeatFn2     = unsigned (__stdcall*)(ID3D12GraphicsCommandList*, void*, void*, void*);
using GetModFileNameFn = DWORD (WINAPI*)(HMODULE, LPWSTR, DWORD);

constexpr unsigned           kNgxVersionApi = 0x15;
constexpr unsigned long long kAppId = 0x876232C;
constexpr unsigned           kFeatureDlssNr = 18;
// The core NGX modules. The per-feature DLLs (nvngx_dlss.dll, ...) are not the dispatcher -- Render/FoveatedDlss.cpp
// hooks nvngx_dlss.dll's own evaluate -- and are never touched here.
const wchar_t* const kNgxModules[] = {L"_nvngx.dll", L"nvngx.dll"};

bool NgxFailed(unsigned r) { return (r & 0xFFF00000u) == 0xBAD00000u; }

// NGX's parameter object, by its measured vtable (CP2077 + Streamline), not the SDK's declaration order. POD.
struct NgxParams {
    void* vtbl;
    template <class Fn> Fn At(size_t offset) { return *reinterpret_cast<Fn*>(reinterpret_cast<uint8_t*>(vtbl) + offset); }
    void SetPtr(const char* n, void* v) { At<void (__fastcall*)(void*, const char*, void*)>(0x00)(this, n, v); }
    void SetResource(const char* n, void* r) { At<void (__fastcall*)(void*, const char*, void*)>(0x08)(this, n, r); }
    void SetUInt(const char* n, unsigned v) { At<void (__fastcall*)(void*, const char*, unsigned)>(0x18)(this, n, v); }
    void SetInt(const char* n, int v) { At<void (__fastcall*)(void*, const char*, int)>(0x20)(this, n, v); }
    void SetFloat(const char* n, float v) { At<void (__fastcall*)(void*, const char*, float)>(0x30)(this, n, v); }
    void* GetResource(const char* n) { void* out = nullptr; At<unsigned (__fastcall*)(void*, const char*, void**)>(0x48)(this, n, &out); return out; }
    unsigned GetUInt(const char* n, unsigned* out) { return At<unsigned (__fastcall*)(void*, const char*, unsigned*)>(0x58)(this, n, out); }
    unsigned GetInt(const char* n, int* out) { return At<unsigned (__fastcall*)(void*, const char*, int*)>(0x60)(this, n, out); }
};

// One bound resource of the game's evaluate and its size and format, read on a foreign object in an SEH frame. NGX's
// getter returns the stored pointer without a reference, and nothing is released.
bool ReadResource(const void* params, const char* key, ID3D12Resource** res, D3D12_RESOURCE_DESC* desc) {
    __try {
        auto* r = static_cast<ID3D12Resource*>(reinterpret_cast<NgxParams*>(const_cast<void*>(params))->GetResource(key));
        if (!r) return false;
        *desc = r->GetDesc();
        *res = r;
        return desc->Width && desc->Height;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SafeGetDevice(ID3D12Resource* res, ID3D12Device** out) {
    __try { return SUCCEEDED(res->GetDevice(IID_PPV_ARGS(out))); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The runtime asks this while it decides how much to scale (the add-on's callback).
unsigned __fastcall ComputeScalingRatioCallback(NgxParams* p) {
    if (!p) return 0xBAD00005u;
    unsigned upscaling = 0;
    if (NgxFailed(p->GetUInt("DLSSNR.Upscaling", &upscaling))) upscaling = 0;
    int inW = 0, outW = 0;
    if (NgxFailed(p->GetInt("DLSSNR.InputWidth", &inW))) inW = 0;
    if (NgxFailed(p->GetInt("DLSSNR.OutputWidth", &outW))) outW = 0;
    p->SetFloat("DLSSNR.ScalingRatio", upscaling && inW && outW ? float(inW) / float(outW) : 1.0f);
    return 1;
}

// ---- the caller-name answer for the snippet -----------------------------------------------------------------------
GetModFileNameFn g_realGetModFileName = nullptr;
HMODULE g_ourModule = nullptr;
std::atomic<bool> g_spoofArmed{false};

DWORD WINAPI HookedGetModuleFileNameW(HMODULE module, LPWSTR out, DWORD size) {
    if (t_ours && out && size >= 10 && g_ourModule && module == g_ourModule) {
        static const wchar_t kName[] = L"nvngx.dll";
        memcpy(out, kName, sizeof(kName));
        return 9;
    }
    return g_realGetModFileName(module, out, size);
}

// Put in once and left in: enabling and disabling a MinHook suspends every thread of the process.
void EnsureSpoof() {
    if (g_spoofArmed.load(std::memory_order_acquire)) return;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&HookedGetModuleFileNameW), &g_ourModule);
    void* target = reinterpret_cast<void*>(&GetModuleFileNameW);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedGetModuleFileNameW),
                      reinterpret_cast<void**>(&g_realGetModFileName)) == MH_OK && MH_EnableHook(target) == MH_OK) {
        g_spoofArmed.store(true, std::memory_order_release);
        TestLog("[nr] caller-name answer armed.\n");
    } else {
        TestLog("[nr] caller-name answer could not be armed.\n");
    }
}

// ---- the colour codec ---------------------------------------------------------------------------------------------
// Encode: the eye's area of the game's Output (at Off) -> the SDR proxy at (0, 0). Decode: the NR result at (0, 0) ->
// back onto the game's Output at Off, blended by ColorStrength.
const char* const kCodecHLSL = R"HLSL(
cbuffer K : register(b0) { uint W; uint H; float PaperWhite; float ColorStrength; uint OffX; uint OffY; };
Texture2D<float4>   Src : register(t0);
RWTexture2D<float4> Dst : register(u0);

float3 SrgbEncode(float3 c){ c=saturate(c); return (c<=0.0031308)?(c*12.92):(1.055*pow(c,1.0/2.4)-0.055); }
float3 SrgbDecode(float3 c){ return (c<=0.04045)?(c/12.92):pow((c+0.055)/1.055,2.4); }

[numthreads(8,8,1)]
void Encode(uint3 id : SV_DispatchThreadID) {
    if (id.x >= W || id.y >= H) return;
    float pw = max(1e-4, PaperWhite);
    float3 c = max(0.0.xxx, Src[id.xy + uint2(OffX, OffY)].rgb) / pw;
    c = c / (1.0 + c);                       // Reinhard -> [0,1)
    Dst[id.xy] = float4(SrgbEncode(c), 1.0);
}

[numthreads(8,8,1)]
void Decode(uint3 id : SV_DispatchThreadID) {
    if (id.x >= W || id.y >= H) return;
    uint2 g = id.xy + uint2(OffX, OffY);
    float pw = max(1e-4, PaperWhite);
    float3 n = SrgbDecode(saturate(Src[id.xy].rgb));
    float4 o = Dst[g];                                  // still the game's own pixel
    // A proxy collapsed to black keeps the game's pixel.
    float ny = dot(n, float3(0.2126, 0.7152, 0.0722));
    if (ny <= 1e-5) return;
    float3 c = (n / max(1e-4, 1.0 - n)) * pw;          // inverse Reinhard
    // Near n = 1 the inverse explodes: the luma ratio to the original is held within [0, 4], chroma kept.
    float oy = dot(max(0.0.xxx, o.rgb), float3(0.2126, 0.7152, 0.0722));
    float cy = dot(max(0.0.xxx, c), float3(0.2126, 0.7152, 0.0722));
    if (oy > 1e-5 && cy > 1e-5) c *= (clamp(cy / oy, 0.0, 4.0) * oy) / cy;
    Dst[g] = float4(lerp(o.rgb, c, saturate(ColorStrength)), o.a);
}
)HLSL";

struct Codec {
    ID3D12RootSignature*  rootSig = nullptr;
    ID3D12PipelineState*  encode = nullptr;
    ID3D12PipelineState*  decode = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT descSize = 0, ring = 0;
    static constexpr UINT kRing = 256;   // two descriptors a pass, four passes a frame: reused after 32 frames
    bool ready = false, tried = false;

    bool Compile(const char* entry, ID3DBlob** out) {
        ID3DBlob* err = nullptr;
        const HRESULT hr = D3DCompile(kCodecHLSL, strlen(kCodecHLSL), nullptr, nullptr, nullptr, entry, "cs_5_0", 0, 0, out, &err);
        if (FAILED(hr)) TestLog("[nr] codec %s compile 0x%08X %.*s\n", entry, hr, err ? int(err->GetBufferSize()) : 0,
                                err ? static_cast<const char*>(err->GetBufferPointer()) : "");
        if (err) err->Release();
        return SUCCEEDED(hr);
    }

    bool Init(ID3D12Device* dev) {
        if (ready || tried) return ready;
        tried = true;
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 1;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;
        D3D12_ROOT_PARAMETER params[2] = {};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 6;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 2;
        params[1].DescriptorTable.pDescriptorRanges = ranges;
        D3D12_ROOT_SIGNATURE_DESC rs = {}; rs.NumParameters = 2; rs.pParameters = params;
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
        if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) {
            if (err) err->Release();
            TestLog("[nr] codec root signature failed\n");
            return false;
        }
        const HRESULT hr = dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig));
        blob->Release();
        if (FAILED(hr)) { TestLog("[nr] codec CreateRootSignature 0x%08X\n", hr); return false; }
        ID3DBlob* enc = nullptr; ID3DBlob* dec = nullptr;
        if (!Compile("Encode", &enc) || !Compile("Decode", &dec)) {
            if (enc) enc->Release();
            if (dec) dec->Release();
            return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = rootSig;
        pd.CS = {enc->GetBufferPointer(), enc->GetBufferSize()};
        const HRESULT h1 = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&encode));
        pd.CS = {dec->GetBufferPointer(), dec->GetBufferSize()};
        const HRESULT h2 = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&decode));
        enc->Release(); dec->Release();
        if (FAILED(h1) || FAILED(h2)) { TestLog("[nr] codec PSO failed\n"); return false; }
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kRing;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)))) { TestLog("[nr] codec heap failed\n"); return false; }
        descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ready = true;
        TestLog("[nr] codec ready\n");
        return true;
    }

    // One pass, src (SRV) -> dst (UAV), each viewed in its own format; the caller has them in those states.
    void Run(ID3D12Device* dev, ID3D12GraphicsCommandList* list, bool enc, ID3D12Resource* src, DXGI_FORMAT srcFormat,
             ID3D12Resource* dst, DXGI_FORMAT dstFormat, UINT w, UINT h, UINT offX, UINT offY, float paperWhite,
             float colorStrength) {
        const UINT slot = (ring % (kRing / 2)) * 2; ++ring;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = heap->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += SIZE_T(slot) * descSize;
        gpu.ptr += UINT64(slot) * descSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = srcFormat; sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(src, &sv, cpu);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu2 = cpu; cpu2.ptr += descSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = dstFormat; uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        dev->CreateUnorderedAccessView(dst, nullptr, &uv, cpu2);
        ID3D12DescriptorHeap* heaps[] = {heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(rootSig);
        list->SetPipelineState(enc ? encode : decode);
        UINT k[6] = {w, h, 0, 0, offX, offY};
        memcpy(&k[2], &paperWhite, 4); memcpy(&k[3], &colorStrength, 4);
        list->SetComputeRoot32BitConstants(0, 6, k, 0);
        list->SetComputeRootDescriptorTable(1, gpu);
        list->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    }
};
Codec g_codec;

void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    list->ResourceBarrier(1, &b);
}

// ---- NGX: init once, one NR feature per DLSS handle ---------------------------------------------------------------
AllocParamsFn g_alloc = nullptr;
CreateFeatFn  g_snippetCreate = nullptr, g_coreCreate = nullptr;
EvalFeatFn2   g_snippetEvaluate = nullptr, g_coreEvaluate = nullptr;
bool          g_inited = false;

void __cdecl NgxLogSink(const char* message, int level, int component) {
    if (message) TestLog("[ngx] (%d/%d) %s%s", level, component, message,
                         (message[0] && message[strlen(message) - 1] == '\n') ? "" : "\n");
}

// NVSDK_NGX_FeatureCommonInfo, zeroed and generously sized: +0x10 the logging callback, +0x18 its level (1 = on).
alignas(16) unsigned char g_featureInfo[256] = {};

unsigned SafeInitExt(InitExtFn fn, const wchar_t* dir, ID3D12Device* dev) {
    unsigned r = 0xDEAD0002u;
    t_ours = true;
    __try { r = fn(kAppId, dir, dev, kNgxVersionApi, g_featureInfo); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = 0xDEAD0002u; }
    t_ours = false;
    return r;
}

unsigned SafeCreate(CreateFeatFn fn, ID3D12GraphicsCommandList* list, void* params, void** out) {
    unsigned r = 0xDEAD0003u;
    t_ours = true;
    __try { r = fn(list, kFeatureDlssNr, params, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *out = nullptr; r = 0xDEAD0003u; }
    t_ours = false;
    return r;
}

// The snippet from the game's folder, the core's parameter allocator (and its create / evaluate, preferred as the
// add-on does), then Init_Ext with our logging sink. Once; a failure latches the fault.
void Init(ID3D12Device* dev) {
    if (g_inited) return;
    wchar_t dir[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(dir, L'\\')) *slash = L'\0';
    wchar_t path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\nvngx_dlssnr.dll", dir);
    HMODULE snippet = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (!snippet) snippet = LoadLibraryW(path);
    if (!snippet) { TestLog("[nr] nvngx_dlssnr.dll not found at %ls\n", path); Fault(1); return; }
    auto initExt = reinterpret_cast<InitExtFn>(GetProcAddress(snippet, "NVSDK_NGX_D3D12_Init_Ext"));
    g_snippetCreate = reinterpret_cast<CreateFeatFn>(GetProcAddress(snippet, "NVSDK_NGX_D3D12_CreateFeature"));
    g_snippetEvaluate = reinterpret_cast<EvalFeatFn2>(GetProcAddress(snippet, "NVSDK_NGX_D3D12_EvaluateFeature"));
    for (const wchar_t* name : kNgxModules) {
        HMODULE core = GetModuleHandleW(name);
        auto alloc = core ? reinterpret_cast<AllocParamsFn>(GetProcAddress(core, "NVSDK_NGX_D3D12_AllocateParameters")) : nullptr;
        if (!alloc) continue;
        g_alloc = alloc;
        g_coreCreate = reinterpret_cast<CreateFeatFn>(GetProcAddress(core, "NVSDK_NGX_D3D12_CreateFeature"));
        g_coreEvaluate = reinterpret_cast<EvalFeatFn2>(GetProcAddress(core, "NVSDK_NGX_D3D12_EvaluateFeature"));
        TestLog("[nr] core %ls: create=%p eval=%p (snippet create=%p eval=%p)\n", name, g_coreCreate, g_coreEvaluate,
                g_snippetCreate, g_snippetEvaluate);
        break;
    }
    if (!initExt || !g_snippetCreate || !g_snippetEvaluate || !g_alloc) {
        TestLog("[nr] missing export: init=%p create=%p eval=%p alloc=%p\n", initExt, g_snippetCreate, g_snippetEvaluate, g_alloc);
        Fault(2);
        return;
    }
    EnsureSpoof();
    memset(g_featureInfo, 0, sizeof(g_featureInfo));
    *reinterpret_cast<void**>(g_featureInfo + 0x10) = reinterpret_cast<void*>(&NgxLogSink);
    *reinterpret_cast<int*>(g_featureInfo + 0x18) = 1;
    const unsigned r = SafeInitExt(initExt, dir, dev);
    if (r == 0xDEAD0002u) { TestLog("[nr] Init_Ext FAULTED\n"); Fault(4); return; }
    if (NgxFailed(r)) { TestLog("[nr] Init_Ext failed 0x%08X\n", r); Fault(3); return; }
    g_inited = true;
    CyberpunkVR_NrInited = 1;
    TestLog("[nr] Init_Ext ok\n");
}

ID3D12Resource* MakeTexture(ID3D12Device* dev, UINT w, UINT h) {
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                            IID_PPV_ARGS(&res)))) return nullptr;
    return res;
}

// One NR feature, its two textures (the SDR proxy in, the NR result out, both the eye's output size) and the one
// parameter object it was created with: the runtime ties the object to its feature, a fresh one at evaluate is
// refused (0xBAD00005).
struct Context {
    const void*     handle = nullptr;
    void*           feature = nullptr;
    void*           params = nullptr;
    ID3D12Resource* color = nullptr;
    ID3D12Resource* output = nullptr;
    UINT            w = 0, h = 0;
    bool            broken = false, viaCore = false, fresh = false;   // fresh: its first evaluate resets the history
};
std::mutex g_contextMutex;
Context    g_contexts[8];
unsigned   g_contextCount = 0;

void FillCreateParams(NgxParams* p, int w, int h) {
    // NATIVE: colour and output at the same size, Upscaling 0 -- the upscaling contract was refused (0xBAD00005).
    p->SetInt("Width", w);                 p->SetInt("Height", h);
    p->SetInt("OutWidth", w);              p->SetInt("OutHeight", h);
    p->SetInt("DLSSNR.Width", w);          p->SetInt("DLSSNR.Height", h);
    p->SetInt("DLSSNR.InputWidth", w);     p->SetInt("DLSSNR.InputHeight", h);
    p->SetInt("DLSSNR.OutputWidth", w);    p->SetInt("DLSSNR.OutputHeight", h);
    p->SetInt("DLSSNR.Output.Width", w);   p->SetInt("DLSSNR.Output.Height", h);
    p->SetFloat("DLSSNR.ScalingRatio", 1.0f);
    p->SetFloat("DLSSNR.Scale", 1.0f);
    p->SetUInt("DLSSNR.Upscaling", 0u);
    p->SetPtr("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ComputeScalingRatioCallback));
    p->SetInt("DLSSNR.Hint.Render.Preset", CyberpunkVR_NrPreset);
    p->SetUInt("PerfQualityValue", 0);
    p->SetUInt("DLSS.Feature.Create.Flags", 0x4B);   // the add-on's
    p->SetInt("CreationNodeMask", 1);
    p->SetInt("VisibilityNodeMask", 1);
}

Context* ContextFor(const void* handle, ID3D12Device* dev, ID3D12GraphicsCommandList* list, UINT w, UINT h) {
    std::lock_guard lock(g_contextMutex);
    for (unsigned i = 0; i < g_contextCount; ++i) if (g_contexts[i].handle == handle) return &g_contexts[i];
    if (g_contextCount >= 8) return nullptr;
    Context& c = g_contexts[g_contextCount++];
    c.handle = handle; c.w = w; c.h = h;
    if (NgxFailed(g_alloc(&c.params)) || !c.params) { c.broken = true; return &c; }
    FillCreateParams(static_cast<NgxParams*>(c.params), int(w), int(h));
    unsigned r = 0xBAD0000Bu;
    void* feature = nullptr;
    if (g_coreCreate && g_coreEvaluate) {
        r = SafeCreate(g_coreCreate, list, c.params, &feature);
        c.viaCore = r != 0xDEAD0003u && !NgxFailed(r) && feature;
    }
    if (!c.viaCore && r != 0xDEAD0003u) {
        feature = nullptr;
        r = SafeCreate(g_snippetCreate, list, c.params, &feature);
    }
    CyberpunkVR_NrCreateResult = r;
    if (r == 0xDEAD0003u) { c.broken = true; Fault(5); TestLog("[nr] CreateFeature(18) FAULTED (handle %p)\n", handle); return &c; }
    if (NgxFailed(r) || !feature) { c.broken = true; TestLog("[nr] CreateFeature(18) failed 0x%08X (handle %p, %ux%u)\n", r, handle, w, h); return &c; }
    c.feature = feature;
    c.color = MakeTexture(dev, w, h);
    c.output = MakeTexture(dev, w, h);
    c.broken = !c.color || !c.output || !g_codec.Init(dev);
    c.fresh = true;
    CyberpunkVR_NrContexts = int(g_contextCount);
    TestLog("[nr] context handle %p feature %p via %s, %ux%u%s\n", handle, feature, c.viaCore ? "core" : "snippet", w, h,
            c.broken ? " BROKEN" : "");
    return &c;
}

// ---- one eye's pass -----------------------------------------------------------------------------------------------
struct Job {
    ID3D12GraphicsCommandList* list;
    ID3D12Device* dev;
    ID3D12Resource* output;   // the game's DLSS output; the eye's area at (outX, outY), the context's size
    DXGI_FORMAT format;       // ...and its format (CodecReads)
    UINT outX, outY;
    ID3D12Resource* motion; cvr::engine_stereo::dlss::EyeArea motionArea;
    ID3D12Resource* depth;  cvr::engine_stereo::dlss::EyeArea depthArea;
    float motionScale[2];     // the eye's DLSS scale, 1 without engine stereo
};

// Encode the eye into the proxy, NR the proxy into our output, decode that back onto the eye. No C++ objects here: it
// runs in RunGuarded's SEH frame.
void Run(Context* c, const Job& j) {
    const UINT w = c->w, h = c->h;
    Barrier(j.list, j.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g_codec.Run(j.dev, j.list, true, j.output, j.format, c->color, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, j.outX, j.outY,
                CyberpunkVR_NrPaperWhite, 1.0f);
    Barrier(j.list, j.output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_RESOURCE_BARRIER uav = {}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = c->color;
    j.list->ResourceBarrier(1, &uav);

    auto* p = static_cast<NgxParams*>(c->params);
    p->SetResource("DLSSNR.Color",  c->color);
    p->SetResource("DLSSNR.Output", c->output);
    p->SetResource("DLSSNR.MVec",   j.motion);
    p->SetResource("DLSSNR.Depth",  j.depth);
    p->SetInt("Width", int(w));               p->SetInt("Height", int(h));
    p->SetInt("OutWidth", int(w));            p->SetInt("OutHeight", int(h));
    p->SetInt("DLSSNR.ColorSubrectBaseX", 0);  p->SetInt("DLSSNR.ColorSubrectBaseY", 0);
    p->SetInt("DLSSNR.ColorSubrectWidth", int(w));  p->SetInt("DLSSNR.ColorSubrectHeight", int(h));
    p->SetInt("DLSSNR.OutputSubrectBaseX", 0); p->SetInt("DLSSNR.OutputSubrectBaseY", 0);
    p->SetInt("DLSSNR.OutputSubrectWidth", int(w)); p->SetInt("DLSSNR.OutputSubrectHeight", int(h));
    p->SetInt("DLSSNR.DepthSubrectBaseX", int(j.depthArea.left));   p->SetInt("DLSSNR.DepthSubrectBaseY", int(j.depthArea.top));
    p->SetInt("DLSSNR.DepthSubrectWidth", int(j.depthArea.width));  p->SetInt("DLSSNR.DepthSubrectHeight", int(j.depthArea.height));
    p->SetInt("DLSSNR.MVecSubrectBaseX", int(j.motionArea.left));   p->SetInt("DLSSNR.MVecSubrectBaseY", int(j.motionArea.top));
    p->SetInt("DLSSNR.MVecSubrectWidth", int(j.motionArea.width));  p->SetInt("DLSSNR.MVecSubrectHeight", int(j.motionArea.height));
    p->SetInt("DLSSNR.InputWidth", int(w));    p->SetInt("DLSSNR.InputHeight", int(h));
    p->SetInt("DLSSNR.OutputWidth", int(w));   p->SetInt("DLSSNR.OutputHeight", int(h));
    p->SetFloat("DLSSNR.ScalingRatio", 1.0f);
    p->SetFloat("DLSSNR.Scale", 1.0f);
    p->SetUInt("DLSSNR.Upscaling", 0u);
    p->SetPtr("DLSSNRComputeScalingRatioCallback", reinterpret_cast<void*>(&ComputeScalingRatioCallback));
    // A wrong motion scale is moving colour noise: the history reprojects with MVec * scale, in pixels.
    float baseX = 1.0f, baseY = 1.0f;
    const float renderX = j.motionScale[0] * float(j.motionArea.width), renderY = j.motionScale[1] * float(j.motionArea.height);
    switch (CyberpunkVR_NrMVecScaleMode) {
        case 1: baseX = renderX;  baseY = renderY;  break;
        case 2: baseX = -renderX; baseY = -renderY; break;
        case 3: baseX = float(w); baseY = float(h); break;
        default: break;
    }
    p->SetFloat("DLSSNR.MVecScaleX", baseX * CyberpunkVR_NrMotionScaleX);
    p->SetFloat("DLSSNR.MVecScaleY", baseY * CyberpunkVR_NrMotionScaleY);
    p->SetUInt("DLSSNR.DepthInverted", CyberpunkVR_NrDepthConversion == 1 ? 0u : 1u);
    p->SetUInt("DLSSNR.Enabled", 1);
    p->SetUInt("DLSSNR.Reset", (CyberpunkVR_NrReset || c->fresh) ? 1u : 0u);
    c->fresh = false;
    p->SetFloat("DLSSNR.Intensity", CyberpunkVR_NrIntensity);
    p->SetFloat("DLSSNR.LocalToneStrength", CyberpunkVR_NrLocalTone);
    p->SetFloat("DLSSNR.LocalStructureStrength", CyberpunkVR_NrLocalStructure);
    p->SetFloat("DLSSNR.SkinStructureStrength", CyberpunkVR_NrSkinStructure);
    p->SetUInt("DLSSNR.UseAutoMask", 0);
    p->SetInt("DLSSNR.Style", CyberpunkVR_NrStyle);
    p->SetUInt("DLSSNR.UICorrection", 0);
    // Set last, as the add-on does; without them the runtime refused the evaluate (0xBAD00005).
    p->SetUInt("DLSS.Indicator.Invert.X.Axis", 0);
    p->SetUInt("DLSS.Indicator.Invert.Y.Axis", 0);

    // Through the module that created the feature. The core's is the hooked function: t_ours lets it pass.
    t_ours = true;
    const unsigned r = (c->viaCore ? g_coreEvaluate : g_snippetEvaluate)(j.list, c->feature, c->params, nullptr);
    t_ours = false;
    CyberpunkVR_NrLastResult = r;
    static unsigned s_logged = 0;
    if (s_logged < 6) {
        ++s_logged;
        TestLog("[nr] evaluate handle %p -> 0x%08X (%ux%u at %u,%u; motion %ux%u at %u,%u scale %.4f,%.4f)\n", c->handle, r,
                w, h, j.outX, j.outY, j.motionArea.width, j.motionArea.height, j.motionArea.left, j.motionArea.top,
                j.motionScale[0], j.motionScale[1]);
    }
    if (NgxFailed(r)) return;

    Barrier(j.list, c->output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g_codec.Run(j.dev, j.list, false, c->output, DXGI_FORMAT_R16G16B16A16_FLOAT, j.output, j.format, w, h, j.outX, j.outY,
                CyberpunkVR_NrPaperWhite, CyberpunkVR_NrColorStrength);
    Barrier(j.list, c->output, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

bool RunGuarded(Context* c, const Job& j) {
    __try { Run(c, j); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { t_ours = false; return false; }
}

std::atomic<uint32_t> g_presentFrame{0};

void PassRate() {
    static uint64_t s_windowMs = 0;
    static unsigned s_passes = 0;
    ++s_passes;
    const uint64_t now = GetTickCount64();
    if (!s_windowMs) s_windowMs = now;
    if (now - s_windowMs < 2000) return;
    CyberpunkVR_NrPassesPerSec = float(s_passes * 1000.0 / double(now - s_windowMs));
    s_windowMs = now;
    s_passes = 0;
}

// The codec reads the eye's output through an SRV and writes it back through a UAV it also loads from. RGBA16F is
// always a typed-UAV-load format; R11G11B10_FLOAT -- the game's DLSS output on 2.5 -- only where the device says so.
bool CodecReads(ID3D12Device* dev, DXGI_FORMAT format) {
    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) return true;
    if (format != DXGI_FORMAT_R11G11B10_FLOAT) return false;
    static const bool supported = [dev] {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT s{DXGI_FORMAT_R11G11B10_FLOAT};
        constexpr auto need = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
        return SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &s, sizeof(s))) &&
               (s.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) && (s.Support2 & need) == need;
    }();
    return supported;
}

// After the game's evaluate: this eye's NR, if the switch is on and the evaluate is a DLSS one this can read.
void AfterEvaluate(void* cmdList, const void* handle, const void* params) {
    namespace dlss = cvr::engine_stereo::dlss;
    const int eye = dlss::EvaluatingEye();
    dlss::EyeAreas areas{};
    const bool perEye = eye >= 0 && dlss::EvaluatingAreas(&areas);
    if (!perEye && cvr::engine_stereo::Enabled() && cvr::engine_stereo::Rendering()) return;
    ID3D12Resource *output = nullptr, *motion = nullptr, *depth = nullptr;
    D3D12_RESOURCE_DESC od{}, md{}, dd{};
    if (!ReadResource(params, "Output", &output, &od) || !ReadResource(params, "MotionVectors", &motion, &md) ||
        !ReadResource(params, "Depth", &depth, &dd)) return;
    if (!perEye) {
        areas.output = {0, 0, uint32_t(od.Width), od.Height};
        areas.motion = {0, 0, uint32_t(md.Width), md.Height};
        areas.depth = {0, 0, uint32_t(dd.Width), dd.Height};
        areas.motionScale[0] = areas.motionScale[1] = 1.0f;
    }
    auto inside = [](const dlss::EyeArea& a, const D3D12_RESOURCE_DESC& d) {
        return a.width && a.height && a.left + a.width <= d.Width && a.top + a.height <= d.Height;
    };
    if (!inside(areas.output, od) || !inside(areas.motion, md) || !inside(areas.depth, dd)) return;
    if (CyberpunkVR_NrAlternateEyes && perEye && (eye == 1) != ((g_presentFrame.load(std::memory_order_relaxed) & 1u) != 0))
        return;
    ID3D12Device* dev = nullptr;
    if (!SafeGetDevice(output, &dev) || !dev) return;
    if (!CodecReads(dev, od.Format)) {
        TestLog("[nr] the DLSS output is format %u: the codec cannot read and write it\n", unsigned(od.Format));
        Fault(7);
        dev->Release();
        return;
    }
    auto* list = static_cast<ID3D12GraphicsCommandList*>(cmdList);
    Init(dev);
    Context* c = g_inited ? ContextFor(handle, dev, list, areas.output.width, areas.output.height) : nullptr;
    if (c && !c->broken && c->w == areas.output.width && c->h == areas.output.height) {
        const Job job{list, dev, output, od.Format, areas.output.left, areas.output.top, motion, areas.motion, depth,
                      areas.depth, {areas.motionScale[0], areas.motionScale[1]}};
        if (RunGuarded(c, job)) {
            if (!NgxFailed(CyberpunkVR_NrLastResult)) { ++CyberpunkVR_NrEvals[eye == 1 ? 1 : 0]; PassRate(); }
        } else {
            TestLog("[nr] evaluate FAULTED -- NR stopped\n");
            Fault(6);
        }
    }
    dev->Release();
}

EvalFeatFn g_origEval = nullptr;

uint32_t HookedEvaluate(void* cmdList, const void* handle, const void* params, void* callback) {
    const unsigned r = g_origEval(cmdList, handle, params, callback);
    if (!t_ours && CyberpunkVR_NrEnable && !CyberpunkVR_NrDisabled && !NgxFailed(r) && cmdList && params)
        AfterEvaluate(cmdList, handle, params);
    return r;
}

void Arm() {
    void* target = nullptr;
    const wchar_t* found = nullptr;
    for (const wchar_t* name : kNgxModules) {
        HMODULE core = GetModuleHandleW(name);
        target = core ? reinterpret_cast<void*>(GetProcAddress(core, "NVSDK_NGX_D3D12_EvaluateFeature")) : nullptr;
        if (target) { found = name; break; }
    }
    if (!target) return;   // NGX not loaded yet: the next Present tries again
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookedEvaluate), reinterpret_cast<void**>(&g_origEval)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        Log("[neural-render] could not hook %ls!NVSDK_NGX_D3D12_EvaluateFeature at %p\n", found, target);
        Fault(8);
        return;
    }
    CyberpunkVR_NrArmed = 1;
    Log("[neural-render] hooked %ls!NVSDK_NGX_D3D12_EvaluateFeature at %p\n", found, target);
}

}  // namespace

void OnPresent() {
    g_presentFrame.fetch_add(1, std::memory_order_relaxed);
    if (CyberpunkVR_NrEnable && !CyberpunkVR_NrArmed && !CyberpunkVR_NrDisabled) Arm();
}

}  // namespace cvr::neural_render
