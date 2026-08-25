// Replacement pixel shader for the holographic sight's reticle quad.
//
// Identified live as PS hash 66394C5F4B95AB9A (6271 bytes, DXIL / shader model 6.0), drawn as a
// 6-index instanced quad inside CRenderNode_RenderElements. Confirmed by removal: dropping this
// draw removes the reticle and nothing else.
//
// WHAT THE ORIGINAL DOES, and why it is wrong in VR
// -------------------------------------------------
// It samples the reticle atlas four times: once at the quad's own uv, and three more times
// displaced along the view direction in tangent space. Measured material constants:
//
//     _29_m0[7].x  = 0.1                     displacement scale
//     _29_m0[8]    = (1, 0.1, 0.075, 0.05)   weights of the four layers
//     _29_m0[12]   = (0.5, 0, 1, 0.5)        atlas rect of the reticle
//
// The crisp reticle is the FIRST layer -- the one with zero displacement. It is painted on the
// glass and does not move with the eye at all; the displaced layers are a faint shimmer. On a
// flat screen the eye is always on the sight's axis, so nothing gives it away. In VR you can
// look at the glass from the side and the dot follows you, which a collimated sight never does.
//
// WHAT THIS DOES INSTEAD
// ----------------------
// A collimated sight projects its reticle to infinity along the bore. The eye therefore sees the
// dot at the one point on the glass where the line of sight is parallel to the bore, and sees
// nothing once that point leaves the window. That is an angular lookup, not a positional one:
//
//     uv = 0.5 + dist * float2(dot(V,T), dot(V,B)) / glassSizePerUv
//
// with V the direction from eye to pixel and dist their distance. The scale is DERIVED, not
// tuned: glassSizePerUv comes from the screen-space derivatives of the interpolated world
// position, so the reticle keeps exactly the apparent size the artist authored and there is no
// magic number to fit. The same construction is what the Crysis VR mod does with an explicit
// world-space aim point; here the aim point is at infinity, which is what a collimator gives.
//
// The dot vanishing off-axis needs no extra code: the sample coordinate is clamped into the
// atlas rect with a 1% inset (the original's own clamp, kept below), and the reticle's border is
// empty. That is the same mechanism the Crysis shader relies on.
//
// Signature reproduced from the original container's ISG1 chunk -- SV_Position0 plus TEXCOORD0..4
// at registers 0..5. The decompiler renamed those to TEXCOORD1..5 and dropped SV_Position; using
// its names would produce a signature the vertex shader does not match and the PSO would fail.

// ---- the two tunables, both physical ---------------------------------------------------------
// SIGHT_SIZE_SCALE corrects the glass size the vertex shader measures, if it turns out to be off
// by a constant. It is a CHECK, not a taste setting: the reticle's apparent size is a direct
// readout of it. At the correct value the collimated reticle is exactly as large as the stock
// one, because the mapping is then a pure translation; if it looks N times smaller, this is N.
#ifndef SIGHT_SIZE_SCALE
#define SIGHT_SIZE_SCALE 1.0f
#endif
// SIGHT_RETICLE_DISTANCE is where the reticle is projected, in metres. A real collimator puts it
// at infinity, which is the default here. Finite values are not a fudge: the Crysis VR mod uses
// 10 m deliberately, because a reticle at infinity tracks head movement one-for-one and in VR the
// eye is not held steady by a cheek weld, so tracking noise shows up as a wandering dot.
// The blend is exact -- k = D/(d+D) with d the eye-to-glass distance -- so 0 reproduces the stock
// painted-on reticle and a large value reproduces full collimation.
#ifndef SIGHT_RETICLE_DISTANCE
#define SIGHT_RETICLE_DISTANCE 1.0e6f
#endif
// ZEROING. The collimation above is built around the glass NORMAL -- the dot sits where the line
// of sight through the glass is perpendicular to it. A bullet does not care about the glass: it
// leaves along the bore, and a reflex sight's window is deliberately tilted relative to that. So
// unless the model happens to mount the glass square to the bore, the dot is off by that tilt.
//
// The bore direction is known to the mod (the plugin publishes the muzzle forward in shared slots
// [24..26], which is what the barrel dot draws from) but NOT reachable from here: the root
// signature is the game's and there is no spare constant to carry it in.
//
// So the sight is zeroed the way a real one is -- by moving the reticle on the glass. Units are
// uv, i.e. 1.0 is the full width of the reticle across the window. The dot moves OPPOSITE to the
// sign, exactly like a windage screw.
#ifndef SIGHT_USE_BORE
#define SIGHT_USE_BORE 1
#endif
// Draws a bright mark at the QUAD'S OWN CENTRE -- the place the stock shader paints the reticle,
// i.e. the geometric centre of the glass. The collimated reticle sits at the eye's lateral offset
// from the sight axis, so the gap between the two marks, measured in window widths, IS that
// offset divided by the glass width. One look per eye and the number stops being a deduction:
// if the eyes really are 65 mm apart on a 26 mm window, one of them must show the gap at 2.5
// widths -- and if instead both gaps are small, the glass is wider than the vertex buffer said
// and sizeU is the thing that is wrong.
#ifndef SIGHT_DEBUG_CENTRE
#define SIGHT_DEBUG_CENTRE 0
#endif

#ifndef SIGHT_ZERO_U
#define SIGHT_ZERO_U 0.0f
#endif
#ifndef SIGHT_ZERO_V
#define SIGHT_ZERO_V 0.0f
#endif

cbuffer GlobalShaderConstants : register(b0, space0) { float4 _19_m0[30]; };
cbuffer CameraShaderConstants : register(b1, space0) { float4 _24_m0[53]; };
cbuffer ShaderSpecificConsts  : register(b4, space0) { float4 _29_m0[21]; };

Texture2D<float4> _11[32768] : register(t0, space1);   // bindless: the noise map
Texture2D<float4> _13        : register(t0, space0);   // the reticle atlas
SamplerState      _32        : register(s0, space0);

static const float _47[4] = { 1.0f, 0.5f, 0.0f, 0.0f };
static const float _50[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
static const float _52[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

struct PSIn {
    float4 pos      : SV_Position;   // reg 0, unused -- declared so the signature matches
    float4 TEXCOORD : TEXCOORD0;     // reg 1: uv.xy, view z, 0
    float4 COLOR    : TEXCOORD1;     // reg 2: vertex colour (unused by the original)
    float4 NRM_BX   : TEXCOORD2;     // reg 3: normal.xyz, bitangent.x
    float4 BYZ_TXY  : TEXCOORD3;     // reg 4: bitangent.yz, tangent.xy
    float4 TZ_WPOS  : TEXCOORD4;     // reg 5: tangent.z, world position xyz
};

float4 main(PSIn IN) : SV_Target {
    const float3 T = float3(IN.BYZ_TXY.z, IN.BYZ_TXY.w, IN.TZ_WPOS.x);
    const float3 B = float3(IN.NRM_BX.w,  IN.BYZ_TXY.x, IN.BYZ_TXY.y);

    // Eye -> pixel. _24_m0[36] is the camera position in the same rebased space as the
    // interpolated world position, and it differs per eye by the interpupillary distance --
    // measured at 64 mm between the two views, so this term is already stereo-correct.
    const float3 d3 = IN.TZ_WPOS.yzw - _24_m0[36].xyz;
    const float  dist = max(length(d3), 1e-6f);
    const float3 V = d3 / dist;

    const float vT = dot(V, T);
    const float vB = _29_m0[18].x * dot(V, B);

    // --- the atlas rect, exactly as the original ------------------------------------------
    const float _120 = 1.0f - IN.TEXCOORD.y;
    const float _121 = _29_m0[12].z - _29_m0[12].x;      // rect width
    const float _122 = _29_m0[12].w - _29_m0[12].y;      // rect height
    const float _126 = (_121 * 0.01f) + _29_m0[12].x;
    const float _127 = (_122 * 0.01f) + _29_m0[12].y;
    const float _131 = (_121 * 0.99f) + _29_m0[12].x;
    const float _132 = (_122 * 0.99f) + _29_m0[12].y;

    // --- collimation ------------------------------------------------------------------------
    // The glass size comes from the VERTEX shader now (see sight_reflex_vs.hlsl), in the slot
    // that used to carry the vertex colour. That removes ddx/ddy from the coordinate entirely:
    // the previous two attempts derived the scale from derivatives, and DLSS's per-frame
    // sub-pixel jitter moved the 2x2 derivative quads, which made the reticle's texels run back
    // and forth. This value is per-vertex and static.
    const float sizeU = IN.COLOR.x * SIGHT_SIZE_SCALE;
    const float sizeV = IN.COLOR.y * SIGHT_SIZE_SCALE;

    const float ucoord = IN.TEXCOORD.x;
    const float vcoord = _120;                 // the original's own v, 1 - uv.y
    // Collimate along the sight's OPTICAL AXIS, not the glass normal. The vertex shader hands
    // down that axis already resolved into this tangent frame (see sight_reflex_vs.hlsl); with
    // both terms zero this reduces exactly to the previous behaviour, so it is a generalisation
    // rather than a replacement.
    const float3 N = IN.NRM_BX.xyz;
    const float  dn = dot(d3, N);
#if SIGHT_USE_BORE
    const float2 bore = IN.COLOR.zw;
#else
    const float2 bore = float2(0.0f, 0.0f);
#endif
    const float fT = dot(d3, T) - dn * bore.x;
    const float fB = (dot(d3, B) - dn * bore.y) * _29_m0[18].x;

    // fT is the eye-to-pixel vector on the glass's u axis: across the quad it is exactly
    // (pixel offset - eye offset) in world units, so dividing by the world size of one uv unit
    // puts the reticle centre where fT = 0 -- the point on the glass laterally aligned with the
    // eye, which is where a collimated sight puts its dot. Degenerate size: keep the stock
    // mapping rather than emit something arbitrary.
    // Reticle at a finite distance: k = D/(d+D). k = 1 is a collimator, k = 0 is paint on glass.
    const float kColl = SIGHT_RETICLE_DISTANCE / (dist + SIGHT_RETICLE_DISTANCE);

    float2 uvC;
    uvC.x = (sizeU > 1e-5f) ? (0.5f + fT / sizeU + SIGHT_ZERO_U) : ucoord;
    // Measured, not reasoned: with the minus sign the reticle moved the wrong way
    // vertically. The bitangent runs WITH the shader's own v (which is already 1 - uv.y).
    uvC.y = (sizeV > 1e-5f) ? (0.5f + fB / sizeV + SIGHT_ZERO_V) : vcoord;

    uvC = lerp(float2(ucoord, vcoord), uvC, kColl);

    const float _135 = (_121 * uvC.x) + _29_m0[12].x;
    const float _136 = (_122 * uvC.y) + _29_m0[12].y;

    float _146, _147, _148;
    if (_29_m0[15].x != 0.0f) { _146 = 1.0f - _136; _147 = 1.0f - _132; _148 = 1.0f - _127; }
    else                      { _146 = _136;        _147 = _127;        _148 = _132;        }

    const float _155 = min(max(_29_m0[7].x * _121, 0.0f), 0.5f);


    // Four layers as before. With the base coordinate now angular they read as a short smear
    // along the line of sight, which is what they were for; nothing here is invented.
    float4 _229 = _13.Sample(_32, float2(clamp(min(max(_135, _126), _131), 0.0f, 1.0f),
                                         clamp(min(max(_146, _147), _148), 0.0f, 1.0f)));
    float4 _235 = _13.Sample(_32, float2(clamp(min(max((_155 * vT) + _135, _126), _131), 0.0f, 1.0f),
                                         clamp(min(max((_155 * vB) + _146, _147), _148), 0.0f, 1.0f)));
    float4 _241 = _13.Sample(_32, float2(clamp(min(max(((vT * 2.0f) * _155) + _135, _126), _131), 0.0f, 1.0f),
                                         clamp(min(max(((vB * 2.0f) * _155) + _146, _147), _148), 0.0f, 1.0f)));
    float4 _247 = _13.Sample(_32, float2(clamp(min(max(((vT * 3.0f) * _155) + _135, _126), _131), 0.0f, 1.0f),
                                         clamp(min(max(((vB * 3.0f) * _155) + _146, _147), _148), 0.0f, 1.0f)));

    // --- everything below is the original, unchanged -----------------------------------------
    const float _259 = min(max(_29_m0[11].x, 0.75f), 1.0f);
    const float _262 = ((1.0f - _259) * _11[asuint(_29_m0[1]).x + 0u].Sample(_32,
        float2((_29_m0[9].z * _19_m0[0].x) + (_29_m0[9].x * IN.TEXCOORD.x),
               (_29_m0[9].w * _19_m0[0].x) + (_29_m0[9].y * IN.TEXCOORD.y))).x) + _259;

    const float _326 = 1.0f - ((((1.0f - ((_29_m0[8].y * _235.x) * _262)) * (1.0f - (_29_m0[8].x * _229.x))) * (1.0f - ((_262 * _241.x) * _29_m0[8].z))) * (1.0f - ((_262 * _247.x) * _29_m0[8].w)));
    const float _327 = 1.0f - ((((1.0f - ((_29_m0[8].y * _235.y) * _262)) * (1.0f - (_29_m0[8].x * _229.y))) * (1.0f - ((_262 * _241.y) * _29_m0[8].z))) * (1.0f - ((_262 * _247.y) * _29_m0[8].w)));
    const float _328 = 1.0f - ((((1.0f - ((_29_m0[8].y * _235.z) * _262)) * (1.0f - (_29_m0[8].x * _229.z))) * (1.0f - ((_262 * _241.z) * _29_m0[8].z))) * (1.0f - ((_262 * _247.z) * _29_m0[8].w)));
    const float _329 = 1.0f - ((((1.0f - ((_29_m0[8].y * _235.w) * _262)) * (1.0f - (_29_m0[8].x * _229.w))) * (1.0f - ((_262 * _241.w) * _29_m0[8].z))) * (1.0f - ((_262 * _247.w) * _29_m0[8].w)));

    const float _338 = (frac(_19_m0[0].x + 0.375f) * 6.0f) + 2.0f;
    const float _346 = frac(floor((_19_m0[0].x + 0.35747999f) * 60.0f) * 0.1031f);
    const float _349 = dot(_346.xxx, (_346 + 33.33f).xxx);
    const float _369 = frac(frac(((_346 + _346) + (_349 * 2.0f)) * (_349 + _346)) + IN.TEXCOORD.x)
                     / ((clamp(frac(((floor(_338 * IN.TEXCOORD.x) / _338) + 1.0f) * _19_m0[0].x), 0.0f, 1.0f) * 3.0f) + 1.0f);
    const float _378 = frac(((frac(_19_m0[0].x) * (floor(_369 * _338) / _338)) + (-0.16249999f)) * 0.1031f);
    const float _380 = dot(_378.xxx, (_378 + 33.33f).xxx);
    const uint  _391 = uint(int(frac(((_378 + _378) + (_380 * 2.0f)) * (_380 + _378)) * 4.0f));
    const float _400 = frac(_19_m0[0].x * 0.1031f);
    const float _402 = dot(_400.xxx, (_400 + 33.33f).xxx);
    const float _410 = frac(((_400 + _400) + (_402 * 2.0f)) * (_402 + _400));
    const float _417 = (_410 < 0.69999999f) ? 0.0f : 1.0f;
    const float _433 = frac(floor(_19_m0[0].x * 10.0f) * 0.1031f);
    precise float _435 = _433 * (_433 + 33.33f);
    precise float _436 = _435 * _435;

    float _445, _447, _449, _451;
    if (frac(_436 * 2.0f) < (1.0f - _29_m0[10].x)) {
        _445 = _326; _447 = _327; _449 = _328; _451 = _329;
    } else {
        if (((IN.TEXCOORD.x > 0.77999997f) && (IN.TEXCOORD.x < 0.85000002f))
         || (((IN.TEXCOORD.x > 0.019999999f) && (IN.TEXCOORD.x < 0.11999999f))
          || ((IN.TEXCOORD.x > 0.28000000f) && (IN.TEXCOORD.x < 0.54299998f)))) {
            float4 _544 = _13.Sample(_32, _369.xx);
            float4 _555 = _13.Sample(_32, float2(
                ((_417 * ((_410 * 0.024999998f) + (-0.019999999f))) + 0.0099999998f) + _135,
                ((_417 * ((_410 * 0.029999999f) + (-0.0099999998f))) + (-0.0099999998f)) + _146));
            _445 = (_555.x * 0.40000000f) + (_544.x * _47[_391]);
            _447 = (_555.y * 0.40000000f) + (_544.y * _50[_391]);
            _449 = (_555.z * 0.40000000f) + (_544.z * _52[_391]);
            _451 = (_555.w * 0.40000000f) + _544.w;
        } else {
            _445 = _326; _447 = _327; _449 = _328; _451 = _329;
        }
    }

    const float _461 = clamp((((IN.TEXCOORD.y * 4.0f) * _120) + 1.0f) - _29_m0[16].x, 0.0f, 1.0f);
    const float _473 = ((_461 * _445) * _29_m0[17].x) * _29_m0[2].x;
    const float _476 = ((_461 * _447) * _29_m0[17].y) * _29_m0[2].x;
    const float _479 = ((_461 * _449) * _29_m0[17].z) * _29_m0[2].x;
    const float _485 = clamp(max(max(min(1.0f, _473), min(1.0f, _476)), min(1.0f, _479)), 0.0f, 1.0f);
    const float _491 = (_29_m0[0].x * (_451 - _485)) + _485;
    const float _498 = ((1.0f - _491) * _29_m0[19].x) + _491;

    float4 o;
    o.x = min(max((-0.0f) - min((-0.0f) - (_473 * _498), 0.0f), 0.0f), 65000.0f);
    o.y = min(max((-0.0f) - min((-0.0f) - (_476 * _498), 0.0f), 0.0f), 65000.0f);
    o.z = min(max((-0.0f) - min((-0.0f) - (_479 * _498), 0.0f), 0.0f), 65000.0f);
    o.w = clamp(_498 * _29_m0[0].x, 0.0f, 1.0f);

#if SIGHT_DEBUG_CENTRE
    // Green cross-hair at the window's geometric centre, opaque and brighter than the reticle so
    // it cannot be mistaken for part of it.
    const float du_ = abs(ucoord - 0.5f);
    const float dv_ = abs(vcoord - 0.5f);
    if ((du_ < 0.006f && dv_ < 0.10f) || (dv_ < 0.006f && du_ < 0.10f)) {
        o = float4(0.0f, 8.0f, 0.0f, 1.0f);
    }
    // Ticks every 0.25 of a window width along the horizontal, so the gap can be READ rather than
    // estimated: one tick = a quarter of the glass width.
    const float g_ = abs(frac(ucoord * 4.0f + 0.5f) - 0.5f);
    if (g_ < 0.012f && dv_ > 0.34f && dv_ < 0.46f) {
        o = float4(0.0f, 3.0f, 4.0f, 1.0f);
    }
#endif
    return o;
}
