// Camera-scope source resolve. The scope's RTT view has no temporal
// anti-aliasing: it is not jittered and keeps no history (measured: frame-to-
// frame shift < 0.001 px), so every effect the engine builds for a temporal
// resolve -- screen-space reflections, AO, contact shadows -- leaves its noise
// in the image, ~1500 dark single-pixel dots per 768^2 frame, two thirds of
// them in a different place every frame. This accumulates the image over
// frames the way TAA would have: history reprojected by the camera's own
// rotation (a homography of the two camera bases; the weapon-mounted camera
// mostly turns, and the far scene a magnifying scope looks at has no useful
// parallax), clipped to the current 3x3 neighbourhood in tonemapped YCoCg.
// A sample darker than half of its second-darkest neighbour is an isolated
// dot, not detail (a one-pixel line keeps two dark neighbours), and enters as
// the mean of its brighter neighbours.
//
// dxc -T cs_6_0 -E main -O3 camera_source_resolve_cs.hlsl -Fo camera_source_resolve_cs.dxil
#define ROOT "RootConstants(num32BitConstants=20, b0)," \
             "DescriptorTable(SRV(t0, numDescriptors=2), UAV(u0, numDescriptors=1))," \
             "StaticSampler(s0, filter=FILTER_MIN_MAG_MIP_LINEAR, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP)"

cbuffer Constants : register(b0) {
    uint2 size;
    float2 inverseSize;
    float alpha;        // weight of the current frame, camera still
    float motionAlpha;  // weight of the current frame from 4 px/frame of motion on
    float gamma;        // neighbourhood box in standard deviations
    uint reset;
    float4 row0;        // current NDC (x, y, 1) -> previous homogeneous NDC
    float4 row1;
    float4 row2;
};
Texture2D<float4> current : register(t0);   // the camera image the engine just wrote
Texture2D<float4> history : register(t1);
RWTexture2D<float4> historyOut : register(u0);
SamplerState linearClamp : register(s0);

float Peak(float3 c) { return max(c.r, max(c.g, c.b)); }
float3 Compress(float3 c) { return c / (1.0f + Peak(c)); }
float3 Expand(float3 c) { return c / max(1.0f - Peak(c), 1e-4f); }
float3 ToYCoCg(float3 c) {
    return float3(dot(c, float3(0.25f, 0.5f, 0.25f)), dot(c, float3(0.5f, 0.0f, -0.5f)), dot(c, float3(-0.25f, 0.5f, -0.25f)));
}
float3 FromYCoCg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
float3 Load(int2 p) {
    const float3 c = current[clamp(p, int2(0, 0), int2(size) - 1)].rgb;
    return ToYCoCg(Compress(max(c, 0.0f) * all(isfinite(c))));
}

// Five bilinear taps of a Catmull-Rom filter: history is resampled every frame
// the camera moves and a bilinear lookup would blur it a little more each time.
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
    float3 n[8];
    uint k = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
            if (x || y) n[k++] = Load(p + int2(x, y));
    float3 c = Load(p);
    // Two darkest neighbour lumas, and the sum of the other six.
    float d0 = 1e9f, d1 = 1e9f, total = 0.0f;
    float3 sum8 = 0.0f;
    [unroll] for (uint i = 0; i < 8; ++i) {
        const float l = n[i].x;
        sum8 += n[i]; total += l;
        if (l < d0) { d1 = d0; d0 = l; } else if (l < d1) d1 = l;
    }
    if (c.x < 0.5f * d1 && d1 > 2e-3f) {
        // Mean of the six brighter neighbours in luma, chroma from all eight.
        const float bright = (total - d0 - d1) / 6.0f;
        c = float3(bright, sum8.yz / 8.0f);
    }
    float3 m1 = c, m2 = c * c;
    [unroll] for (uint j = 0; j < 8; ++j) { m1 += n[j]; m2 += n[j] * n[j]; }
    m1 /= 9.0f; m2 /= 9.0f;
    const float3 sigma = sqrt(max(m2 - m1 * m1, 0.0f));
    const float2 uv = (float2(p) + 0.5f) * inverseSize;
    const float3 ndc = float3(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f);
    const float3 h = float3(dot(row0.xyz, ndc), dot(row1.xyz, ndc), dot(row2.xyz, ndc));
    float3 result = c;
    if (!reset && h.z > 1e-4f) {
        const float2 q = h.xy / h.z;
        const float2 previous = float2(0.5f + 0.5f * q.x, 0.5f - 0.5f * q.y);
        if (all(previous >= 0.0f) && all(previous <= 1.0f)) {
            const float moved = length((previous - uv) * float2(size));
            const float a = lerp(alpha, motionAlpha, saturate(moved * 0.25f));
            const float3 low = m1 - gamma * sigma, high = m1 + gamma * sigma;
            result = lerp(Clip(ToYCoCg(Compress(History(previous))), low, high), c, a);
        }
    }
    historyOut[p] = float4(Expand(max(FromYCoCg(result), 0.0f)), current[p].a);
}
