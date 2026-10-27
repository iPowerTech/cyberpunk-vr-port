#if defined(READ_ONLY)
Texture2D<float4> source : register(t0);
#elif !defined(DIRECT_HEAP)
RWStructuredBuffer<uint> values : register(u0);
#endif
#if defined(DIRECT_HEAP)
[RootSignature("RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)")]
#endif
float4 main(float4 position : SV_Position) : SV_Target {
#if defined(READ_ONLY)
    return source.Load(int3(position.xy, 0));
#else
#if defined(DIRECT_HEAP)
    RWStructuredBuffer<uint> values = ResourceDescriptorHeap[0];
#endif
    values[0] = uint(position.x);
    return 1;
#endif
}
