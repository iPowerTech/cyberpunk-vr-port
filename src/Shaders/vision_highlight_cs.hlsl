// Replacement for the engine's VisionMode_Highlight compute shader -- the pass that DRAWS the
// scanner / focus outline. Identified in capturedwithhmdinbrain.rdc at event 40073,
// Dispatch(384,384,1), SM6.0, shader hash 786e64c7288bce14f7706b9a61c264ff. Its own symbols name it:
// the tap table is "c_offsets" inside "VisionMode_Highlight", the colours are "Highlights.1.v".
//
// WHY IT IS REPLACED: outline THICKNESS exists nowhere else. Not in the scripts -- their whole surface
// is two colour INDICES, a through-walls flag, transition times, a priority and a pattern type, every
// one of which was tried live. Not in the archives either: no outline resource exists anywhere. The
// line is one texel wide because this shader scans a 3x3 neighbourhood, and in VR it reads thinner
// still, the width being fixed in PIXELS while this port renders 3072 wide against the 1920 the value
// was chosen for.
//
// WHAT DECIDES THE WIDTH, read out of the DXIL:
//
//     c_offsets[18] = { 0,0  -1,0  1,0  0,-1  0,1  -1,1  1,-1  -1,-1  1,1 }
//
// nine taps at radius ONE. Each iteration reads the highlight flags (t6) at threadId + offset, clamped
// to the view, and a pixel joins the silhouette when a neighbour's flags differ from its own. Scaling
// those offsets scales the band, and nothing else in the shader depends on them.
//
// The colour comes from Highlights.1.v[16] -- two palettes of eight, fill and outline, indexed by bits
// 2..4 of the flags texture. That is exactly the 0..7 that EFocusOutlineType and
// EFocusForcedHighlightType map onto in script, which is how this shader was recognised.
//
// The body below is the capture's own decompilation, kept as it came out so that no behaviour drifts;
// only the two tap sites are changed, and `c_offsets` is held as signed ints (the engine writes -1 as
// 4294967295 and relies on unsigned wraparound, which cannot be scaled).
//
// Rebuild after editing -- no plugin rebuild, the DLL loads this as a file:
//   dxc -T cs_6_0 -E main -Fo src\Shaders\vision_highlight_cs.dxil src\Shaders\vision_highlight_cs.hlsl

// ---- the knob ----------------------------------------------------------------------------------
// Tap radius multiplier. 1 reproduces the engine exactly; 2 doubles the band, and so on.
// KEEP THIS AT 1. Scaling either stencil was measured twice on the picture and both times the outline
// vanished entirely: at radius 4 the silhouette test's depth comparisons stop agreeing and the alpha
// comes out zero, at radius 10 the depth Laplacian saturates its own clamp. Width is added by
// VRP_WHITE_GROW below instead, which does not touch the engine's own decision at all.
//
// It scales ONLY c_offsets -- the neighbourhood of the highlight flags. The depth Laplacian
// (_333/_364/_400) stays at +/-1: widened to 10 it saturated its own clamp and the outline vanished
// altogether, measured on the picture. An earlier reading that scaling c_offsets changes nothing was
// taken while the substitution was not executing at all (an ordinal counter that never reset), so it
// proved nothing and is withdrawn.
static const int VRP_THICKNESS = 1;
// How many texels the WHITE outline -- ours -- grows by. The game's own colours are untouched.
static const int VRP_WHITE_GROW = 3;   // 0 = чистая копия движкового поведения

// Opacity of the added band. The engine's own line is opaque at its core, so 1 matches it.
static const float VRP_GROW_ALPHA = 1.0f;

static const float _52[16] = { 1.5f, 0.0f, 0.0f, 1.5f, 1.5f, 1.5f, 0.800000011920928955078125f, 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
static const float _59[16] = { 5.0f, 3.5f, 3.5f, 5.0f, 5.0f, 5.0f, 1.0f, 5.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
static const float _67[16] = { 1000.0f, 1000.0f, 20.0f, 20.0f, 30.0f, 20.0f, 40.0f, 60.0f, 1000.0f, 50.0f, 50.0f, 20.0f, 50.0f, 1000.0f, 20.0f, 20.0f };
static const float _73[16] = { 1001.0f, 1001.0f, 35.0f, 35.0f, 65.0f, 35.0f, 50.0f, 75.0f, 1001.0f, 65.0f, 65.0f, 35.0f, 65.0f, 1001.0f, 35.0f, 35.0f };
static const float _78[16] = { 1.0f, 0.20000000298023223876953125f, 0.20000000298023223876953125f, 0.100000001490116119384765625f, 0.20000000298023223876953125f, 0.100000001490116119384765625f, 0.20000000298023223876953125f, 0.25f, 0.20000000298023223876953125f, 1.0f, 1.0f, 1.0f, 1.0f, 0.20000000298023223876953125f, 1.0f, 1.0f };
static const int _85i[18] = { 0, 0, -1, 0, 1, 0, 0, -1, 0, 1, -1, 1, 1, -1, -1, -1, 1, 1 };
static const float _105[64] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.941176474094390869140625f, 0.70980393886566162109375f, 0.21568627655506134033203125f, 1.0f, 0.3529411852359771728515625f, 0.4823529422283172607421875f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.113725490868091583251953125f, 1.0f, 0.19607843458652496337890625f, 1.0f, 0.12549020349979400634765625f, 0.4823529422283172607421875f, 1.0f, 1.0f, 1.0f, 0.19607843458652496337890625f, 0.0f, 1.0f, 1.0f, 0.090196080505847930908203125f, 0.0431372560560703277587890625f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.113725490868091583251953125f, 1.0f, 0.19607843458652496337890625f, 1.0f, 1.0f, 0.0588235296308994293212890625f, 0.01960784383118152618408203125f, 1.0f, 0.3529411852359771728515625f, 0.4823529422283172607421875f, 1.0f, 1.0f, 0.99215686321258544921875f, 0.22745098173618316650390625f, 0.17647059261798858642578125f, 1.0f, 0.941176474094390869140625f, 0.70980393886566162109375f, 0.21568627655506134033203125f, 1.0f, 0.12549020349979400634765625f, 0.4823529422283172607421875f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

cbuffer _26_28 : register(b0, space0)
{
    float4 _28_m0[30] : packoffset(c0);
};

cbuffer _31_33 : register(b1, space0)
{
    float4 _33_m0[53] : packoffset(c0);
};

cbuffer _36_38 : register(b12, space0)
{
    float4 _38_m0[99] : packoffset(c0);
};

cbuffer _41_43 : register(b6, space0)
{
    float4 _43_m0[3] : packoffset(c0);
};

// DECLARED EXACTLY AS THE ENGINE DECLARES THEM, and that is not cosmetic. The capture's own resource
// list reads:
//     Texture2D<float> t0 t1 t2 t4 t5      Texture2D<int2> t6      Texture2D<float4> t7
// The first copy of this shader declared the single-channel ones as float4 and the flags as uint4.
// Measured: with those declarations every read of t6 came back ZERO -- a probe that painted white
// wherever the flags said "outlined" painted nothing at all -- while writes to u0/u1 worked fine (a
// paint-everything-red variant flooded the frame). A typed SRV read through a mismatched component
// type is what that looks like, so the declarations now match the original's, component for component.
Texture2D<float> _8 : register(t0, space0);
Texture2D<float> _9 : register(t1, space0);
Texture2D<float> _10 : register(t2, space0);
Texture2D<float> _11 : register(t4, space0);
Texture2D<float> _12 : register(t5, space0);
Texture2D<int2> _16 : register(t6, space0);
Texture2D<float4> _17 : register(t7, space0);
Texture2D<float4> _18 : register(t8, space0);
RWTexture2D<float4> _21 : register(u0, space0);
RWTexture2D<float4> _22 : register(u1, space0);
SamplerState _46 : register(s11, space0);

static uint3 gl_GlobalInvocationID;
struct SPIRV_Cross_Input
{
    uint3 gl_GlobalInvocationID : SV_DispatchThreadID;
};

void comp_main()
{
    float _145 = _38_m0[79u].z * _38_m0[27u].x;
    float _146 = _38_m0[79u].w * _38_m0[27u].y;
    float _147 = _146 + (-1.0f);
    float _149 = _145 + (-1.0f);
    uint _151 = 0u; uint _153 = 0u; float _155 = 0.0f; float _157 = 0.0f; float _159 = 0.0f;
    uint _161 = 0u; uint _163 = 0u;
    uint _168 = 0u; uint _170 = 0u; uint _172 = 0u; uint _174 = 0u; uint _176 = 0u; uint _178 = 0u;
    uint _298 = 0u; uint _299 = 0u; float _300 = 0.0f; float _301 = 0.0f; float _302 = 0.0f;
    uint _303 = 0u; uint _304 = 0u;
    uint _305 = 0u; uint _306 = 0u; uint _307 = 0u; uint _308 = 0u; uint _309 = 0u; uint _310 = 0u;
    uint _311 = 0u;
    uint _150 = 0u; uint _152 = 0u; float _154 = 0.0f; float _156 = 0.0f; float _158 = 0.0f;
    uint _160 = 0u; uint _162 = 0u; uint _164 = 0u;
    uint _166 = 0u; uint _169 = 0u; uint _171 = 0u; uint _173 = 0u;
    uint _175 = 0u; uint _177 = 0u;
    uint _198 = 0u; uint _199 = 0u; uint _203 = 0u; uint _216 = 0u;
    uint _218 = 0u; uint _222 = 0u;
    float _242 = 0.0f; float _251 = 0.0f; bool _253 = false;
    for (;;)
    {
        // THE THICKNESS, tap site 1 of 2. The engine adds c_offsets[i] to threadId at radius one;
        // here the offset is scaled and the clamp to the view rectangle is left exactly as it was.
        _198 = uint(int(min(max(float(int(gl_GlobalInvocationID.x) + (_85i[0u + (_164 * 2u)] * VRP_THICKNESS)), 0.0f), _149)));
        _199 = uint(int(min(max(float(int(gl_GlobalInvocationID.y) + (_85i[1u + (_164 * 2u)] * VRP_THICKNESS)), 0.0f), _147)));
        _203 = uint(_16.Load(int3(uint2(_198, _199), 0u)).y);
        _216 = _203 & 3u;
        _218 = (_203 >> 2u) & 7u;
        _222 = (_203 >> 5u) & 1u;
        _242 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_198, _199), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
        _251 = 1.0f / max((((_38_m0[26u].x * _10.Load(int3(uint2(uint(int(ceil(_38_m0[79u].x * float(int(_198))))), uint(int(ceil(_38_m0[79u].y * float(int(_199)))))), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
        _253 = _216 == 0u;
        uint _254 = 0u; uint _256 = 0u;
        if (_253) { _254 = 0u; _256 = 0u; }
        else
        {
            bool _269 = (_242 - _251) > ((clamp(_251 * 0.00999999977648258209228515625f, 0.0f, 1.0f) * 0.1500000059604644775390625f) + 0.0500000007450580596923828125f);
            _254 = uint(_269);
            _256 = uint(((_203 & 64u) != 0u) || (!_269));
        }
        uint fp0 = 0u; uint fp1 = 0u; uint fp2 = 0u; uint fp3 = 0u; uint fp4 = 0u;
        uint fp5 = 0u; float fp6 = 0.0f; float fp7 = 0.0f; float fp8 = 0.0f;
        uint fp9 = 0u; uint fp10 = 0u; uint fp11 = 0u; uint fp12 = 0u;
        if (_256 == 0u)
        {
            fp0 = _150; fp1 = _171; fp2 = _169; fp3 = _166; fp4 = _162; fp5 = _160;
            fp6 = _158; fp7 = _156; fp8 = _154; fp9 = _152; fp10 = _175; fp11 = _177; fp12 = _173;
        }
        else
        {
            bool _284 = (_203 & 2u) != 0u;
            uint _285 = _284 ? _218 : 0u;
            uint _286 = _285 + 0u;
            uint _289 = _285 + 0u;
            float _274 = 1.0f;
            if (asuint(_43_m0[2u]).z == 0u)
            {
                _274 = ((1.0f - (clamp((_242 - _52[_286]) / (_59[_285 + 0u] - _52[_286]), 0.0f, 1.0f) * (1.0f - clamp((_242 - _67[_289]) / (_73[_285 + 0u] - _67[_289]), 0.0f, 1.0f)))) * (_78[_285 + 0u] + (-1.0f))) + 1.0f;
            }
            else { _274 = 1.0f; }
            float _273 = _12.Load(int3(uint2(_198, _199), 0u)) * _274;
            uint _272 = 0u; float _275 = 0.0f; uint _276 = 0u;
            uint _277 = 0u; uint _278 = 0u; uint _279 = 0u;
            if ((_150 == 0u) && ((_203 & 1u) != 0u))
            {
                if (!(_152 == 0u))
                {
                    _298 = 1u; _299 = _152; _300 = _154; _301 = _156; _302 = _273; _303 = _160;
                    _304 = _254; _305 = _164; _306 = _218; _307 = _218; _308 = _216;
                    _309 = _173; _310 = _175; _311 = _177;
                    break;
                }
                _272 = 1u; _275 = _273; _276 = _254; _277 = _218; _278 = _218; _279 = _216;
            }
            else
            {
                _272 = _150; _275 = _158; _276 = _162; _277 = _166; _278 = _169; _279 = _171;
            }
            uint l0 = 0u; uint l1 = 0u; uint l2 = 0u; uint l3 = 0u; uint l4 = 0u;
            uint l5 = 0u; float l6 = 0.0f; float l7 = 0.0f; float l8 = 0.0f;
            uint l9 = 0u; uint l10 = 0u; uint l11 = 0u; uint l12 = 0u;
            if ((_152 == 0u) && _284)
            {
                if (!(_272 == 0u))
                {
                    _298 = _272; _299 = 1u; _300 = _273; _301 = _274; _302 = _275; _303 = _254;
                    _304 = _276; _305 = _164; _306 = _277; _307 = _278; _308 = _279;
                    _309 = _222; _310 = _218; _311 = _216;
                    break;
                }
                l0 = 0u; l1 = _279; l2 = _278; l3 = _277; l4 = _276; l5 = _254;
                l6 = _275; l7 = _274; l8 = _273; l9 = 1u; l10 = _218; l11 = _216; l12 = _222;
            }
            else
            {
                l0 = _272; l1 = _279; l2 = _278; l3 = _277; l4 = _276; l5 = _160;
                l6 = _275; l7 = _156; l8 = _154; l9 = _152; l10 = _175; l11 = _177; l12 = _173;
            }
            fp0 = l0; fp1 = l1; fp2 = l2; fp3 = l3; fp4 = l4; fp5 = l5;
            fp6 = l6; fp7 = l7; fp8 = l8; fp9 = l9; fp10 = l10; fp11 = l11; fp12 = l12;
        }
        _151 = fp0; _172 = fp1; _170 = fp2; _168 = fp3; _163 = fp4; _161 = fp5;
        _159 = fp6; _157 = fp7; _155 = fp8; _153 = fp9; _176 = fp10; _178 = fp11; _174 = fp12;
        uint _165 = _164 + 1u;
        if (_165 < 9u)
        {
            _150 = _151; _152 = _153; _154 = _155; _156 = _157; _158 = _159; _160 = _161;
            _162 = _163; _164 = _165; _166 = _168; _169 = _170; _171 = _172;
            _173 = _174; _175 = _176; _177 = _178;
            continue;
        }
        else
        {
            _298 = _151; _299 = _153; _300 = _155; _301 = _157; _302 = _159; _303 = _161;
            _304 = _163; _305 = _165; _306 = _168; _307 = _170; _308 = _172;
            _309 = _174; _310 = _176; _311 = _178;
            break;
        }
    }
    float _313 = 1.0f / _146;
    float _314 = float(int(gl_GlobalInvocationID.x));
    float _315 = float(int(gl_GlobalInvocationID.y));
    float _316 = (1.0f / _145) * _314;
    float _317 = _313 * _315;
    float _318 = _316 + (-0.5f);
    float _321 = (_317 + (-0.5f)) * 2.0f;
    float _326 = (_321 * _321) * (_318 * (-0.0089999996125698089599609375f));
    float _330 = ((_318 * _318) * (-0.17000000178813934326171875f)) * _321;
    uint _333 = gl_GlobalInvocationID.x + 4294967295u;   // x-1, NOT scaled: see the note above
    uint _334 = gl_GlobalInvocationID.y + 4294967295u;   // y-1, NOT scaled
    float _351 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_333, _334), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _352 = (-0.0f) - _351;
    float _362 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(gl_GlobalInvocationID.x, _334), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    uint _364 = gl_GlobalInvocationID.x + 1u;            // x+1, NOT scaled
    float _373 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_364, _334), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _384 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_333, gl_GlobalInvocationID.y), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _396 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_364, gl_GlobalInvocationID.y), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    uint _400 = gl_GlobalInvocationID.y + 1u;            // y+1, NOT scaled
    float _409 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_333, _400), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _419 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(gl_GlobalInvocationID.x, _400), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _430 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_364, _400), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _447 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
    float _451 = (((((((_351 - _373) + (_384 * 2.0f)) - (_396 * 2.0f)) + _409) - _430) / _447) * 0.5f) * _38_m0[25u].x;
    float _453 = ((((((_352 - _373) + _409) + _430) + ((_419 - _362) * 2.0f)) / _447) * 0.5f) * _38_m0[25u].x;
    float _474 = ((_146 * 400.0f) * _313) * (_330 + _317);
    float _475 = floor(_474);
    float _477 = (_475 * 0.5f) + (((_145 * 400.0f) * _313) * (_326 + _316));
    float _484 = frac(floor(_477) * 0.103100001811981201171875f);
    float _485 = frac(_475 * 0.103100001811981201171875f);
    float _488 = _484 + 33.3300018310546875f;
    float _489 = dot(float3(_484, _485, _484), float3(_485 + 33.3300018310546875f, _488, _488));
    float _509 = frac(_475 * (-0.103100001811981201171875f));
    precise float _511 = _509 * (_509 + 33.3300018310546875f);
    precise float _512 = _511 * _511;
    float _521 = exp2(log2(frac((_43_m0[2u].x * 5.0f) + frac(_512 * 2.0f))) * 0.300000011920928955078125f);
    float _522 = frac(_477) + (-0.5f);
    float _523 = frac(_474) + (-0.5f);
    float _528 = ((sin((frac(((_485 + _484) + (_489 * 2.0f)) * (_489 + _484)) * 40.0f) * _43_m0[2u].x) * 0.0500000007450580596923828125f) + 0.89999997615814208984375f) - sqrt((_523 * _523) + (_522 * _522));
    float _559 = 0.0f; float _561 = 0.0f; float _563 = 0.0f; float _565 = 0.0f;
    float _567 = 0.0f; float _569 = 0.0f; float _571 = 0.0f;
    if (_298 == 0u)
    {
        _559 = _447; _561 = _314; _563 = _315;
        _565 = 0.0f; _567 = 0.0f; _569 = 0.0f; _571 = 0.0f;
    }
    else
    {
        float _560 = 0.0f; float _562 = 0.0f; float _564 = 0.0f;
        uint _794 = 0u; uint _796 = 0u; uint _798 = 0u;
        if (_305 < 9u)
        {
            uint _795 = 0u; uint _797 = 0u; uint _799 = 0u;
            float _800 = 0.0f; float _801 = 0.0f; float _802 = 0.0f;
            uint _931 = _305; uint _933 = _306; uint _934 = _307; uint _935 = _308;
            float _936 = _447; float _937 = _314; float _938 = _315;
            uint _955 = 0u; uint _956 = 0u; float4 _958 = 0.0f.xxxx;
            uint _961 = 0u; uint _963 = 0u; bool _965 = false; bool _967 = false;
            for (;;)
            {
                // THE THICKNESS, tap site 2 of 2 -- the same table, scaled the same way.
                _955 = uint(int(min(max(float(int(gl_GlobalInvocationID.x) + (_85i[0u + (_931 * 2u)] * VRP_THICKNESS)), 0.0f), _149)));
                _956 = uint(int(min(max(float(int(gl_GlobalInvocationID.y) + (_85i[1u + (_931 * 2u)] * VRP_THICKNESS)), 0.0f), _147)));
                _958 = _38_m0[79u];
                _961 = uint(_16.Load(int3(uint2(_955, _956), 0u)).y);
                _963 = (_961 >> 2u) & 7u;
                _965 = (_961 & 64u) != 0u;
                _967 = (_961 & 1u) == 0u;
                float q0 = 0.0f; float q1 = 0.0f; uint q2 = 0u; uint q3 = 0u;
                uint q4 = 0u; float q5 = 0.0f;
                if (_967)
                {
                    q0 = _938; q1 = _937; q2 = _935; q3 = _934; q4 = _933; q5 = _936;
                }
                else
                {
                    uint _1029 = _961 & 3u;
                    float _1030 = float(int(_956));
                    float _1035 = float(int(_955));
                    float _1056 = 1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(_955, _956), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
                    float _1065 = 1.0f / max((((_38_m0[26u].x * _10.Load(int3(uint2(uint(int(ceil(_958.x * _1035))), uint(int(ceil(_1030 * _958.y)))), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
                    uint _1161 = 0u;
                    if (_1029 == 0u) { _1161 = 0u; }
                    else
                    {
                        _1161 = uint(_965 || (!((_1056 - _1065) > ((clamp(_1065 * 0.00999999977648258209228515625f, 0.0f, 1.0f) * 0.1500000059604644775390625f) + 0.0500000007450580596923828125f))));
                    }
                    bool _1165 = (_1161 != 0u) && (_1056 < _936);
                    q0 = _1165 ? _1030 : _938;
                    q1 = _1165 ? _1035 : _937;
                    q2 = _1165 ? _1029 : _935;
                    q3 = _1165 ? _963 : _934;
                    q4 = _1165 ? _963 : _933;
                    q5 = _1165 ? _1056 : _936;
                }
                _802 = q0; _801 = q1; _799 = q2; _797 = q3; _795 = q4; _800 = q5;
                uint _932 = _931 + 1u;
                if (_932 == 9u) { break; }
                else
                {
                    _931 = _932; _933 = _795; _934 = _797; _935 = _799;
                    _936 = _800; _937 = _801; _938 = _802;
                    continue;
                }
            }
            _794 = _795; _796 = _797; _798 = _799;
            _560 = _800; _562 = _801; _564 = _802;
        }
        else
        {
            _794 = _306; _796 = _307; _798 = _308;
            _560 = _447; _562 = _314; _564 = _315;
        }
        float _804 = clamp((((((((_352 - _362) - _373) - _384) - _396) - _409) - _419) - _430) + (_447 * 8.0f), 0.0f, 1.0f) * _302;
        float g0 = 0.0f; float g1 = 0.0f; float g2 = 0.0f; float g3 = 0.0f;
        float g4 = 0.0f; float g5 = 0.0f; float g6 = 0.0f;
        if (_804 > 0.00999999977648258209228515625f)
        {
            float _1173 = 0.0f; float _1174 = 0.0f; float _1175 = 0.0f;
            float _1176 = 0.0f; float _1177 = 0.0f; float _1178 = 0.0f;
            float _1179 = 0.0f; float _1180 = 0.0f;
            if ((_798 & 2u) == 0u)
            {
                _1173 = _105[0u + ((_794 + 8u) * 4u)];
                _1174 = _105[1u + ((_794 + 8u) * 4u)];
                _1175 = _105[2u + ((_794 + 8u) * 4u)];
                _1176 = _105[3u + ((_794 + 8u) * 4u)];
                _1177 = _105[0u + ((_794 + 8u) * 4u)];
                _1178 = _105[1u + ((_794 + 8u) * 4u)];
                _1179 = _105[2u + ((_794 + 8u) * 4u)];
                _1180 = _105[3u + ((_794 + 8u) * 4u)];
            }
            else
            {
                _1173 = _105[0u + ((_796 + 0u) * 4u)];
                _1174 = _105[1u + ((_796 + 0u) * 4u)];
                _1175 = _105[2u + ((_796 + 0u) * 4u)];
                _1176 = _105[3u + ((_796 + 0u) * 4u)];
                _1177 = _105[0u + ((_796 + 0u) * 4u)];
                _1178 = _105[1u + ((_796 + 0u) * 4u)];
                _1179 = _105[2u + ((_796 + 0u) * 4u)];
                _1180 = _105[3u + ((_796 + 0u) * 4u)];
            }
            bool _1181 = _304 != 0u;
            g0 = (_1181 ? (_1175 * _528) : _1179) * 1.2000000476837158203125f;
            g1 = clamp((((_302 * 1.2000000476837158203125f) * ((_521 * 0.20000000298023223876953125f) + 0.800000011920928955078125f)) * _804) * (_1181 ? (_1176 * _528) : _1180), 0.0f, 1.0f);
            g2 = (_1181 ? (_1174 * _528) : _1178) * 1.2000000476837158203125f;
            g3 = (_1181 ? (_1173 * _528) : _1177) * 1.2000000476837158203125f;
            g4 = _564; g5 = _562; g6 = _560;
        }
        else
        {
            g0 = 0.0f; g1 = 0.0f; g2 = 0.0f; g3 = 0.0f;
            g4 = _564; g5 = _562; g6 = _560;
        }
        _559 = g6; _561 = g5; _563 = g4; _565 = g3; _567 = g2; _569 = g0; _571 = g1;
    }
    float _583 = (((float(int(uint(int(_314)))) + 0.5f) * 2.0f) / _145) + (-1.0f);
    float _585 = (-0.0f) - ((((float(int(uint(int(_315)))) + 0.5f) * 2.0f) / _146) + (-1.0f));
    float _632 = (((_38_m0[24u].z + _38_m0[23u].z) + mad(_38_m0[22u].z, _585, _38_m0[21u].z * _583)) * _559) + _38_m0[0u].z;
    float _639 = (_38_m0[0u].x + (((mad(_38_m0[22u].x, _585, _38_m0[21u].x * _583) + _38_m0[23u].x) + _38_m0[24u].x) * _559)) - _33_m0[36u].x;
    float _640 = (_38_m0[0u].y + (((_38_m0[24u].y + _38_m0[23u].y) + mad(_38_m0[22u].y, _585, _38_m0[21u].y * _583)) * _559)) - _33_m0[36u].y;
    float _641 = _632 - _33_m0[36u].z;
    float _653 = (sqrt(((_639 * _639) + (_640 * _640)) + (_641 * _641)) * 0.20000000298023223876953125f) - (_28_m0[0u].z * 0.5f);
    float _662 = clamp(min(frac(_653) * 1.111111164093017578125f, frac((-0.0f) - _653) * 9.9999980926513671875f), 0.0f, 1.0f);
    float _667 = (_662 * _662) * (3.0f - (_662 * 2.0f));
    float _673 = 0.0f; float _675 = 0.0f; float _677 = 0.0f; float _679 = 0.0f;
    if (_299 == 0u)
    {
        _673 = 0.0f; _675 = 0.0f; _677 = 0.0f; _679 = 0.0f;
    }
    else
    {
        uint _702 = ((_311 & 2u) != 0u) ? _310 : 0u;
        bool _734 = _303 != 0u;
        float _674 = (_734 ? _105[0u + ((_702 + 0u) * 4u)] : _105[0u + ((_702 + 0u) * 4u)]) * 1.75f;
        float _676 = (_734 ? _105[1u + ((_702 + 0u) * 4u)] : _105[1u + ((_702 + 0u) * 4u)]) * 1.75f;
        float _678 = (_734 ? _105[2u + ((_702 + 0u) * 4u)] : _105[2u + ((_702 + 0u) * 4u)]) * 1.75f;
        float _764 = clamp(1.0f - ((1.0f - (clamp((_559 + (-30.0f)) * 0.014285714365541934967041015625f, 0.0f, 1.0f) * 0.5f)) * (1.0f - exp2((1.0f - (clamp(_559 * 0.02500000037252902984619140625f, 0.0f, 1.0f) * 0.699999988079071044921875f)) * log2(1.0f - ((1.0f - max(0.0f, _453 * rsqrt(dot(float3(_451, _453, 1.0f), float3(_451, _453, 1.0f))))) * (1.0f - min(min(1.0f, sqrt((_453 * _453) + (_451 * _451))), 0.5f))))))), 0.0f, 1.0f);
        float _769 = (_528 * 0.5f) + 0.5f;
        float _792 = ((_301 * _300) * ((_521 * 0.25f) + 0.75f)) * clamp((_304 != 0u) ? clamp(_764 * ((_528 * 1.2000000476837158203125f) + (-0.20000000298023223876953125f)), 0.0f, 1.0f) : (max(clamp((exp2(_8.Load(int3(uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y), 0u)) * (-64.0f)) + _9.Load(int3(uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y), 0u))) * _769, 0.0f, 1.0f), _764 * _769) * ((_702 == 1u) ? 0.75f : 0.5f)), 0.0f, 1.0f);
        float h0 = 0.0f; float h1 = 0.0f; float h2 = 0.0f; float h3 = 0.0f;
        if (_309 == 0u)
        {
            float _901 = (_28_m0[0u].z * 3.0f) + (_632 * 0.25f);
            float _913 = clamp((min(frac(_901) * 10.0f, frac((-0.0f) - _901) * 1.111111164093017578125f) + (-0.60000002384185791015625f)) * 2.5000002384185791015625f, 0.0f, 1.0f);
            int2 _916 = _16.Load(int3(uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y), 0u));
            uint _918 = uint(_916.y);
            uint _921 = (_918 >> 2u) & 7u;
            bool _970 = false; bool _973 = false;
            if ((_918 & 3u) == 0u)
            {
                _970 = false; _973 = false;
            }
            else
            {
                float _1015 = 1.0f / max((((_38_m0[26u].x * _10.Load(int3(uint2(uint(ceil(_38_m0[79u].x * _314)), uint(ceil(_38_m0[79u].y * _315))), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f);
                bool _971 = ((1.0f / max((((_38_m0[26u].x * _11.Load(int3(uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y), 0u))) + _38_m0[26u].y) * _38_m0[25u].x) + _38_m0[25u].y, 1.0000000116860974230803549289703e-07f)) - _1015) > ((clamp(_1015 * 0.00999999977648258209228515625f, 0.0f, 1.0f) * 0.1500000059604644775390625f) + 0.0500000007450580596923828125f);
                _970 = _971;
                _973 = ((_918 & 64u) != 0u) || (!_971);
            }
            float _1150 = 0.0f;
            if (((_918 & 1u) != 0u) && _973)
            {
                float _1216 = 0.0f; float _1217 = 0.0f;
                if ((_918 & 2u) == 0u)
                {
                    _1216 = _105[0u + ((_921 + 8u) * 4u)];
                    _1217 = _105[0u + ((_921 + 8u) * 4u)];
                }
                else
                {
                    _1216 = _105[0u + ((_921 + 0u) * 4u)];
                    _1217 = _105[0u + ((_921 + 0u) * 4u)];
                }
                precise float _1151 = (_970 ? _1216 : _1217) * 8.0f;
                _1150 = _1151;
            }
            else { _1150 = 0.0f; }
            h0 = _674;
            h1 = (clamp(_1150, 0.0f, 1.0f) + 1.0f) * clamp((((_913 * _913) * (_792 * 0.3499999940395355224609375f)) * (3.0f - (_913 * 2.0f))) + _792, 0.0f, 1.0f);
            h2 = _676;
            h3 = _678;
        }
        else
        {
            h0 = _674;
            h1 = _792 * ((_702 != 1u) ? ((_667 * 0.5f) + 0.5f) : ((_667 * 0.125f) + 0.875f));
            h2 = _676;
            h3 = _678;
        }
        _673 = h0; _675 = h2; _677 = h3; _679 = h1;
    }
    float _690 = ((_565 - _673) * _571) + _673;
    float _691 = ((_567 - _675) * _571) + _675;
    float _692 = ((_569 - _677) * _571) + _677;
    float _693 = ((_571 - _679) * _571) + _679;
    // ---- OUR OWN LINE, THICKER, AND ONLY OURS ------------------------------------------------
    //
    // The engine's decision above is left exactly as it was; this adds pixels around it. A pixel that
    // got no outline of its own looks at the highlight FLAGS in a small neighbourhood, and if a
    // neighbour is outlined in WHITE it takes that white too. Two things make this safe where scaling
    // the stencils was not: it reads the flags texture, not depth, so no silhouette test can collapse;
    // and it only ever ADDS, so every outline the engine drew stays untouched.
    //
    // WHITE IS THE MARKER FOR OURS. The palette's second set of eight is the outline colour, and its
    // entry 7 is (1,1,1,1) -- the one the game never picks for its own loot, quests or hostiles. So
    // flags colour index 7 on the +8 branch means "this port asked for it", and nothing else does.
    float _885 = 0.0f; float _886 = 0.0f; float _887 = 0.0f; float _888 = 0.0f;
    if ((_306 != 2u) && (_43_m0[2u].y > 0.0f))
    {
        float4 _817 = _17.Load(int3(uint2(uint(int(ceil(_38_m0[79u].x * _561))), uint(int(ceil(_38_m0[79u].y * _563)))), 0u));
        float _821 = _817.x * _145;
        float _822 = _817.y * _146;
        float _832 = (_43_m0[2u].y * 0.800000011920928955078125f) / ((sqrt((_821 * _821) + (_822 * _822)) * 0.0199999995529651641845703125f) + 1.0f);
        bool _834 = (_299 | _298) != 0u;
        float _836 = _834 ? 1.5f : 3.0f;
        precise float _837 = _836 * (_326 + 1.0f);
        precise float _838 = _836 * _330;
        float _839 = _834 ? (_832 * 0.75f) : _832;
        float _840 = _314 + 0.5f;
        float _841 = _315 + 0.5f;
        float4 _854 = _18.SampleLevel(_46, float2(_43_m0[1u].z * ((_840 - _837) + _821), _43_m0[1u].w * ((_841 - _838) + _822)), 0.0f);
        float4 _867 = _18.SampleLevel(_46, float2(_43_m0[1u].z * ((_840 + _837) + _821), _43_m0[1u].w * ((_841 + _838) + _822)), 0.0f);
        _885 = max(_690, max(_854.x, _867.x) * _839);
        _886 = max(_691, max(_854.y, _867.y) * _839);
        _887 = max(_692, max(_854.z, _867.z) * _839);
        _888 = max(_693, max(_854.w, _867.w) * _839);
    }
    else
    {
        _885 = _690; _886 = _691; _887 = _692; _888 = _693;
    }
    // DIAGNOSTIC, TEMPORARY. Three different nodes were swapped and the picture did not change once --
    // not even the corruption a foreign pass running this shader would have to produce. So the question
    // is no longer "which node" but "does the substitution reach the GPU at all", and this answers it:
    // whatever pass we land in gets painted red. Nothing changing means the swap never executes.
    // THE PORT'S OWN LINE, THICKER -- and only ours.
    //
    // The engine's own decision above is untouched; this adds a band of white OUTSIDE every silhouette
    // that the port asked to outline in white. Both of the conditions this block first carried are
    // gone, and each was measured to be the reason it did nothing:
    //
    //   * "only where the layer is empty" (_888 <= 0.002). The halo the engine adds at the end of this
    //     shader is non-zero exactly in the band we want, so the gate was closed where it mattered.
    //     Raising the alpha only when ours is greater is the same safety property without the gate:
    //     the pass can add, never remove.
    //   * "alpha from the strength texture t5". Read one texel outside the object it comes back zero,
    //     so the band was painted with alpha 0 -- invisible. The band gets a solid alpha of its own.
    //
    // WHITE IS THE MARKER FOR OURS: palette entry 15, which is colour index 7 on the outline branch,
    // is (1,1,1,1) and the game never chooses it for its own loot, quests or hostiles. Confirmed on the
    // picture with a probe that painted the band green when a neighbour passed this test and red when
    // one was highlighted but not white: green appeared around DISTRACTION alone, red around the other
    // two.
    //
    // A pixel that belongs to ANY highlighted object is skipped, which is what keeps this a thicker
    // line instead of a fill, and keeps the interior of the game's own highlights unchanged.
    if (VRP_WHITE_GROW > 0)
    {
        const uint ownf = uint(_16.Load(int3(int2(gl_GlobalInvocationID.xy), 0)).y);
        if ((ownf & 1u) == 0u)
        {
            float whiteNear = 0.0f;
            [unroll] for (int gy2 = -VRP_WHITE_GROW; gy2 <= VRP_WHITE_GROW; ++gy2)
            {
                [unroll] for (int gx2 = -VRP_WHITE_GROW; gx2 <= VRP_WHITE_GROW; ++gx2)
                {
                    if (gx2 == 0 && gy2 == 0) continue;
                    // Round, not square: a square neighbourhood thickens the diagonals half again as
                    // much and the corners of a silhouette read as blobs.
                    if (gx2 * gx2 + gy2 * gy2 > VRP_WHITE_GROW * VRP_WHITE_GROW) continue;
                    const uint nx = uint(int(min(max(float(int(gl_GlobalInvocationID.x) + gx2), 0.0f), _149)));
                    const uint ny = uint(int(min(max(float(int(gl_GlobalInvocationID.y) + gy2), 0.0f), _147)));
                    const uint nf = uint(_16.Load(int3(uint2(nx, ny), 0u)).y);
                    if ((nf & 1u) == 0u) continue;          // not part of a highlight
                    if ((nf & 2u) != 0u) continue;          // the fill branch, not the outline one
                    if (((nf >> 2u) & 7u) != 7u) continue;  // not white, so not ours
                    whiteNear = 1.0f;
                }
            }
            if (whiteNear > 0.0f && VRP_GROW_ALPHA > _888)
            {
                _885 = 1.0f; _886 = 1.0f; _887 = 1.0f;
                _888 = VRP_GROW_ALPHA;
            }
        }
    }
    _22[uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y)] = float4(_885, _886, _887, _888);
    _21[uint2(gl_GlobalInvocationID.x, gl_GlobalInvocationID.y)] = float4(_888 * _885, _888 * _886, _888 * _887, _888);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_GlobalInvocationID = stage_input.gl_GlobalInvocationID;
    comp_main();
}
