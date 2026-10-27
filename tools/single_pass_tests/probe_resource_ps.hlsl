StructuredBuffer<float4> Exposure : register(t0);
Texture2D<float4> Pattern : register(t1);

float4 main(float4 position : SV_Position, float3 data : TEXCOORD0,
            nointerpolation uint instance : TEXCOORD1) : SV_Target0
{
    float value = Exposure[0].x + Pattern.Load(int3(uint2(position.xy) & 1, 0)).y;
    return float4(position.z, (float)instance, dot(data, float3(0.1, 0.01, 0.001)) + value, 1.0);
}
