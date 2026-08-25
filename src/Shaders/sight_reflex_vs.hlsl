// Replacement vertex shader for the holographic sight's reticle quad.
//
// Reproduces the engine's own vertex shader (hash 9228439BF72D91DB, 8280 bytes, DXIL 6.0) and adds
// ONE thing: the world size of the glass, handed down to the pixel shader.
//
// Why that is needed at all: a collimated reticle is placed by the eye's LATERAL OFFSET from the
// sight axis, in metres, while the reticle is addressed in uv. Converting between them needs the
// glass size, and the pixel shader cannot obtain it -- constant buffer b5, which holds the vertex
// dequantisation scale, is bound to the vertex stage only, and a single pixel gives one equation
// in two unknowns. Two attempts to close that gap with ddx/ddy failed for a measurable reason:
// DLSS jitters the projection every frame, the 2x2 derivative quads land differently each time,
// and the reticle's texels visibly ran back and forth.
//
// Here the number is exact and static. POSITION arrives normalised and is expanded by
// _30_m0[4].xyz, so that vector IS the mesh's bounding box in world units, and this mesh is the
// quad. Measuring it along the local tangent and bitangent gives the size of one uv unit.
//
// Which pipeline to patch was itself measured: this pixel shader is used by TWO pipelines, and
// the first one CREATED is not the one that draws (its vertex shader has three inputs and cannot
// place a quad in the world). The draw names the right one; see the [pso] ... IN USE log line.
//
// Signatures taken from the original container, not from a decompiler's naming: 13 inputs
// POSITION0..SV_VertexID0, and 7 outputs SV_Position0 / TEXCOORD0..4 / SV_ClipDistance0.

cbuffer MaterialModifiers  : register(b7, space0) { float4 _15_m0[28]; };
cbuffer GlobalShaderConsts : register(b0, space0) { float4 _20_m0[30]; };
cbuffer CameraShaderConsts : register(b1, space0) { float4 _25_m0[53]; };
cbuffer FrequentVertex     : register(b5, space0) { float4 _30_m0[7];  };

Buffer<uint4> _8 : register(t10, space0);          // skinning matrices, raw dwords

struct VSIn {
    float3 POSITION                 : POSITION0;
    uint4  BLENDINDICES             : BLENDINDICES0;
    float4 BLENDWEIGHT              : BLENDWEIGHT0;
    float2 TEXCOORD                 : TEXCOORD0;
    float3 NORMAL                   : NORMAL0;
    float4 TANGENT                  : TANGENT0;
    float4 COLOR                    : COLOR0;
    float2 TEXCOORD_1               : TEXCOORD1;
    float4 INSTANCE_TRANSFORM[3]    : INSTANCE_TRANSFORM0;
    uint4  INSTANCE_SKINNING_DATA   : INSTANCE_SKINNING_DATA0;
    uint   vertexId                 : SV_VertexID;
};

struct VSOut {
    float4 pos  : SV_Position;
    float4 t0   : TEXCOORD0;
    float4 t1   : TEXCOORD1;
    float4 t2   : TEXCOORD2;
    float4 t3   : TEXCOORD3;
    float4 t4   : TEXCOORD4;
    float  clip : SV_ClipDistance0;
};

// One 3x4 skinning matrix, read as raw dwords exactly the way the original does.
void loadBone(uint bi, uint4 sk, out float4 r0, out float4 r1, out float4 r2) {
    const uint b = (bi * sk.y) + sk.x;
    const uint i0 = b >> 2u, i1 = (b + 16u) >> 2u, i2 = (b + 32u) >> 2u;
    const uint4 a = uint4(_8.Load(i0).x, _8.Load(i0 + 1u).x, _8.Load(i0 + 2u).x, _8.Load(i0 + 3u).x);
    const uint4 c = uint4(_8.Load(i1).x, _8.Load(i1 + 1u).x, _8.Load(i1 + 2u).x, _8.Load(i1 + 3u).x);
    const uint4 d = uint4(_8.Load(i2).x, _8.Load(i2 + 1u).x, _8.Load(i2 + 2u).x, _8.Load(i2 + 3u).x);
    r0 = asfloat(a); r1 = asfloat(c); r2 = asfloat(d);
}

VSOut main(VSIn IN) {
    VSOut OUT;

    const uint4 rebase = asuint(_25_m0[38]);

    // Dequantise. _30_m0[4] is the bounding box, _30_m0[5] its corner.
    const float3 lp = (_30_m0[4].xyz * IN.POSITION) + _30_m0[5].xyz;

    const float wsum = max(1e-05f, dot(IN.BLENDWEIGHT, 1.0f.xxxx));
    const float4 w = IN.BLENDWEIGHT / wsum;

    float4 b0r0, b0r1, b0r2, b1r0, b1r1, b1r2, b2r0, b2r1, b2r2, b3r0, b3r1, b3r2;
    loadBone(IN.BLENDINDICES.x, IN.INSTANCE_SKINNING_DATA, b0r0, b0r1, b0r2);
    loadBone(IN.BLENDINDICES.y, IN.INSTANCE_SKINNING_DATA, b1r0, b1r1, b1r2);
    loadBone(IN.BLENDINDICES.z, IN.INSTANCE_SKINNING_DATA, b2r0, b2r1, b2r2);
    loadBone(IN.BLENDINDICES.w, IN.INSTANCE_SKINNING_DATA, b3r0, b3r1, b3r2);

    const float4 m0 = b0r0 * w.x + b1r0 * w.y + b2r0 * w.z + b3r0 * w.w;
    const float4 m1 = b0r1 * w.x + b1r1 * w.y + b2r1 * w.z + b3r1 * w.w;
    const float4 m2 = b0r2 * w.x + b1r2 * w.y + b2r2 * w.z + b3r2 * w.w;

    const float3 sp = float3(m0.w + dot(m0.xyz, lp),
                             m1.w + dot(m1.xyz, lp),
                             m2.w + dot(m2.xyz, lp));

    // Instance placement. The translation is int32 fixed point at 1/131072, rebased per frame.
    const float3 ip = float3(
        dot(float3(IN.INSTANCE_TRANSFORM[0].x, IN.INSTANCE_TRANSFORM[0].y, IN.INSTANCE_TRANSFORM[0].z), sp)
            + float(int(asuint(IN.INSTANCE_TRANSFORM[0].w) - rebase.x)) * 7.62939453125e-06f,
        dot(float3(IN.INSTANCE_TRANSFORM[1].x, IN.INSTANCE_TRANSFORM[1].y, IN.INSTANCE_TRANSFORM[1].z), sp)
            + float(int(asuint(IN.INSTANCE_TRANSFORM[1].w) - rebase.y)) * 7.62939453125e-06f,
        dot(float3(IN.INSTANCE_TRANSFORM[2].x, IN.INSTANCE_TRANSFORM[2].y, IN.INSTANCE_TRANSFORM[2].z), sp)
            + float(int(asuint(IN.INSTANCE_TRANSFORM[2].w) - rebase.z)) * 7.62939453125e-06f);

    const float3 wp = _25_m0[37].xyz + ip;

    // The skin+instance rotation, applied to the tangent frame.
    const float3x3 R = float3x3(
        float3(dot(float3(IN.INSTANCE_TRANSFORM[0].x, IN.INSTANCE_TRANSFORM[0].y, IN.INSTANCE_TRANSFORM[0].z), m0.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[0].x, IN.INSTANCE_TRANSFORM[0].y, IN.INSTANCE_TRANSFORM[0].z), m1.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[0].x, IN.INSTANCE_TRANSFORM[0].y, IN.INSTANCE_TRANSFORM[0].z), m2.xyz)),
        float3(dot(float3(IN.INSTANCE_TRANSFORM[1].x, IN.INSTANCE_TRANSFORM[1].y, IN.INSTANCE_TRANSFORM[1].z), m0.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[1].x, IN.INSTANCE_TRANSFORM[1].y, IN.INSTANCE_TRANSFORM[1].z), m1.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[1].x, IN.INSTANCE_TRANSFORM[1].y, IN.INSTANCE_TRANSFORM[1].z), m2.xyz)),
        float3(dot(float3(IN.INSTANCE_TRANSFORM[2].x, IN.INSTANCE_TRANSFORM[2].y, IN.INSTANCE_TRANSFORM[2].z), m0.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[2].x, IN.INSTANCE_TRANSFORM[2].y, IN.INSTANCE_TRANSFORM[2].z), m1.xyz),
               dot(float3(IN.INSTANCE_TRANSFORM[2].x, IN.INSTANCE_TRANSFORM[2].y, IN.INSTANCE_TRANSFORM[2].z), m2.xyz)));

    const float3 nL = (IN.NORMAL  * 2.0f) - 1.0f;
    const float3 tL = (IN.TANGENT.xyz * 2.0f) - 1.0f;
    const float  hL = (IN.TANGENT.w   * 2.0f) - 1.0f;
    const float3 bL = cross(nL, tL) * hL;

    // Keep the UNNORMALISED transformed axes: their length is the skinning scale, and that is
    // exactly the factor between local and world size. Throwing it away (which the first version
    // did, by normalising immediately) is what made the reticle too small -- and, by the same
    // factor, over-reactive to head movement, since the dot travels eye_offset / size.
    const float3 tWraw = mul(R, tL);
    const float3 bWraw = mul(R, bL);
    const float3 nW = normalize(mul(R, nL));
    const float3 bW = normalize(bWraw);
    const float3 tW = normalize(tWraw);

    if ((_15_m0[1].x > 0.5f) && (asuint(_20_m0[29]).y == 2u)) {
        OUT.pos = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        OUT.pos = float4(dot(_25_m0[28].xyz, ip) + _25_m0[28].w,
                         dot(_25_m0[29].xyz, ip) + _25_m0[29].w,
                         dot(_25_m0[30].xyz, ip) + _25_m0[30].w,
                         dot(_25_m0[31].xyz, ip) + _25_m0[31].w);
    }

    OUT.t0 = float4(IN.TEXCOORD.x, IN.TEXCOORD.y, dot(_25_m0[26].xyz, ip) + _25_m0[26].w, 0.0f);

    // THE ADDITION. The glass, measured along its own uv axes, in world units. The pixel shader
    // reads it from here. This slot carried the vertex colour, which the reticle's pixel shader
    // never reads -- its input signature marks TEXCOORD1 as entirely unused -- and only the one
    // pipeline that pairs this vertex shader with that pixel shader is ever patched, so no other
    // material can be affected by the reuse.
    // World size of ONE UV UNIT along each glass axis. Read off the actual vertex data rather
    // than assumed -- three assumptions in a row were wrong here, and the buffer settles it:
    //
    //   vertex  uv        normalised position        local position (mm)
    //     v1  (0,0)   (-0.996, +1.000, -0.995)   (-12.85, -0.30, +12.97)
    //     v2  (1,0)   (+1.000, -0.923, -1.000)   (+12.70, -0.49, +12.90)
    //     v3  (0,1)   (-1.000, +0.923, +1.000)   (-12.90, -0.31, +39.50)
    //
    // Two facts come out of it. POSITION is SNORM16, so it arrives in [-1,+1] and the box spans
    // TWICE _30_m0[4] -- 25.6 x 26.5 mm, a normal reflex window -- while _30_m0[5] is its CENTRE,
    // not its corner. And the uv does run the full 0..1 across the quad, which is what the
    // ratio-from-centre attempt (the one that came out deformed) was trying to establish.
    //
    // So the size per uv unit is 2 * box, measured along the glass axes, and that is understated
    // by exactly 2.00x by the first version -- the missing factor behind "too small to aim with".
    const float3 extent = 2.0f * _30_m0[4].xyz;

    // THE SIGHT'S OPTICAL AXIS, and it needs no new channel -- it is already in the data.
    //
    // The window mesh is a slab: 25.6 x 0.2 x 26.5 mm. The THIN axis is the one you look along,
    // so it is the optical axis by construction, and which of the three it is comes from the
    // bounding box rather than from a constant. The instance transform (already read here) then
    // carries it into the world.
    //
    // The pixel shader was collimating along the interpolated vertex NORMAL instead. That normal
    // carries the mesh's authored tilt: from the vertex buffer, u runs 25.55 mm while y drifts
    // 0.19 mm, a 0.43 degree rotation about the vertical -- 15 cm sideways at 20 m, which is
    // exactly the "dot sits slightly right of the barrel" that was reported.
    //
    // Only TWO numbers need to travel: the axis expressed in the glass's own tangent frame.
    // Both fit in the half of TEXCOORD1 that the size measurement left free.
    float3 axisL = float3(1.0f, 0.0f, 0.0f);
    const float3 e = _30_m0[4].xyz;
    if (e.y <= e.x && e.y <= e.z)      axisL = float3(0.0f, 1.0f, 0.0f);
    else if (e.z <= e.x && e.z <= e.y) axisL = float3(0.0f, 0.0f, 1.0f);
    if (dot(axisL, nL) < 0.0f) axisL = -axisL;          // point it the same way as the normal
    const float3 boreW = normalize(mul(R, axisL));
    const float  bn = dot(boreW, nW);
    const float2 bore = (abs(bn) > 1e-4f)
        ? float2(dot(boreW, tW) / bn, dot(boreW, bW) / bn)
        : float2(0.0f, 0.0f);                           // degenerate: fall back to the normal

    OUT.t1 = float4(length(extent * tL) * length(tWraw),
                    length(extent * bL) * length(bWraw),
                    bore.x, bore.y);

    OUT.t2 = float4(nW, bW.x);
    OUT.t3 = float4(bW.y, bW.z, tW.x, tW.y);
    OUT.t4 = float4(tW.z, wp);
    OUT.clip = dot(_25_m0[50], float4(wp, 1.0f));
    return OUT;
}
