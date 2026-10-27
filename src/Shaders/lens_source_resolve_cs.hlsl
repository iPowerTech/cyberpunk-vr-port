// Scope lens source resolve. The glass shader magnifies the pre-upscale scene
// copy, so a sub-pixel light or grate bar that one jittered frame hits and the
// next misses becomes an M x M blob blinking in the lens -- far too large for
// the upscaler to treat as aliasing. This accumulates that scene image over
// frames before the glass reads it: history reprojected with the engine's
// velocity, clipped to the current 3x3 neighbourhood, blended in tonemapped
// YCoCg. The history is kept unjittered: each jittered sample is weighted by
// where it really lies, so the current frame enters at the pixel centre. The
// image handed to the glass is jittered exactly like the frame -- pixel p shows
// the world at p - jitter, the raw sample itself blended with the history
// looked up there -- so the glass and the upscaler see the frame's own jitter
// and the output never feeds back into the history.
//
// Only in motion. Standing still the upscaler resolves the raw jittered frame
// itself, super-sampling across jitter phases -- far sharper than any of this
// (measured: lens Laplacian 8.7 raw vs ~4 resolved, equally stable). It fails
// only when the content moves, so the glass gets the raw frame until the scene
// moves ~0.25 px per frame and the stabilised one from ~1.5 px on.
//
// CLEAN LENS (clean != 0). The upscaler takes the current frame inside the lens
// (lens_mask_ps.hlsl, history rejected there), so the glass gets the stabilised
// image at rest too, sharpened by a contrast-adaptive cross filter (AMD CAS) of
// strength 'sharpen' on the history around the sample.
//
// Bright saturated colours (neon) leave the RGB cube after the clip in YCoCg;
// Expand then divided by ~0 and handed the upscaler huge values that came out
// as black blocks. The compressed peak is kept below 0.99 (99x the tonemap
// knee) and non-finite results fall back to the current sample.
//
// dxc -T cs_6_0 -E main -O3 lens_source_resolve_cs.hlsl -Fo lens_source_resolve_cs.dxil
#define ROOT "RootConstants(num32BitConstants=11, b0)," \
             "DescriptorTable(SRV(t0, numDescriptors=3), UAV(u0, numDescriptors=2))," \
             "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP)"

cbuffer Constants : register(b0) {
    uint2 size;
    float2 inverseSize;
    float alpha;      // weight of the current frame in motion
    float gamma;      // neighbourhood box in standard deviations
    uint reset;
    uint clean;       // the upscaler rejects lens history: stabilised image always
    float2 jitter;    // content shift in pixels, y down: pixel p shows the world at p - jitter
    float sharpen;    // CAS strength 0..1 in clean mode
};
Texture2D<float3> current : register(t0);    // engine scene colour, jittered
Texture2D<float2> velocity : register(t1);   // (previous - current) uv, unjittered
Texture2D<float4> history : register(t2);
RWTexture2D<float4> historyOut : register(u0);
RWTexture2D<float3> output : register(u1);   // copied over the glass pass' scene copy, jittered
SamplerState linearClamp : register(s0);

float Peak(float3 c) { return max(c.r, max(c.g, c.b)); }
float3 Compress(float3 c) { return c / (1.0f + Peak(c)); }
float3 Expand(float3 c) {
    c = max(c, 0.0f);
    const float peak = Peak(c);
    if (peak > 0.99f) c *= 0.99f / peak;
    return c / (1.0f - Peak(c));
}
bool Finite(float3 c) { return !any(isnan(c)) && !any(isinf(c)); }
float3 ToYCoCg(float3 c) {
    return float3(dot(c, float3(0.25f, 0.5f, 0.25f)), dot(c, float3(0.5f, 0.0f, -0.5f)), dot(c, float3(-0.25f, 0.5f, -0.25f)));
}
float3 FromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
float3 Load(int2 p) { return ToYCoCg(Compress(min(max(current[clamp(p, int2(0, 0), int2(size) - 1)], 0.0f), 65000.0f))); }

// Five bilinear taps of a Catmull-Rom filter: history is resampled every frame
// and a bilinear lookup would blur it a little more each time.
float3 History(float2 uv) {
    const float2 position = uv * float2(size);
    const float2 centre = floor(position - 0.5f) + 0.5f;
    const float2 f = position - centre;
    const float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    const float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    const float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    const float2 w3 = f * f * (-0.5f + 0.5f * f);
    const float2 w12 = w1 + w2;
    const float2 t0 = (centre - 1.0f) * inverseSize;
    const float2 t3 = (centre + 2.0f) * inverseSize;
    const float2 t12 = (centre + w2 / w12) * inverseSize;
    float3 sum = history.SampleLevel(linearClamp, float2(t12.x, t0.y), 0).rgb * (w12.x * w0.y);
    sum += history.SampleLevel(linearClamp, float2(t0.x, t12.y), 0).rgb * (w0.x * w12.y);
    sum += history.SampleLevel(linearClamp, t12, 0).rgb * (w12.x * w12.y);
    sum += history.SampleLevel(linearClamp, float2(t3.x, t12.y), 0).rgb * (w3.x * w12.y);
    sum += history.SampleLevel(linearClamp, float2(t12.x, t3.y), 0).rgb * (w12.x * w3.y);
    const float weight = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(sum / weight, 0.0f);
}
float3 Clip(float3 value, float3 low, float3 high) {
    const float3 centre = 0.5f * (low + high), extent = 0.5f * (high - low) + 1e-5f;
    const float3 offset = value - centre, units = abs(offset / extent);
    const float largest = max(units.x, max(units.y, units.z));
    return largest > 1.0f ? centre + offset / largest : value;
}

[RootSignature(ROOT)]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const int2 p = int2(id.xy);
    if (any(id.xy >= size)) return;
    float3 m1 = 0.0f, m2 = 0.0f, sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x) {
            const float3 s = Load(p + int2(x, y));
            m1 += s; m2 += s * s;
            const float2 d = float2(x, y) - jitter;   // sample position relative to this pixel's centre
            const float w = exp(-2.29f * dot(d, d));
            sum += s * w; weight += w;
        }
    m1 /= 9.0f; m2 /= 9.0f;
    const float3 c = sum / weight;
    const float3 sigma = sqrt(max(m2 - m1 * m1, 0.0f));
    const float3 raw = Load(p);
    const float2 uv = (float2(p) + 0.5f) * inverseSize;
    const float2 motion = velocity[p];
    const float2 previous = uv + motion;
    const float moving = saturate((length(motion * float2(size)) - 0.25f) / 1.25f);
    float3 result = c, shown = raw;
    if (!reset && all(previous >= 0.0f) && all(previous <= 1.0f)) {
        const float3 low = m1 - gamma * sigma, high = m1 + gamma * sigma;
        result = lerp(Clip(ToYCoCg(Compress(History(previous))), low, high), c, alpha);
        const float2 at = previous - jitter * inverseSize;
        shown = lerp(Clip(ToYCoCg(Compress(History(at))), low, high), raw, clean ? alpha : lerp(1.0f, alpha, moving));
        if (clean && sharpen > 0.0f) {
            // AMD FidelityFX CAS, cross taps, in tonemapped RGB: the weight of the
            // four neighbours follows the local headroom, so edges sharpen and
            // flat or already contrasted areas do not ring.
            const float3 centre = saturate(FromYCoCg(shown));
            const float3 n = saturate(Compress(history.SampleLevel(linearClamp, at - float2(0.0f, inverseSize.y), 0).rgb));
            const float3 w = saturate(Compress(history.SampleLevel(linearClamp, at - float2(inverseSize.x, 0.0f), 0).rgb));
            const float3 e = saturate(Compress(history.SampleLevel(linearClamp, at + float2(inverseSize.x, 0.0f), 0).rgb));
            const float3 s = saturate(Compress(history.SampleLevel(linearClamp, at + float2(0.0f, inverseSize.y), 0).rgb));
            const float3 lo = min(centre, min(min(n, w), min(e, s)));
            const float3 hi = max(centre, max(max(n, w), max(e, s)));
            const float3 amp = sqrt(saturate(min(lo, 1.0f - hi) / max(hi, 1e-5f)));
            const float3 weight = amp * (-1.0f / lerp(8.0f, 5.0f, saturate(sharpen)));
            shown = ToYCoCg(saturate((centre + (n + w + e + s) * weight) / (1.0f + 4.0f * weight)));
        }
    }
    float3 keep = Expand(FromYCoCg(result)), show = Expand(FromYCoCg(shown));
    if (!Finite(keep)) keep = Expand(FromYCoCg(c));
    if (!Finite(show)) show = Expand(FromYCoCg(raw));
    historyOut[p] = float4(keep, 1.0f);
    output[p] = show;
}
