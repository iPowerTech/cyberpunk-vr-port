#include "FrameGen/FrameGenDilate.hpp"

#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <cstdint>
#include <cstring>

extern void Log(const char* fmt, ...);

namespace {

// Ported from AMD's public FidelityFX-SDK (ffx_fsr3upscaler_reconstruct_dilated_velocity_and_
// previous_depth.h): FindNearestDepth (9-tap 3x3, picks the depth closest to the camera so thin
// foreground silhouettes "dilate" outward a few pixels) + borrowing the motion vector from that
// SAME winning tap (not the centre pixel) + ReconstructPrevDepth's bilinear scatter of the dilated
// depth into where its motion vector says it was last frame. The upscaler's own temporal-lock luma
// (ComputeLockInputLuma) is dropped: that feeds the SPATIAL upscaler's anti-ghosting, which this
// port does not use -- frame interpolation only wants dilatedDepth/dilatedMotionVectors/
// reconstructPrevNearDepth.
//
// The scatter write in ReconstructAndDilate is a plain store, not AMD's atomic min/max -- two
// threads racing on the same destination texel is a one-pixel disocclusion-mask inaccuracy for one
// frame, not a correctness bug worth the extra complexity for a first version.
static const char* kDilateHLSL = R"HLSL(
cbuffer K : register(b0) {
    uint2 RenderSize;
    float2 MvScale;
    uint InvertedDepth;
    float ClearValue;
};
Texture2D<float>  Depth          : register(t0);
Texture2D<float2> MotionVectors  : register(t1);
RWTexture2D<float>  DilatedDepthOut : register(u0);
RWTexture2D<float2> DilatedMvOut    : register(u1);
RWTexture2D<float>  PrevDepthOut    : register(u2);

bool IsOnScreen(int2 p, int2 size) { return all(p >= 0) && all(p < size); }
bool DepthCloser(float a, float b) { return InvertedDepth != 0 ? (a > b) : (a < b); }

static const int2 kTaps[9] = {
    int2(0,0), int2(1,0), int2(0,1), int2(0,-1), int2(-1,0),
    int2(-1,1), int2(1,1), int2(-1,-1), int2(1,-1)
};

[numthreads(8,8,1)]
void Clear(uint3 id : SV_DispatchThreadID) {
    if (id.x >= RenderSize.x || id.y >= RenderSize.y) return;
    PrevDepthOut[id.xy] = ClearValue;
}

[numthreads(8,8,1)]
void ReconstructAndDilate(uint3 id : SV_DispatchThreadID) {
    int2 pos = int2(id.xy);
    if (pos.x >= (int)RenderSize.x || pos.y >= (int)RenderSize.y) return;

    float nearestDepth = Depth.Load(int3(pos, 0));
    int2 nearestCoord = pos;
    [unroll]
    for (int i = 1; i < 9; ++i) {
        int2 p = pos + kTaps[i];
        if (!IsOnScreen(p, (int2)RenderSize)) continue;
        float d = Depth.Load(int3(p, 0));
        if (DepthCloser(d, nearestDepth)) { nearestDepth = d; nearestCoord = p; }
    }

    float2 mv = MotionVectors.Load(int3(nearestCoord, 0)) * MvScale;
    DilatedDepthOut[pos] = nearestDepth;
    DilatedMvOut[pos] = mv;

    // Splat this dilated depth into the previous-frame position the motion vector points at, so
    // the disocclusion pass downstream has a "what was there last frame" reference at this grid.
    float2 mvPixels = mv * float2(RenderSize);
    if (length(mvPixels) <= 0.1) mv = float2(0, 0);
    float2 uv = (float2(pos) + 0.5) / float2(RenderSize);
    float2 reprojPx = (uv + mv) * float2(RenderSize) - 0.5;
    int2 basePos = int2(floor(reprojPx));
    float2 frac = reprojPx - float2(basePos);
    const int2 offs[4] = { int2(0,0), int2(1,0), int2(0,1), int2(1,1) };
    float weights[4] = {
        (1 - frac.x) * (1 - frac.y), frac.x * (1 - frac.y),
        (1 - frac.x) * frac.y,       frac.x * frac.y
    };
    [unroll]
    for (int j = 0; j < 4; ++j) {
        if (weights[j] <= 0.01) continue;
        int2 storePos = basePos + offs[j];
        if (IsOnScreen(storePos, (int2)RenderSize)) PrevDepthOut[storePos] = nearestDepth;
    }
}
)HLSL";

struct EyeSlot {
    ID3D12Resource* dilatedDepth = nullptr;
    ID3D12Resource* dilatedMv = nullptr;
    ID3D12Resource* prevDepth = nullptr;
    int w = 0, h = 0;
};

ID3D12RootSignature* g_rootSig = nullptr;
ID3D12PipelineState* g_psoClear = nullptr;
ID3D12PipelineState* g_psoDilate = nullptr;
ID3D12DescriptorHeap* g_heap = nullptr;
UINT g_descSize = 0;
UINT g_ring = 0;
constexpr UINT kDescsPerDispatch = 5;  // 2 SRV + 3 UAV
constexpr UINT kRingSlots = 64;

EyeSlot g_eyes[2];
bool g_ready = false;
FrameGenDilateOutputs g_lastOutputs[2];
bool g_hasOutput[2] = {false, false};

ID3D12Resource* MakeTex(ID3D12Device* dev, int w, int h, DXGI_FORMAT fmt) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = static_cast<UINT64>(w);
    rd.Height = static_cast<UINT>(h);
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* res = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                  IID_PPV_ARGS(&res));
    return res;
}

bool CompileCS(const char* entry, ID3DBlob** out) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(kDilateHLSL, strlen(kDilateHLSL), nullptr, nullptr, nullptr, entry,
                             "cs_5_0", 0, 0, out, &err);
    if (FAILED(hr)) {
        Log("[framegen-dilate] compile %s failed 0x%08X %.*s\n", entry, hr,
            err ? static_cast<int>(err->GetBufferSize()) : 0,
            err ? static_cast<const char*>(err->GetBufferPointer()) : "");
        if (err) err->Release();
        return false;
    }
    return true;
}

}  // namespace

bool FrameGenDilate_Init(ID3D12Device* device) {
    if (g_ready) return true;
    if (!device) return false;

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;  // t0 depth, t1 motion vectors
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 3;  // u0 dilatedDepth, u1 dilatedMv, u2 prevDepth
    ranges[1].OffsetInDescriptorsFromTableStart = 2;

    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 6;  // uint2 + float2 + uint + float
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2;
    rs.pParameters = params;
    ID3DBlob* rsBlob = nullptr;
    ID3DBlob* rsErr = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr))) {
        Log("[framegen-dilate] root sig serialize failed%s\n",
            rsErr ? static_cast<const char*>(rsErr->GetBufferPointer()) : "");
        if (rsErr) rsErr->Release();
        return false;
    }
    HRESULT hr = device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                              IID_PPV_ARGS(&g_rootSig));
    rsBlob->Release();
    if (FAILED(hr)) {
        Log("[framegen-dilate] CreateRootSignature 0x%08X\n", static_cast<unsigned>(hr));
        return false;
    }

    ID3DBlob* clearCs = nullptr;
    ID3DBlob* dilateCs = nullptr;
    if (!CompileCS("Clear", &clearCs) || !CompileCS("ReconstructAndDilate", &dilateCs)) {
        if (clearCs) clearCs->Release();
        if (dilateCs) dilateCs->Release();
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = g_rootSig;
    pd.CS = {clearCs->GetBufferPointer(), clearCs->GetBufferSize()};
    HRESULT hr1 = device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_psoClear));
    pd.CS = {dilateCs->GetBufferPointer(), dilateCs->GetBufferSize()};
    HRESULT hr2 = device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_psoDilate));
    clearCs->Release();
    dilateCs->Release();
    if (FAILED(hr1) || FAILED(hr2)) {
        Log("[framegen-dilate] PSO create failed (clear=0x%08X dilate=0x%08X)\n",
            static_cast<unsigned>(hr1), static_cast<unsigned>(hr2));
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kRingSlots * kDescsPerDispatch;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_heap)))) {
        Log("[framegen-dilate] descriptor heap create failed\n");
        return false;
    }
    g_descSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    g_ready = true;
    Log("[framegen-dilate] ready (ported ReconstructAndDilate, render-res, both eyes)\n");
    return true;
}

const FrameGenDilateOutputs* FrameGenDilate_Run(ID3D12GraphicsCommandList* cmdList, int eyeSlot,
                                                 ID3D12Resource* rawDepth,
                                                 ID3D12Resource* rawMotionVectors, int renderWidth,
                                                 int renderHeight, float motionVectorScaleX,
                                                 float motionVectorScaleY, bool invertedDepth) {
    static FrameGenDilateOutputs s_out[2];  // returned to THIS call's caller (dispatch path)
    if (!g_ready || eyeSlot < 0 || eyeSlot > 1 || !cmdList || !rawDepth || !rawMotionVectors ||
        renderWidth <= 0 || renderHeight <= 0) {
        return nullptr;
    }

    ID3D12Device* dev = nullptr;
    if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return nullptr;

    EyeSlot& eye = g_eyes[eyeSlot];
    if (eye.w != renderWidth || eye.h != renderHeight || !eye.dilatedDepth) {
        if (eye.dilatedDepth) eye.dilatedDepth->Release();
        if (eye.dilatedMv) eye.dilatedMv->Release();
        if (eye.prevDepth) eye.prevDepth->Release();
        eye.dilatedDepth = MakeTex(dev, renderWidth, renderHeight, DXGI_FORMAT_R32_FLOAT);
        eye.dilatedMv = MakeTex(dev, renderWidth, renderHeight, DXGI_FORMAT_R16G16_FLOAT);
        eye.prevDepth = MakeTex(dev, renderWidth, renderHeight, DXGI_FORMAT_R32_FLOAT);
        eye.w = renderWidth;
        eye.h = renderHeight;
        if (!eye.dilatedDepth || !eye.dilatedMv || !eye.prevDepth) {
            Log("[framegen-dilate] eye=%d output texture create failed (%dx%d)\n", eyeSlot,
                renderWidth, renderHeight);
            dev->Release();
            return nullptr;
        }
    }

    const UINT slot = (g_ring % kRingSlots);
    ++g_ring;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_heap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(slot) * kDescsPerDispatch * g_descSize;
    gpu.ptr += static_cast<UINT64>(slot) * kDescsPerDispatch * g_descSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    D3D12_CPU_DESCRIPTOR_HANDLE h = cpu;
    dev->CreateShaderResourceView(rawDepth, &srv, h);
    h.ptr += g_descSize;
    srv.Format = DXGI_FORMAT_R16G16_FLOAT;
    dev->CreateShaderResourceView(rawMotionVectors, &srv, h);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    h.ptr += g_descSize;
    uav.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateUnorderedAccessView(eye.dilatedDepth, nullptr, &uav, h);
    h.ptr += g_descSize;
    uav.Format = DXGI_FORMAT_R16G16_FLOAT;
    dev->CreateUnorderedAccessView(eye.dilatedMv, nullptr, &uav, h);
    h.ptr += g_descSize;
    uav.Format = DXGI_FORMAT_R32_FLOAT;
    dev->CreateUnorderedAccessView(eye.prevDepth, nullptr, &uav, h);
    dev->Release();

    struct { uint32_t w, hgt; float mvx, mvy; uint32_t inv; float clearVal; } k{};
    k.w = static_cast<uint32_t>(renderWidth);
    k.hgt = static_cast<uint32_t>(renderHeight);
    k.mvx = motionVectorScaleX;
    k.mvy = motionVectorScaleY;
    k.inv = invertedDepth ? 1u : 0u;
    k.clearVal = invertedDepth ? 0.0f : 1.0f;  // sentinel "infinitely far"

    ID3D12DescriptorHeap* heaps[] = {g_heap};
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(g_rootSig);
    cmdList->SetComputeRoot32BitConstants(0, 6, &k, 0);
    cmdList->SetComputeRootDescriptorTable(1, gpu);

    const UINT gx = (static_cast<UINT>(renderWidth) + 7) / 8;
    const UINT gy = (static_cast<UINT>(renderHeight) + 7) / 8;
    cmdList->SetPipelineState(g_psoClear);
    cmdList->Dispatch(gx, gy, 1);
    D3D12_RESOURCE_BARRIER uavB{};
    uavB.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavB.UAV.pResource = eye.prevDepth;
    cmdList->ResourceBarrier(1, &uavB);
    cmdList->SetPipelineState(g_psoDilate);
    cmdList->Dispatch(gx, gy, 1);

    s_out[eyeSlot].dilatedDepth = eye.dilatedDepth;
    s_out[eyeSlot].dilatedMotionVectors = eye.dilatedMv;
    s_out[eyeSlot].reconstructPrevNearDepth = eye.prevDepth;
    g_lastOutputs[eyeSlot] = s_out[eyeSlot];
    g_hasOutput[eyeSlot] = true;
    return &s_out[eyeSlot];
}

const FrameGenDilateOutputs* FrameGenDilate_GetLastOutputs(int eyeSlot) {
    if (eyeSlot < 0 || eyeSlot > 1 || !g_hasOutput[eyeSlot]) return nullptr;
    return &g_lastOutputs[eyeSlot];
}

void FrameGenDilate_Shutdown() {
    for (auto& eye : g_eyes) {
        if (eye.dilatedDepth) eye.dilatedDepth->Release();
        if (eye.dilatedMv) eye.dilatedMv->Release();
        if (eye.prevDepth) eye.prevDepth->Release();
        eye = EyeSlot{};
    }
    if (g_heap) {
        g_heap->Release();
        g_heap = nullptr;
    }
    if (g_psoClear) {
        g_psoClear->Release();
        g_psoClear = nullptr;
    }
    if (g_psoDilate) {
        g_psoDilate->Release();
        g_psoDilate = nullptr;
    }
    if (g_rootSig) {
        g_rootSig->Release();
        g_rootSig = nullptr;
    }
    g_ready = false;
}
