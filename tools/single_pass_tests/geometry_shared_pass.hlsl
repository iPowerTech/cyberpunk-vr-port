struct Camera
{
    float4 color;
    float4 padding[52];
};
struct Selector
{
    uint index;
};
ConstantBuffer<Camera> originalCamera : register(b1);
ConstantBuffer<Selector> originalSelector : register(b4);
Texture2D<float4> originalEye : register(t0);
ConstantBuffer<Camera> cameras[2] : register(b2, space100);
ConstantBuffer<Selector> selectors[2] : register(b8, space101);
Texture2D<float4> eyes[2] : register(t0, space101);
Texture2D<float4> sharedSources[32768] : register(t0, space1);
Texture2DArray<float4> materialArray : register(t67);
SamplerState sharedSamplers[16] : register(s0, space2);
SamplerState fixedSampler : register(s0, space3);
struct Vertex
{
    float4 position : SV_Position;
    float color : TEXCOORD0;
};
Vertex OriginalVS(float2 position : POSITION, float4 instance : INSTANCE)
{
    Vertex result;
    result.position = float4(position + instance.xy, 0, 1);
    result.color = originalCamera.color.r + originalCamera.padding[51].w;
    return result;
}
Vertex PairedVS(float2 position : POSITION, float4 instance : INSTANCE, uint view : SV_ViewID)
{
    Vertex result;
    result.position = float4(position + instance.xy, 0, 1);
    result.color = cameras[view].color.r + cameras[view].padding[51].w;
    return result;
}
float4 OriginalPS(Vertex vertex) : SV_Target
{
    float eye = originalEye.SampleLevel(fixedSampler, float2(.5, .5), 0).r
        + originalEye.SampleLevel(fixedSampler, float2(2, 2), 0).r;
    float sharedValue = sharedSources[NonUniformResourceIndex(originalSelector.index)].SampleLevel(sharedSamplers[13], float2(2, 2), 0).r;
    float material = materialArray.SampleLevel(fixedSampler, float3(.5, .5, 1), 0).r;
    return float4(vertex.color + eye + sharedValue + material, 0, 0, 1);
}
float4 PairedPS(Vertex vertex, uint view : SV_ViewID) : SV_Target
{
    float eye = eyes[NonUniformResourceIndex(view)].SampleLevel(fixedSampler, float2(.5, .5), 0).r
        + eyes[NonUniformResourceIndex(view)].SampleLevel(fixedSampler, float2(2, 2), 0).r;
    float sharedValue = sharedSources[NonUniformResourceIndex(selectors[view].index)].SampleLevel(sharedSamplers[13], float2(2, 2), 0).r;
    float material = materialArray.SampleLevel(fixedSampler, float3(.5, .5, 1), 0).r;
    return float4(vertex.color + eye + sharedValue + material, 0, 0, 1);
}
