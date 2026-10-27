"""2.5 engine stereo: the deferred ambient pass takes each eye's reflections from the eye that traced them further
(2026-10-23, the brick wall that was lit differently in the two eyes, shadowcheckSBS.rdc).

Under engine stereo (Stereo/EngineStereo.cpp, feature 98) both eyes are one side-by-side frame. The SSR trace
marches each pixel's ray in its own eye's half and stops at that half's edge, so near each eye's INNER edge
(where its lens window ends and the other eye's goes on) rays leave the picture, the confidence drops and the
ambient pass falls back to the probe -- brighter than the reflection there: the right eye's left edge and the
left eye's right edge are each lit differently from the same wall in the other eye (+0.3..+0.9 and
-0.5..-1.6 log2 in the ambient output, the middle ~0). The same surface sits well inside the OTHER eye's
picture, whose rays found what this eye's could not.

The ambient pass (one fullscreen draw per eye, b1 row 53.x = 1 for the left eye) reads the SSR colour (t8) and
confidence (t9) at its own pixel, then composites

    result = probe + (exposure * colour - probe) * confidence

The twin reprojects the pixel's world position (the shader's own, from the eye's inverse in b12 rows 81-84 /
101-104) into the other eye through that eye's world->clip rows (b12 93-96 left, 113-116 right; checked
against the inverses on the capture to a quarter pixel), and where the other eye shows the same surface
(reverse-Z depth within TOLERANCE) and found more, moves towards its result by

    w = saturate((c_other - c_own) * STRENGTH)
    c = lerp(c_own, c_other, w)       colour = ((1 - w) c_own colour_own + w c_other colour_other) / c

-- the composite of lerp(own result, other result, w), so where both eyes traced the same the eye keeps its
own reflection (and its parallax) and only what it lost comes from the other eye. First-person pixels (the
hands and weapon, their own inverse) and the mono branch (b0 row 32.x == 0, the VRCAM path) are left as
they are; so is every pixel at full confidence.

Output: <FNV-1a of the original>.dxil signed containers for `stereo_merge\\` (Render/HiddenAreaShaders.cpp
group 8, CyberpunkVR_StereoMergePatch).

  py -3.13 -B stereo_ssr_merge_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

# The deferred ambient pass and its permutations in staticshader_final.cache (every pixel shader there that has
# the structure below; 19fe... is the one shadowcheckSBS.rdc ran).
SHADERS = {
    0x19fe7d5422b1206e: 'deferred ambient + SSR composite (one draw per eye)',
    0x056eafbcc8ee2a87: 'deferred ambient, permutation',
    0x2188c71b84b45ded: 'deferred ambient, permutation',
    0x68de2bc2f8971207: 'deferred ambient, permutation',
    0x6a766457f5744bdd: 'deferred ambient, permutation',
    0x710492da1c504db5: 'deferred ambient, permutation',
    0xb39515454d8975e2: 'deferred ambient, permutation',
    0xe3e6c1d2d6391955: 'deferred ambient, permutation',
}
TOLERANCE = 0.03       # reverse-Z depth, relative: the same surface
STRENGTH = 4.0         # all the way to the other eye at a quarter more confidence
FORWARD_ROWS = (93, 113)   # b12 world->clip: left eye, right eye (rows k..k+3 = clip x, y, z, w)
INVERSE_ROWS = (81, 101)   # b12 pixel->world the shader picks by eye (the twin only checks they are read)
EYE_ROW = 53           # b1: x = 1 for the left eye's draw

HANDLE = r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 {cls}, i32 \d+, i32 {reg}, i1 false\)'
F = '%dx.types.CBufRet.f32'
I = '%dx.types.CBufRet.i32'


def handle(ir, cls, register):
    found = re.findall(HANDLE.format(cls=cls, reg=register), ir)
    if len(found) != 1:
        raise ValueError('expected one handle for class %d register %d, found %d' % (cls, register, len(found)))
    return found[0]


def one(pattern, ir, what):
    found = re.findall(pattern, ir)
    if len(found) != 1:
        raise ValueError('%s: %d matches' % (what, len(found)))
    return found[0]


def texture_load(ir, srv, what):
    return one(r'(?m)^  (%\d+) = call %dx\.types\.ResRet\.f32 @dx\.op\.textureLoad\.f32\(i32 66, %dx\.types\.Handle ' +
               re.escape(srv) + r', i32 0, i32 (%\d+), i32 (%\d+), i32 undef', ir, what)


def extract(ir, value, comp, kind='%dx.types.ResRet.f32'):
    return one(r'(?m)^  (%\d+) = extractvalue ' + re.escape(kind) + ' ' + re.escape(value) + ', %d$' % comp, ir,
               'component %d of %s' % (comp, value))


def merge(ir):
    t0, t8, t9 = (handle(ir, 0, r) for r in (0, 8, 9))
    b0, b1, b12 = (handle(ir, 2, r) for r in (0, 1, 12))
    for row in INVERSE_ROWS:
        if not re.search(r'(?m)^  %\d+ = call ' + re.escape(F) + r' @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle ' +
                         re.escape(b12) + ', i32 %d\\)' % row, ir):
            raise ValueError('b12 row %d is not read: not the block layout the twin knows' % row)
    if not re.search(r'(?m)^  %\d+ = call ' + re.escape(I) + r' @dx\.op\.cbufferLoadLegacy\.i32\(i32 59, %dx\.types\.Handle ' +
                     re.escape(b1) + ', i32 %d\\)' % EYE_ROW, ir):
        raise ValueError('b1 row %d (the eye) is not read' % EYE_ROW)

    # The confidence at the pixel, the branch that loads the colour only where it is > 0, and its join.
    conf_load, x, y = texture_load(ir, t9, 'SSR confidence load')
    conf = extract(ir, conf_load, 0)
    test = one(r'(?m)^  (%\d+) = fcmp fast ogt float ' + re.escape(conf) + r', 0\.000000e\+00$', ir, 'confidence test')
    colour_block, join = one(r'(?m)^  br i1 ' + re.escape(test) + r', label %(\d+), label %(\d+)', ir, 'colour branch')
    colour_load, cx, cy = texture_load(ir, t8, 'SSR colour load')
    if (cx, cy) != (x, y):
        raise ValueError('colour and confidence read different pixels')
    own = []
    for comp in range(3):
        value = extract(ir, colour_load, comp)
        own.append(one(r'(?m)^  (%\d+) = phi float \[ ' + re.escape(value) + ', %' + colour_block +
                       r' \], \[ 0\.000000e\+00, %\d+ \]$', ir, 'colour phi %d' % comp))
    depth_load, dx, dy = texture_load(ir, t0, 'depth load')
    if (dx, dy) != (x, y):
        raise ValueError('depth read at another pixel')
    depth = extract(ir, depth_load, 0)

    # Mono: b0 row 32.x == 0, the test in the entry block (read again further on).
    entry = ir[:ir.index('\n; <label>:')]
    flag_load = one(r'(?m)^  (%\d+) = call ' + re.escape(I) + r' @dx\.op\.cbufferLoadLegacy\.i32\(i32 59, %dx\.types\.Handle ' +
                    re.escape(b0) + r', i32 32\)', entry, 'stereo flag load in the entry block')
    flag = extract(ir, flag_load, 0, I)
    mono = one(r'(?m)^  (%\d+) = icmp eq i32 ' + re.escape(flag) + ', 0$', ir, 'mono test')

    # The world position: the mono inverse (b12 rows 69-72) ends in three fdivs by one w; their phis with the
    # stereo value, which is select(first person, FPP inverse, eye inverse).
    mono_start = ir.index(re.search(r'(?m)^  %\d+ = call ' + re.escape(F) + r' @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                                    r'%dx\.types\.Handle ' + re.escape(b12) + r', i32 69\)', ir).group(0))
    divs = re.findall(r'(?m)^  (%\d+) = fdiv fast float %\d+, (%\d+)$', ir[mono_start:])[:3]
    if len(divs) != 3 or len({w for _, w in divs}) != 1:
        raise ValueError('mono world position: no three fdivs by one w')
    world, fpp = [], None
    for value, _ in divs:
        stereo, mono_value = one(r'(?m)^  %\d+ = phi float \[ (%\d+), %\d+ \], \[ (' + re.escape(value) + r'), %\d+ \]$', ir,
                                 'world phi of ' + value)
        world.append(one(r'(?m)^  (%\d+) = phi float \[ ' + re.escape(stereo) + r', %\d+ \], \[ ' + re.escape(mono_value) +
                         r', %\d+ \]$', ir, 'world phi'))
        flag_fpp = one(r'(?m)^  ' + re.escape(stereo) + r' = select i1 (%\d+), float %\d+, float %\d+$', ir, 'first-person select')
        if fpp not in (None, flag_fpp):
            raise ValueError('world components picked by different selects')
        fpp = flag_fpp

    # Split the join after its phis: everything that followed moves to %sm.join, so its successors' phis name
    # %sm.join as their predecessor, and every later use of the confidence and colour takes the merged value.
    head = re.search(r'(?m)^; <label>:' + join + r' .*\n((?:  %\d+ = phi .*\n)+)', ir)
    if not head:
        raise ValueError('join block %s has no phis' % join)
    cut = head.end()
    tail = re.sub(r', %' + join + r' \]', ', %sm.join ]', ir[cut:])
    for old, new in zip([conf] + own, ['%sm.c', '%sm.r', '%sm.g', '%sm.b']):
        tail = re.sub(re.escape(old) + r'\b', new, tail)

    lit = ham.lit
    rows = ['  %%sm.row%d = add i32 %%sm.base, %d' % (k, k) for k in (1, 2, 3)]
    for k in range(4):
        index = '%sm.base' if k == 0 else '%%sm.row%d' % k
        rows.append('  %%sm.m%d = call %s @dx.op.cbufferLoadLegacy.f32(i32 59, %%dx.types.Handle %s, i32 %s)' % (k, F, b12, index))
        rows += ['  %%sm.m%d%s = extractvalue %s %%sm.m%d, %d' % (k, 'xyzw'[c], F, k, c) for c in range(4)]
        rows.append('  %%sm.d%d = call float @dx.op.dot3.f32(i32 55, float %%sm.m%dx, float %%sm.m%dy, float %%sm.m%dz, '
                    'float %s, float %s, float %s)' % (k, k, k, k, world[0], world[1], world[2]))
        rows.append('  %%sm.c%d = fadd fast float %%sm.d%d, %%sm.m%dw' % (k, k, k))
    load = lambda name, srv: ('  %%sm.%s = call %%dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %%dx.types.Handle %s, '
                              'i32 0, i32 %%sm.tx, i32 %%sm.ty, i32 undef, i32 undef, i32 undef, i32 undef)' % (name, srv))
    body = [
        '  %sm.stereo = xor i1 ' + mono + ', true',
        '  %sm.part = fcmp fast olt float ' + conf + ', 1.000000e+00',
        '  %sm.eye = xor i1 ' + fpp + ', true',
        '  %sm.near = fcmp fast ogt float ' + depth + ', 0.000000e+00',
        '  %sm.g0 = and i1 %sm.stereo, %sm.part',
        '  %sm.g1 = and i1 %sm.g0, %sm.eye',
        '  %sm.go = and i1 %sm.g1, %sm.near',
        '  br i1 %sm.go, label %sm.body, label %sm.join',
        '',
        'sm.body:                                          ; preds = %' + join,
        '  %%sm.dims = call %s @dx.op.cbufferLoadLegacy.f32(i32 59, %%dx.types.Handle %s, i32 3)' % (F, b0),
        '  %%sm.w = extractvalue %s %%sm.dims, 0' % F,
        '  %%sm.h = extractvalue %s %%sm.dims, 1' % F,
        '  %sm.hw = fmul fast float %sm.w, 5.000000e-01',
        '  %%sm.eyerow = call %s @dx.op.cbufferLoadLegacy.i32(i32 59, %%dx.types.Handle %s, i32 %d)' % (I, b1, EYE_ROW),
        '  %%sm.eyeval = extractvalue %s %%sm.eyerow, 0' % I,
        '  %sm.left = icmp eq i32 %sm.eyeval, 1',
        # the OTHER eye's world->clip
        '  %%sm.base = select i1 %%sm.left, i32 %d, i32 %d' % (FORWARD_ROWS[1], FORWARD_ROWS[0]),
    ] + rows + [
        '  %sm.iw = fdiv fast float 1.000000e+00, %sm.c3',
        '  %sm.nx = fmul fast float %sm.c0, %sm.iw',
        '  %sm.ny = fmul fast float %sm.c1, %sm.iw',
        '  %sm.nz = fmul fast float %sm.c2, %sm.iw',
        '  %sm.qw = fmul fast float %sm.hw, 5.000000e-01',
        '  %sm.qh = fmul fast float %sm.h, 5.000000e-01',
        '  %sm.nqh = fsub fast float -0.000000e+00, %sm.qh',
        '  %sm.ex = call float @dx.op.tertiary.f32(i32 46, float %sm.nx, float %sm.qw, float %sm.qw)',
        '  %sm.ey = call float @dx.op.tertiary.f32(i32 46, float %sm.ny, float %sm.nqh, float %sm.qh)',
        '  %sm.v0 = fcmp fast ogt float %sm.c3, 0.000000e+00',
        '  %sm.v1 = fcmp fast oge float %sm.ex, 0.000000e+00',
        '  %sm.v2 = fcmp fast olt float %sm.ex, %sm.hw',
        '  %sm.v3 = fcmp fast oge float %sm.ey, 0.000000e+00',
        '  %sm.v4 = fcmp fast olt float %sm.ey, %sm.h',
        '  %sm.v01 = and i1 %sm.v0, %sm.v1',
        '  %sm.v23 = and i1 %sm.v2, %sm.v3',
        '  %sm.v03 = and i1 %sm.v01, %sm.v23',
        '  %sm.inside = and i1 %sm.v03, %sm.v4',
        '  %sm.hw1 = fadd fast float %sm.hw, -1.000000e+00',
        '  %sm.h1 = fadd fast float %sm.h, -1.000000e+00',
        '  %sm.exa = call float @dx.op.binary.f32(i32 35, float %sm.ex, float 0.000000e+00)',
        '  %sm.exc = call float @dx.op.binary.f32(i32 36, float %sm.exa, float %sm.hw1)',
        '  %sm.eya = call float @dx.op.binary.f32(i32 35, float %sm.ey, float 0.000000e+00)',
        '  %sm.eyc = call float @dx.op.binary.f32(i32 36, float %sm.eya, float %sm.h1)',
        # the other eye's half: the right one for the left eye
        '  %sm.off = select i1 %sm.left, float %sm.hw, float 0.000000e+00',
        '  %sm.sx = fadd fast float %sm.exc, %sm.off',
        '  %sm.tx = fptoui float %sm.sx to i32',
        '  %sm.ty = fptoui float %sm.eyc to i32',
        load('dl', t0),
        '  %sm.dq = extractvalue %dx.types.ResRet.f32 %sm.dl, 0',
        load('cl', t9),
        '  %sm.cq = extractvalue %dx.types.ResRet.f32 %sm.cl, 0',
        load('ql', t8),
        '  %sm.q0 = extractvalue %dx.types.ResRet.f32 %sm.ql, 0',
        '  %sm.q1 = extractvalue %dx.types.ResRet.f32 %sm.ql, 1',
        '  %sm.q2 = extractvalue %dx.types.ResRet.f32 %sm.ql, 2',
        '  %sm.dd = fsub fast float %sm.dq, %sm.nz',
        '  %sm.ad = call float @dx.op.unary.f32(i32 6, float %sm.dd)',
        '  %%sm.lim = fmul fast float %%sm.nz, %s' % lit(TOLERANCE),
        '  %sm.same = fcmp fast olt float %sm.ad, %sm.lim',
        '  %sm.ok = and i1 %sm.inside, %sm.same',
        '  %sm.gain = fsub fast float %sm.cq, ' + conf,
        '  %%sm.ws = fmul fast float %%sm.gain, %s' % lit(STRENGTH),
        '  %sm.wsat = call float @dx.op.unary.f32(i32 7, float %sm.ws)',
        '  %sm.wt = select i1 %sm.ok, float %sm.wsat, float 0.000000e+00',
        '  %sm.has = fcmp fast ogt float %sm.wt, 0.000000e+00',
        '  %sm.fc = call float @dx.op.tertiary.f32(i32 46, float %sm.wt, float %sm.gain, float ' + conf + ')',
        '  %sm.keep = fsub fast float 1.000000e+00, %sm.wt',
        '  %sm.ao = fmul fast float %sm.keep, ' + conf,
        '  %sm.bq = fmul fast float %sm.wt, %sm.cq',
        '  %sm.ic = fdiv fast float 1.000000e+00, %sm.fc',
    ]
    for comp, name in enumerate('rgb'):
        body += [
            '  %%sm.p%s = fmul fast float %%sm.ao, %s' % (name, own[comp]),
            '  %%sm.s%s = call float @dx.op.tertiary.f32(i32 46, float %%sm.bq, float %%sm.q%d, float %%sm.p%s)' % (name, comp, name),
            '  %%sm.u%s = fmul fast float %%sm.s%s, %%sm.ic' % (name, name),
            '  %%sm.m%s = select i1 %%sm.has, float %%sm.u%s, float %s' % (name, name, own[comp]),
        ]
    body += [
        '  %sm.mc = select i1 %sm.has, float %sm.fc, float ' + conf,
        '  br label %sm.join',
        '',
        'sm.join:                                          ; preds = %sm.body, %' + join,
        '  %%sm.c = phi float [ %s, %%%s ], [ %%sm.mc, %%sm.body ]' % (conf, join),
    ] + ['  %%sm.%s = phi float [ %s, %%%s ], [ %%sm.m%s, %%sm.body ]' % (name, own[comp], join, name)
         for comp, name in enumerate('rgb')]
    return ir[:cut] + '\n'.join(body) + '\n' + tail


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = ham.assemble(merge(ham.disassemble(original, work)), work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True, help='original shaders named <fnv1a>.dxil')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for key, name in SHADERS.items():
        source = args.shaders / ('%016x.dxil' % key)
        if not source.exists():
            print('MISSING %016x %s' % (key, name)); failed += 1; continue
        original = source.read_bytes()
        if ham.fnv1a(original) != key:
            raise RuntimeError('%s does not hash to its name' % source)
        try:
            patched = build(original)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d -> %d bytes' % (key, name, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
