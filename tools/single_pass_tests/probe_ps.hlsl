// Diagnostic output, independent of material shading: projected depth and the
// game's original instance ID. Alpha marks covered samples for exact comparison.
float4 main(float4 position : SV_Position, float3 data : TEXCOORD0,
            nointerpolation uint instance : TEXCOORD1) : SV_Target0
{
    return float4(position.z, (float)instance, dot(data, float3(0.1, 0.01, 0.001)), 1.0);
}
