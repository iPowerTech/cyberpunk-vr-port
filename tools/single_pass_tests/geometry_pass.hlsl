struct Config { float4 offsetColor; };
#ifdef NATIVE_CAMERA
cbuffer NativeCameras : register(b1) { float4 cameras[2]; };
#elif defined(PAIRED)
ConstantBuffer<Config> eyes[2] : register(b2, space100);
#else
ConstantBuffer<Config> eye : register(b1);
#endif
struct Output { float4 position : SV_Position; float4 color : COLOR0; };
Output VS(float2 vertex : POSITION, float4 instance : INSTANCE0, uint id : SV_InstanceID
#if defined(PAIRED) || defined(NATIVE_CAMERA)
    , uint view : SV_ViewID
#endif
) {
#ifdef NATIVE_CAMERA
    float4 config = cameras[view ^ 1];
#elif defined(PAIRED)
    float4 config = eyes[view].offsetColor;
#else
    float4 config = eye.offsetColor;
#endif
    Output o;
    o.position = float4(vertex * .3 + instance.xy + config.xy, config.w, 1);
    o.color = float4((id + 1) * .25, instance.z, config.z, 1);
    return o;
}
float4 PS(Output i) : SV_Target { return i.color; }
