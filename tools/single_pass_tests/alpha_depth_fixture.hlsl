Texture2D<float> Alpha : register(t0);
cbuffer Material : register(b4) { uint TextureIndex; float Cutoff; };

void main(float4 position : SV_Position) {
    const uint2 texel = uint2(position.xy) & 1;
    clip(Alpha.Load(int3(texel, 0)) - Cutoff);
}
