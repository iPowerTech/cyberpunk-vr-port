"""The shadow mask's sun contact shadow as one wide display (Render/SsrWideMerge.cpp, channel kShadowMask).

RenderShadowmask (node 0x786BCC) draws the sun shadow factor of every pixel (shader 0f7d..., x of its
target; y packs the local lights' bits) from the cascades -- the same in both eyes, the asymmetric projection
fits them to the union window -- and a screen-space contact march towards the sun: a few depth samples along
a straight screen line, uv = uv0 + t d for t in [t0, t0 + 1). A sample whose uv leaves the picture finds
nothing, so where the line runs out over an eye's inner edge the contact shadow thins out in that eye while
the other eye, which sees past that edge, still finds the occluder.

The twin keeps every pixel's sun factor, the fraction of its march that left the picture (closed form: the
part of [t0, t0 + 1) beyond the line's exit from [0, 1]^2) and its depth in a store of its own (u16), and
moves towards the other eye's factor at the same surface point (u17) by STRENGTH x the difference of the lost
fractions -- the wide merge of asym_ssr_shaders.py: store, header ring, lookup through the pass that wrote the
entry. Its header is the pass's world->clip matrix (b1 rows 0-3) with an identity rotation, so the lookup
projects the world point straight into the other eye's picture and compares projected w.

The mask is drawn checkerboarded -- half the columns per frame, x = 2 px + parity -- so the store is indexed by
the half-width column the pixel shader runs on. The plugin fills the padding of b6 (PIX, 56 bytes uploaded
as 64): row 3.z the pass stamp, row 3.w SHADOW_TAG | mode (diagnostics in the low 4 bits); without the tag the
twin draws exactly what the engine's shader does.

Output: <FNV-1a of the original>.dxil from `asym_shadow\\` (Render/HiddenAreaShaders.cpp, CyberpunkVR_AsymShadowPatch).

  py -3.13 -B asym_shadow_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc
import asym_ssr_shaders as ssr

SHADERS = {
    0x0f7d6dab2e706d58: 'RenderShadowmask sun and local shadows with contact shadows',
}
SHADOW_TAG = 0x41734D50          # Render/SsrWideMerge.hpp kShadowTag (low 4 bits: mode)
TOLERANCE = 0.03                 # the other eye's surface: projected w within 3 % + 1 cm
STRENGTH = 4.0                   # all the way to the other eye's factor at a quarter more of the march lost
DEF = re.compile(r'(?m)^  (%\d+) = (.*)$')
HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')


def cbv(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    if len(found) != 1:
        raise ValueError('expected one handle for b%d, found %d' % (register, len(found)))
    return found[0]


def screen_tests(defs):
    """Branch conditions `uv inside [0, 1)^2`: and(and(u >= 0, u < 1), and(v >= 0, v < 1)) -> (u, v)."""
    def leaf(c, op, k):
        m = re.fullmatch(r'fcmp fast %s float (%%\d+), %s' % (op, k), defs.get(c, ''))
        return m.group(1) if m else None
    out = {}
    for k, v in defs.items():
        m = re.fullmatch(r'and i1 (%\d+), (%\d+)', v)
        if not m:
            continue
        axes = []
        for half in m.groups():
            mm = re.fullmatch(r'and i1 (%\d+), (%\d+)', defs.get(half, ''))
            if not mm:
                break
            a, b = leaf(mm.group(1), 'oge', r'0\.000000e\+00'), leaf(mm.group(2), 'olt', r'1\.000000e\+00')
            if not a or a != b:
                break
            axes.append(a)
        if len(axes) == 2:
            out[k] = tuple(axes)
    return out


PLANAR_MODE = 8                  # diagnostics: mode 8 = the engine's planar cascade choice (radial_cascades off)
BOTH_MODE = 10                   # diagnostics: mode 10 = planar choice with cascade_bands
OVERRIDE_RADIAL, OVERRIDE_PLANAR = 11, 12   # diagnostics: the sun factor replaced by the cascade zebra (radial / planar)
INSIDE = 0.98                    # a cascade takes the point if its light-space x, y are within this (PCF margin)


def radial_cascades(ir):
    """The sun cascade chosen by the distance from the eye instead of the depth along the camera's forward axis.

    The shader picks the cascade by dot(b1 row 41, P - b1 row 36) -- the eye's forward axis -- against the
    split distances in b13 rows 22-25 .x (then dithers into the next cascade over the last part of each).
    Every cascade boundary is then a plane square to where the head looks: turning the head sweeps it over the
    world, and the last one, where the cascades hand over to the distant shadows, shows as a line of shadow or
    light that follows the head (user 2026-10-16). By the distance |P - eye| the boundaries are spheres around
    the eye and stay put when the head turns.

    The cascades are fitted to planar slices of the frustum, so a point a sphere hands to a cascade is first
    checked against that cascade's light-space box (b13 rows 6 + 4i .. 9 + 4i, the same transform the shader
    samples through): outside it, the planar depth decides as before. Beyond the last split the distant shadows
    take over, as they do beyond the planar one."""
    b1, b13, b6 = cbv(ir, 1), cbv(ir, 13), cbv(ir, 6)
    defs = dict(DEF.findall(ir))
    rows = {}
    for v, d in defs.items():
        m = re.fullmatch(r'call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle (%\d+), i32 (\d+)\)\s*(;.*)?', d)
        if m:
            rows[v] = (m.group(1), int(m.group(2)))
    ext = {}
    for v, d in defs.items():
        m = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 (%\d+), (\d)', d)
        if m and m.group(1) in rows:
            ext[v] = rows[m.group(1)] + (int(m.group(2)),)
    found = []
    for v, d in defs.items():
        m = re.fullmatch(r'call float @dx\.op\.dot3\.f32\(i32 55, float (%\d+), float (%\d+), float (%\d+), '
                         r'float (%\d+), float (%\d+), float (%\d+)\)\s*(;.*)?', d)
        if not m or [ext.get(x) for x in m.groups()[:3]] != [(b1, 41, k) for k in range(3)]:
            continue
        subs = [re.fullmatch(r'fsub fast float (%\d+), (%\d+)', defs.get(x, '')) for x in m.groups()[3:6]]
        if not all(subs) or [ext.get(s.group(2)) for s in subs] != [(b1, 36, k) for k in range(3)]:
            continue
        users = [o for o, d_ in defs.items() for mm in [re.fullmatch(r'fcmp fast olt float %s, (%%\d+)' % re.escape(v), d_)] if mm
                 and ext.get(mm.group(1)) == (b13, 22, 0)]
        if users:
            found.append((v, list(m.groups()[3:6]), [s.group(1) for s in subs]))
    if len(found) != 1:
        raise ValueError('cascade depth: %d candidates' % len(found))
    depth, rel, world = found[0]
    q = '%asym.rc.'
    e = ssr.Emit(q)
    r = e.un('r', 24, e('r2', 'call float @dx.op.dot3.f32(i32 55, float %s, float %s, float %s, float %s, float %s, float %s)'
                        % tuple(rel + rel)))
    idx = '0'
    for k in range(4):
        s = e.ex('s%d' % k, e.cb('sr%d' % k, b13, 22 + k), 0)
        idx = e.i('i%d' % k, 'add', idx, e('z%d' % k, 'zext i1 %s to i32' % e('g%d' % k, 'fcmp fast oge float %s, %s' % (r, s))))
    count = e.ex('cnt', e.cb('cn', b13, 27, 'i32'), 0, '%dx.types.CBufRet.i32')
    beyond = e('by', 'icmp uge i32 %s, %s' % (idx, count))
    base = e.i('rb', 'shl', e.sel('ic', beyond, '0', idx, 'i32'), '2')
    col = [e('m%d' % k, 'call %%dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %%dx.types.Handle %s, i32 %s)'
             % (b13, e.i('mr%d' % k, 'add', base, str(6 + k)))) for k in range(4)]

    def coord(n, c):
        t = e.f('%sa' % n, 'fmul', e.ex('%s0' % n, col[0], c), world[0])
        t = e.mad('%sb' % n, e.ex('%s1' % n, col[1], c), world[1], t)
        t = e.mad('%sc' % n, e.ex('%s2' % n, col[2], c), world[2], t)
        return e.f(n, 'fadd', t, e.ex('%s3' % n, col[3], c))
    inx = e('inx', 'fcmp fast olt float %s, %s' % (e.un('ax', 6, coord('x', 0)), ssr.flt(INSIDE)))
    iny = e('iny', 'fcmp fast olt float %s, %s' % (e.un('ay', 6, coord('y', 1)), ssr.flt(INSIDE)))
    inside = e('in', 'or i1 %s, %s' % (e.both('inb', inx, iny), beyond))
    r3 = e.cb('t3', b6, 3, 'i32')
    tagw = e.ex('tw', r3, 3, '%dx.types.CBufRet.i32')
    tag = e('tg', 'icmp eq i32 %s, %d' % (e.i('th', 'and', tagw, str(ssr.i32(0xFFFFFFF0))), ssr.i32(SHADOW_TAG)))
    tmode = e.i('tm', 'and', tagw, '15')
    pl = e('p80', 'icmp eq i32 %s, %d' % (tmode, PLANAR_MODE))
    for k, md in enumerate((BOTH_MODE, OVERRIDE_PLANAR)):
        pl = e('p8%d' % (k + 1), 'or i1 %s, %s' % (pl, e('p9%d' % k, 'icmp eq i32 %s, %d' % (tmode, md))))
    planar = e.both('pm', tag, pl)
    use = e.both('use', inside, e('np', 'xor i1 %s, true' % planar))
    dsel = e.sel('d', use, r, depth)
    lines = ir.split('\n')
    at = next(i for i, l in enumerate(lines) if l.startswith('  %s = ' % depth))
    for i in range(at + 1, len(lines)):
        lines[i] = re.sub(re.escape(depth) + r'(?!\d)', dsel, lines[i])
    lines[at + 1:at + 1] = e.lines
    # the chosen cascade for cascade_bands: the slice index the cascade map is sampled with
    ir = '\n'.join(lines)
    t39 = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 0, i32 \d+, i32 39, i1 false\)', ir)
    if len(t39) != 1:
        raise ValueError('cascade map t39: %d' % len(t39))
    slices = set(re.findall(r'@dx\.op\.sampleCmpLevelZero\.f32\(i32 65, %dx\.types\.Handle ' + re.escape(t39[0]) +
                            r', %dx\.types\.Handle %\d+, float %\d+, float %\d+, float (%\d+),', ir))
    if len(slices) != 1:
        raise ValueError('cascade slice: %d' % len(slices))
    index = re.fullmatch(r'sitofp i32 (%\d+) to float', dict(DEF.findall(ir))[slices.pop()]).group(1)
    lines = ir.split('\n')
    at = next(i for i, l in enumerate(lines) if l.startswith('  %s = phi ' % index))
    while ' = phi ' in lines[at + 1]:
        at += 1
    lines[at + 1:at + 1] = ['  store i32 %s, i32* %%asym.rc.iv, align 4' % index]
    entry = next(i for i, l in enumerate(lines) if l.startswith('define void @ps_main_()'))
    lines[entry + 1:entry + 1] = ['  %asym.rc.iv = alloca i32, align 4', '  store i32 5, i32* %asym.rc.iv, align 4']
    return '\n'.join(lines), 1


BANDS_MODE = 9                   # diagnostics: mode 9 = the sun factor darkened in bands by the cascade (cascade_bands)


def cascade_bands(ir):
    """Diagnostics (mode 9 with the tag): the sun factor the mask stores darkened in bands by the cascade the pixel
    was shadowed from (0, 2 as they are, 1, 3 at 55 %, the distant shadows beyond the last cascade at 80 %), to see
    where the cascade boundaries run and whether they move with the head."""
    b6 = cbv(ir, 6)
    out = re.search(r'(?m)^  call void @dx\.op\.storeOutput\.f32\(i32 5, i32 0, i32 0, i8 0, float (%[\w.]+)\)', ir)
    if not out:
        raise ValueError('sun factor store')
    e = ssr.Emit('%asym.cb.')
    iv = e('iv', 'load i32, i32* %asym.rc.iv, align 4')
    r3 = e.cb('t3', b6, 3, 'i32')
    tagw = e.ex('tw', r3, 3, '%dx.types.CBufRet.i32')
    tag = e('tg', 'icmp eq i32 %s, %d' % (e.i('th', 'and', tagw, str(ssr.i32(0xFFFFFFF0))), ssr.i32(SHADOW_TAG)))
    tmode = e.i('tm', 'and', tagw, '15')
    on = e.both('on', tag, e('m9', 'or i1 %s, %s' % (e('m90', 'icmp eq i32 %s, %d' % (tmode, BANDS_MODE)),
                                                    e('m91', 'icmp eq i32 %s, %d' % (tmode, BOTH_MODE)))))
    odd = e('odd', 'icmp eq i32 %s, 1' % e.i('ob', 'and', iv, '1'))
    f = e.sel('f0', odd, ssr.flt(0.55), '1.000000e+00')
    f = e.sel('f1', e('far', 'icmp eq i32 %s, 4' % iv), ssr.flt(0.8), f)
    f = e.sel('f2', e('unk', 'icmp ugt i32 %s, 4' % iv), '1.000000e+00', f)
    v = e.sel('v', on, e.f('vm', 'fmul', out.group(1), f), out.group(1))
    # modes 11 / 12: the factor replaced -- odd cascades 0, even 1, the distant shadows 0.5 -- lit or not
    ov = e.both('ov', tag, e('ovm', 'or i1 %s, %s' % (e('ov1', 'icmp eq i32 %s, %d' % (tmode, OVERRIDE_RADIAL)),
                                                     e('ov2', 'icmp eq i32 %s, %d' % (tmode, OVERRIDE_PLANAR)))))
    z = e.sel('z0', odd, '0.000000e+00', '1.000000e+00')
    z = e.sel('z1', e('zf', 'icmp eq i32 %s, 4' % iv), ssr.flt(0.5), z)
    z = e.sel('z2', e('zu', 'icmp ugt i32 %s, 4' % iv), out.group(1), z)
    v = e.sel('v2', ov, z, v)
    lines = ir.split('\n')
    at = ir.count('\n', 0, out.start())
    lines[at] = lines[at].replace('float %s)' % out.group(1), 'float %s)' % v)
    lines[at:at] = e.lines
    return '\n'.join(lines)


def shadow_merge(ir):
    lines = ir.split('\n')
    defs = dict(DEF.findall(ir))
    b0, b1, b6, b12 = cbv(ir, 0), cbv(ir, 1), cbv(ir, 6), cbv(ir, 12)
    # the pixel: SV_Position x (half-width column) and y, the full-resolution column and the device depth
    px = next(k for k, v in defs.items() if re.fullmatch(r'fptoui float (%\d+) to i32', v) and
              re.fullmatch(r'call float @dx\.op\.loadInput\.f32\(i32 4, i32 0, i32 0, i8 0, i32 undef\).*', defs[re.fullmatch(r'fptoui float (%\d+) to i32', v).group(1)]))
    py = next(k for k, v in defs.items() if re.fullmatch(r'fptoui float (%\d+) to i32', v) and
              re.fullmatch(r'call float @dx\.op\.loadInput\.f32\(i32 4, i32 0, i32 0, i8 1, i32 undef\).*', defs[re.fullmatch(r'fptoui float (%\d+) to i32', v).group(1)]))
    load = re.search(r'(?m)^  (%\d+) = call %dx\.types\.ResRet\.f32 @dx\.op\.textureLoad\.f32\(i32 66, %dx\.types\.Handle %\d+, i32 0, i32 (%\d+), i32 '
                     + re.escape(py) + r', i32 undef', ir)
    if not load:
        raise ValueError('no depth load at the pixel')
    fx = load.group(2)
    depth = next(k for k, v in defs.items() if v == 'extractvalue %%dx.types.ResRet.f32 %s, 0' % load.group(1))
    if not re.fullmatch(r'or i32 %\d+, %\d+', defs.get(fx, '')):
        raise ValueError('the depth column %s is not the checkerboard 2 px | parity' % fx)
    # the sun contact march: the last loop sampling depth along uv0 + t d inside the picture
    tests = screen_tests(defs)
    if len(tests) != 2:
        raise ValueError('expected the local-light and sun marches, found %d screen tests' % len(tests))
    cond = max(tests, key=lambda c: int(c[1:]))
    u, v = tests[cond]
    mu = re.fullmatch(r'fadd fast float (%\d+), (%\d+)', defs[u]); mv = re.fullmatch(r'fadd fast float (%\d+), (%\d+)', defs[v])
    su = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', defs[mu.group(1)]); sv = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', defs[mv.group(1)])
    if not (mu and mv and su and sv) or su.group(1) != sv.group(1):
        raise ValueError('march sample is not uv0 + t d')
    tvar, du, dv, u0, v0 = su.group(1), su.group(2), sv.group(2), mu.group(2), mv.group(2)
    # t = exp(log(t_k)), t_k a phi starting at t0 in the loop header
    tk = re.fullmatch(r'call float @dx\.op\.unary\.f32\(i32 21, float (%\d+)\).*', defs[tvar])
    tk = tk and re.fullmatch(r'call float @dx\.op\.unary\.f32\(i32 23, float (%\d+)\).*', defs[tk.group(1)])
    phi = tk and re.fullmatch(r'phi float \[ (%\d+), (%\d+) \], \[ (%\d+), (%\d+) \]', defs[tk.group(1)])
    if not phi:
        raise ValueError('march parameter is not exp(log(phi))')
    at = lambda val: next(i for i, l in enumerate(lines) if l.startswith('  %s = ' % val))
    header = at(tk.group(1))
    while not lines[header].startswith('; <label>:'):
        header -= 1
    label = '%' + lines[header].split()[1].split(':')[1]
    preds = re.search(r'preds = (.*)$', lines[header]).group(1).split(', ')
    entry_pair = [(phi.group(1), phi.group(2)), (phi.group(3), phi.group(4))]
    t0, pre = next((val, blk) for val, blk in entry_pair if blk in preds and val != tk.group(1) and not defs.get(val, '').startswith('fadd fast float ' + tk.group(1)))
    pre_line = next(i for i, l in enumerate(lines) if l.startswith('; <label>:%s ' % pre[1:]))
    pre_term = pre_line + 1
    while not lines[pre_term].startswith('  br '):
        pre_term += 1
    if lines[pre_term] != '  br label %s' % label:
        raise ValueError('march preheader %s does not fall into %s' % (pre, label))
    for val in (du, dv, u0, v0, t0):
        if at(val) > pre_term:
            raise ValueError('%s is not defined before the march' % val)
    # the final block: the two output phis, the stores, ret
    out0 = re.search(r'(?m)^  call void @dx\.op\.storeOutput\.f32\(i32 5, i32 0, i32 0, i8 0, float (%\d+)\)', ir)
    out1 = re.search(r'(?m)^  call void @dx\.op\.storeOutput\.f32\(i32 5, i32 0, i32 0, i8 1, float (%\d+)\)', ir)
    if not out0 or not out1:
        raise ValueError('no target stores')
    sx = out0.group(1)
    fin = at(sx)
    head = fin
    while not lines[head].startswith('; <label>:'):
        head -= 1
    final = '%' + lines[head].split()[1].split(':')[1]
    body = head + 1
    while ' = phi ' in lines[body]:
        body += 1
    if re.search(r'\[ [^\]]+, %s \]' % re.escape(final), ir):
        raise ValueError('a phi takes a value from the final block')
    q, lab = '%asym.mg.', 'asym.mg.'
    _, ids = ssr.store_metadata(ir)
    entry = next(i for i, l in enumerate(lines) if l.startswith('define void @ps_main_()'))
    handles = ['  %sh2 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[0], ssr.OWN_SLOT),
               '  %sh3 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[1], ssr.OTHER_SLOT),
               '  %slostv = alloca float, align 4' % q,
               '  store float 0.000000e+00, float* %slostv, align 4' % q]
    h2, h3 = q + 'h2', q + 'h3'
    # the lost fraction, before the march: exit of uv0 + t d from [0, 1]^2, lost = sat(t0 + 1 - t_exit)
    p = ssr.Emit(q)
    exits = []
    for ax, (start, d) in enumerate(((u0, du), (v0, dv))):
        p('pos%d' % ax, 'fcmp fast ogt float %s, 0.000000e+00' % d)
        p.sel('far%d' % ax, q + 'pos%d' % ax, '1.000000e+00', '0.000000e+00')
        p.f('gap%d' % ax, 'fsub', q + 'far%d' % ax, start)
        p.un('pad%d' % ax, 6, d)
        p('tiny%d' % ax, 'fcmp fast olt float %spad%d, %s' % (q, ax, ssr.flt(1e-8)))
        p.sel('dd%d' % ax, q + 'tiny%d' % ax, '1.000000e+00', d)
        p.f('te%d' % ax, 'fdiv', q + 'gap%d' % ax, q + 'dd%d' % ax)
        exits.append(p.sel('tx%d' % ax, q + 'tiny%d' % ax, ssr.flt(1e9), q + 'te%d' % ax))
    p.bin('tex', 36, exits[0], exits[1])
    p.f('lpa', 'fadd', t0, '1.000000e+00'); p.f('lpb', 'fsub', q + 'lpa', q + 'tex'); lost = p.un('lost', 7, q + 'lpb')
    p.raw('  store float %s, float* %slostv, align 4' % (lost, q))
    # the merge, after the output phis
    e = ssr.Emit(q)
    r3 = e.cb('r3', b6, 3, 'i32')
    stamp = e.ex('stamp', r3, 2, '%dx.types.CBufRet.i32'); tagw = e.ex('tagw', r3, 3, '%dx.types.CBufRet.i32')
    e.i('tagh', 'and', tagw, str(ssr.i32(0xFFFFFFF0))); mode = e.i('mode', 'and', tagw, '15')
    on = e('on', 'icmp eq i32 %stagh, %d' % (q, ssr.i32(SHADOW_TAG)))
    r0s = e.cb('scr', b0, 3)
    wfull = e('wfu', 'fptoui float %s to i32' % e.ex('wf', r0s, 0)); width = e.i('w', 'lshr', wfull, '1')
    height = e('h', 'fptoui float %s to i32' % e.ex('hf', r0s, 1))
    vp = [e.cb('vp%d' % r, b1, r) for r in range(4)]
    P = [e.ex('P%d%d' % (r, k), vp[r], k) for r in range(4) for k in range(4)]
    # the pixel's world position, as the shader computes it: (x, y, depth, 1) through b12 rows 69-72
    m69 = [e.cb('m%d' % r, b12, r) for r in (69, 70, 71, 72)]
    xf = e('xf', 'uitofp i32 %s to float' % fx); yf = e('yf', 'uitofp i32 %s to float' % py)
    hw = []
    for k in range(4):
        t = e.f('wp%da' % k, 'fmul', e.ex('M0%d' % k, m69[0], k), xf)
        t = e.mad('wp%db' % k, e.ex('M1%d' % k, m69[1], k), yf, t)
        t = e.mad('wp%dc' % k, e.ex('M2%d' % k, m69[2], k), depth, t)
        hw.append(e.f('wp%d' % k, 'fadd', t, e.ex('M3%d' % k, m69[3], k)))
    world = [e.f('W%d' % k, 'fdiv', hw[k], hw[3]) for k in range(3)]
    t = e.f('cwa', 'fmul', P[12], world[0]); t = e.mad('cwb', P[13], world[1], t); t = e.mad('cwc', P[14], world[2], t)
    cw = e.f('cw', 'fadd', t, P[15])
    lostv = e('lv', 'load float, float* %slostv, align 4' % q)
    e.i('qx', 'and', px, '15'); e.i('qy', 'and', py, '31'); e.i('qxy', 'or', q + 'qx', q + 'qy')
    e('corner', 'icmp eq i32 %sqxy, 0' % q); e.both('hw', on, q + 'corner')
    e.raw('  br i1 %shw, label %%%shdr, label %%%sgo' % (q, lab, lab)); e.raw(''); e.raw('%shdr:' % lab)
    one = str(ssr.i32(struct.unpack('<I', struct.pack('<f', 1.0))[0]))
    d = [one, '0', '0', '0', one, '0', '0', '0', one, '0', '0', '0']
    d += [e.bits('hP%d' % k, P[k]) for k in range(16)]
    d += [stamp, width, height, str(ssr.i32(ssr.MERGE_MAGIC)), '0', '0', '0', '0']
    assert len(d) == 3 * ssr.SLOT
    e.i('hsl', 'and', stamp, str(ssr.RING - 1)); hb = e.i('hbs', 'mul', q + 'hsl', str(ssr.SLOT))
    for k in range(ssr.SLOT):
        e.store(h2, e.i('hi%d' % k, 'add', hb, str(k)), d[3 * k], d[3 * k + 1], d[3 * k + 2])
    e.store(h2, str(ssr.LATEST), stamp, str(ssr.i32(ssr.MERGE_MAGIC)), '0')
    e.raw('  br label %%%sgo' % lab); e.raw(''); e.raw('%sgo:' % lab)
    e.raw('  br i1 %s, label %%%sm1, label %%%sm9' % (on, lab, lab)); e.raw(''); e.raw('%sm1:' % lab)
    m = e
    # own entry: x = factor 16 bits | lost fraction 8 bits << 16, y = half projected w, z = stamp
    m('inx', 'icmp ult i32 %s, %s' % (px, width)); m('iny', 'icmp ult i32 %s, %s' % (py, height))
    m.both('inb', q + 'inx', q + 'iny')
    m.i('row', 'mul', py, width); m.i('pix', 'add', q + 'row', px); m.i('pix2', 'add', q + 'pix', str(ssr.HEADER))
    idx = m.sel('idx', q + 'inb', q + 'pix2', '-1', 'i32')
    m.un('ks', 7, sx); m.mad('ksf', q + 'ks', ssr.flt(65535.0), ssr.flt(0.5)); m('ksi', 'fptoui float %sksf to i32' % q)
    m.un('kl', 7, lostv); m.mad('klf', q + 'kl', ssr.flt(255.0), ssr.flt(0.5)); m('kli', 'fptoui float %sklf to i32' % q)
    m.i('kls', 'shl', q + 'kli', '16'); ex_ = m.i('kx', 'or', q + 'ksi', q + 'kls')
    m.bin('kz', 36, cw, ssr.flt(60000.0)); m('hz0', 'call i32 @dx.op.legacyF32ToF16(i32 130, float %skz)' % q)
    ey_ = m.i('hz', 'and', q + 'hz0', '65535')
    m.store(h2, idx, ex_, ey_, stamp)
    # only a pixel whose march lost samples can gain from the other eye: the lookup runs there (and
    # everywhere under a diagnostic mode)
    m('lpos', 'fcmp fast ogt float %s, 0.000000e+00' % lostv); m('mdon', 'icmp ne i32 %s, 0' % mode)
    m('look', 'or i1 %slpos, %smdon' % (q, q))
    m.raw('  br i1 %slook, label %%%slk, label %%%sm9' % (q, lab, lab)); m.raw(''); m.raw('%slk:' % lab)
    zero = '0.000000e+00'
    found, ox, oy, oz = ssr.emit_other(m, h3, (zero, zero, zero), world, clip_depth=True)
    m.i('os0', 'and', ox, '65535'); m('os1', 'uitofp i32 %sos0 to float' % q); osx = m.f('osx', 'fmul', q + 'os1', ssr.flt(1.0 / 65535.0))
    m.i('ol0', 'lshr', ox, '16'); m.i('ol1', 'and', q + 'ol0', '255'); m('ol2', 'uitofp i32 %sol1 to float' % q)
    olost = m.f('olost', 'fmul', q + 'ol2', ssr.flt(1.0 / 255.0))
    m.i('oz0', 'and', oy, '65535'); ozs = m('ozs', 'call float @dx.op.legacyF16ToF32(i32 131, i32 %soz0)' % q)
    m.f('dz0', 'fsub', ozs, oz); m.un('dz', 6, q + 'dz0')
    m.mad('lim', ssr.flt(TOLERANCE), oz, ssr.flt(0.01))
    m('dok', 'fcmp fast olt float %sdz, %slim' % (q, q))
    same = m.both('same', found, q + 'dok')
    m.f('gp', 'fsub', lostv, olost); m.f('gs', 'fmul', q + 'gp', ssr.flt(STRENGTH)); tt = m.un('t', 7, q + 'gs')
    m('tp', 'fcmp fast ogt float %s, 0.000000e+00' % tt)
    use = m.both('use', same, q + 'tp')
    m.f('sd', 'fsub', osx, sx); mixed = m.mad('mx', q + 'sd', tt, sx)
    # diagnostics: 1 = the pixels that moved towards the other eye fully shadowed, 2 = the other eye's factor
    # wherever it is the same surface (lit elsewhere), 3 = this eye's own
    m('md1', 'icmp eq i32 %s, 1' % mode); m('md2', 'icmp eq i32 %s, 2' % mode); m('md3', 'icmp eq i32 %s, 3' % mode)
    shown = m.sel('sh', q + 'md1', zero, mixed)
    res = m.sel('ra', use, shown, sx)
    m.sel('d2', same, osx, '1.000000e+00')
    res = m.sel('rb', q + 'md2', q + 'd2', res)
    res = m.sel('rc', q + 'md3', sx, res)
    # 4 = this eye's lost fraction (dark = more of the march left the picture), 5 = the other eye's (lit where
    # there is no entry for this surface)
    m('md4', 'icmp eq i32 %s, 4' % mode); m('md5', 'icmp eq i32 %s, 5' % mode)
    res = m.sel('rd', q + 'md4', m.f('il', 'fsub', '1.000000e+00', lostv), res)
    m.f('io', 'fsub', '1.000000e+00', olost); m.sel('io2', same, q + 'io', '1.000000e+00')
    res = m.sel('re', q + 'md5', q + 'io2', res)
    m.raw('  br label %%%sm9' % lab); m.raw(''); m.raw('%sm9:' % lab)
    merged = m('fx0', 'phi float [ %s, %%%sgo ], [ %s, %%%sm1 ], [ %s, %%%slk ]' % (sx, lab, sx, lab, res, lab))
    store_line = next(i for i, l in enumerate(lines) if l.startswith('  call void @dx.op.storeOutput.f32(i32 5, i32 0, i32 0, i8 0, float %s)' % sx))
    lines[store_line] = lines[store_line].replace('float %s)' % sx, 'float %s)' % merged)
    lines[body:body] = e.lines
    lines[pre_term:pre_term] = p.lines
    lines[entry + 1:entry + 1] = handles
    ir = '\n'.join(lines)
    ir, _ = ssr.store_metadata(ir)
    # b6 is uploaded as 64 bytes; the stamp and tag sit in its padding
    ir, n = re.subn(r'(?m)^(!\d+ = !\{i32 \d+, %PIX\* undef, !"", i32 0, i32 6, i32 1, i32 )56(, null\})$', r'\g<1>64\2', ir)
    if n != 1:
        raise ValueError('b6 metadata')
    return ssr.add_declarations(ir), 1


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, _ = radial_cascades(ham.disassemble(original, work))
        ir, merges = shadow_merge(ir)
        ir = cascade_bands(ir)
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, merges


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True)
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
            patched, merges = build(original)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d wide merge(s), %d -> %d bytes' % (key, name, merges, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
