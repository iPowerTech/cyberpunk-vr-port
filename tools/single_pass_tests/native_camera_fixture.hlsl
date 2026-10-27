cbuffer CameraShaderConsts : register(b1) { float4 cameraWords[53]; };
cbuffer Material : register(b5) { float4 tint; };
Texture2D<float4> surface : register(t0);
SamplerState pointSampler : register(s0);

struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; float clip : SV_ClipDistance0; };
Vertex VS(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float2 positions[3] = { float2(-1, -1), float2(0, 1), float2(1, -1) };
    float4 view = cameraWords[0];
    Vertex o;
    o.position = float4(positions[vertex] * view.w + float2(-.4 + instance * .8, 0) + view.xy, view.z, 1);
    o.uv = positions[vertex] * .5 + .5;
    o.clip = o.position.x + .45;
    return o;
}
struct Pixel {
    float4 color : SV_Target0;
    float4 auxiliary : SV_Target1;
#ifdef FOUR_TARGETS
    float4 albedo : SV_Target2;
    float2 motion : SV_Target3;
#endif
};
Pixel PS(Vertex i, bool front : SV_IsFrontFace) {
    float4 view = cameraWords[1 + (uint(tint.w) & 1)];
    Pixel o;
    o.color = surface.Sample(pointSampler, i.uv) * float4(view.rgb * tint.rgb, 1);
    o.auxiliary = float4(i.uv, cameraWords[52].x, front ? 1 : .5);
#ifdef FOUR_TARGETS
    o.albedo = float4(view.rgb, 1);
    o.motion = float2(cameraWords[52].x, view.r);
#endif
    return o;
}
#ifdef FOUR_TARGETS
struct PixelThree { float4 color : SV_Target0; float4 auxiliary : SV_Target1; float4 albedo : SV_Target2; };
PixelThree PS3(Vertex i, bool front : SV_IsFrontFace) {
    Pixel p = PS(i, front);
    PixelThree o;
    o.color = p.color;
    o.auxiliary = p.auxiliary;
    o.albedo = p.albedo;
    return o;
}
#endif
