// Velocity of the magnified image inside a VR scope lens.
//
// The glass is drawn in Transparents_WeaponAndScreen, after the G-buffer
// velocity target (R16G16_FLOAT) was filled, and writes no velocity itself, so
// the upscaler reprojected lens pixels with the motion of the unmagnified world
// behind them. The image there moves M times faster: content at p = c+M(s-c)
// was last at c'+M(s'-c'), so its velocity is M*v_scene - (M-1)*v_lens. On every
// turn the history was rejected and the raw internal-resolution image showed as
// blocks and grain. The port redraws the lens with this shader into the velocity
// target right after the glass draw.
//
// Same input signature as the native glass PS (registers 0..7). Encoding is the
// engine's own: (prevNDC - curNDC) * (0.5, -0.5).

cbuffer GlobalShaderConstants : register(b0, space0) { uint4 globals[12]; };
cbuffer CameraShaderConstants : register(b1, space0) { float4 camera[53]; };
cbuffer MaterialConstants     : register(b4, space0) { float4 material[32]; };

struct PSIn {
    float4 pos      : SV_Position;
    float4 t0       : TEXCOORD0;
    float4 t1       : TEXCOORD1;
    float4 t2       : TEXCOORD2;
    float4 t3       : TEXCOORD3;
    float4 current  : TEXCOORD4;   // unjittered current clip position of the glass
    float4 previous : TEXCOORD5;   // previous clip position of the glass
    float2 t6       : TEXCOORD6;
};

float2 main(PSIn i) : SV_Target {
    // Only lenses authored for VR (EyeRelief.w marker); globals[11].w selects the
    // alternate projection path the rows below do not describe.
    if (material[31].w != 713.25f || globals[11].w != 0u) discard;
    const float magnification = material[17].x;
    const float2 current = i.current.xy / i.current.w;
    const float2 lens = (i.previous.xy / i.previous.w - current) * float2(0.5f, -0.5f);
    // Distant point on this pixel's eye ray: rows 28/29/31 (x, y, w) are the
    // current jittered view-projection, [51].xy the NDC jitter the VS removed.
    const float2 jittered = current + camera[51].xy;
    const float3 a = camera[28].xyz, b = camera[29].xyz, c = camera[31].xyz;
    const float3 ray = cross(b, c) * jittered.x + cross(c, a) * jittered.y + cross(a, b);
    const float3 previous = float3(dot(camera[16].xyz, ray), dot(camera[17].xyz, ray), dot(camera[19].xyz, ray));
    if (abs(previous.z) < 1e-20f) discard;
    const float2 scene = (previous.xy / previous.z - current) * float2(0.5f, -0.5f);
    return lens + magnification * (scene - lens);
}
