// Upscaler history mask of a VR scope lens (Streamline kBufferTypeBiasCurrentColorHint).
//
// The lens shows the scene magnified M times. The upscaler accumulates every
// pixel over jittered frames; inside the lens one internal pixel spans M x M
// output pixels and a turn moves the image M times faster than the glass, so
// its history kept failing in blinking points and blocks. The lens source
// resolve (lens_source_resolve_cs.hlsl) already hands the glass a temporally
// stable, sharpened scene image; the upscaler only has to take the current
// frame there. Right after each glass draw the port redraws the lens with this
// shader into an R8 mask (1 = reject history), tagged with the velocity.
//
// Same input signature as the native glass PS (registers 0..7).
cbuffer GlobalShaderConstants : register(b0, space0) { uint4 globals[12]; };
cbuffer MaterialConstants     : register(b4, space0) { float4 material[32]; };

struct PSIn {
    float4 pos      : SV_Position;
    float4 t0       : TEXCOORD0;
    float4 t1       : TEXCOORD1;
    float4 t2       : TEXCOORD2;
    float4 t3       : TEXCOORD3;
    float4 current  : TEXCOORD4;
    float4 previous : TEXCOORD5;
    float2 t6       : TEXCOORD6;
};

float main(PSIn i) : SV_Target {
    // Only lenses authored for VR (EyeRelief.w marker), as lens_velocity_ps.
    if (material[31].w != 713.25f || globals[11].w != 0u) discard;
    return 1.0f;
}
