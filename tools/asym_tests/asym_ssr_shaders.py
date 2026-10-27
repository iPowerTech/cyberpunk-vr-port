"""Screen-space reflection shaders patched for the asymmetric projection (docs: memory cpvr-asym-projection).

The engine's SSR (render-graph node 0x157B24) rebuilds every pixel's view-space position from its NDC and
depth with the symmetric tangents of its own constant block (b6 row 17.zw):

    p = ( tanX * (2u - 1) * z,  tanY * (2v - 1) * z,  z )        (v from the top, tanY < 0)

and then projects points along the reflected ray back to the screen with the full projection in b6 rows 0-3,
which carries the NDC shift Stereo/AsymProjection.cpp gives each eye. Under the shift the two disagree: the
reconstructed point projects ~shift * size / 2 pixels away from the pixel it came from (116 px per eye on a
Quest 3 at 960 px), so the march starts in the wrong place and runs in the wrong direction, mirrored in the
two eyes. ScreenSpacePlanarReflections, the probes and the lighting use full matrices and are fine
(ndc_tangent_scan.py).

Each recipe takes the projection's own centre off the NDC wherever it is scaled by a tangent, so the
reconstruction is the exact inverse of the shader's projection:

    x:  (2u - 1) - r0.z / r3.z            y:  (2v - 1) + r1.z / r3.z     (r = b6 rows 0, 1, 3)

With no shift the centre is the TAA jitter alone, a fraction of a pixel; the engine's own reconstruction
leaves even that out, so the twin is also the more exact inverse at rest.

Output: <FNV-1a of the original>.dxil, a signed container with nothing appended, loaded from `asym_ssr\\`
beside the plugin like the fog twins from `asym\\` (Render/HiddenAreaShaders.cpp, CyberpunkVR_AsymSsrPatch).

  py -3.13 -B asym_ssr_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham          # disassemble / assemble / fnv1a
import dxil_container as dc

# The indirect traces differ by SSR quality (UserSettings ScreenSpaceReflectionsQuality, applied at launch):
# Low 5663 + 8c54, Medium 5663 + 8e29, High 8e29 + cbf5, Ultra cbf5 + 0d0d, Insane 0d0d (ssr_census.py).
SHADERS = {
    0x78cc00b0064fdcc3: 'ScreenSpaceReflections trace',
    0x5663e7e429cb06f7: 'ScreenSpaceReflections trace, indirect (Low, Medium)',
    0x8e29e9279bc461ad: 'ScreenSpaceReflections trace, indirect (Medium, High)',
    0x8c54cd58991d44b2: 'ScreenSpaceReflections trace, indirect (Low)',
    0xcbf5910cf3345d82: 'ScreenSpaceReflections trace, indirect (High, Ultra)',
    0x0d0d144348ca7372: 'ScreenSpaceReflections trace, indirect (Ultra, Insane)',
    0x057c1b9099263e6d: 'ScreenSpacePlanarReflections trace (node 0x78A6B8, same block layout)',
    0xc36da84c6344378a: 'ScreenSpaceReflections confidence clear of the tiles without coarse hits (wide merge only)',
}

HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')
LOAD = r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle {h}, i32 {row}\)'
PRELUDE = (
    '  {q}l0 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {h}, i32 {row})\n'
    '  {q}c0 = extractvalue %dx.types.CBufRet.f32 {q}l0, 2\n'
    '  {q}l3 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {h}, i32 3)\n'
    '  {q}w = extractvalue %dx.types.CBufRet.f32 {q}l3, 2\n'
    '  {q}q = fdiv fast float {q}c0, {q}w\n'
    '  {q}ok = fcmp fast one float {q}w, 0.000000e+00\n'
    '  {q}c = select i1 {q}ok, float {q}q, float 0.000000e+00\n'
    '  {q}n = {op} fast float {ndc}, {q}c\n'
    '  {out} = fmul fast float {other}, {q}n')


def cbv_handle(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    if len(found) != 1:
        raise ValueError('expected one handle for b%d, found %d' % (register, len(found)))
    return found[0]


def shift_reconstruction(ir):
    b6 = cbv_handle(ir, 6)
    tangents = {}                     # value -> axis (0 = x from .z, 1 = y from .w)
    for load in re.findall(LOAD.format(h=re.escape(b6), row=17), ir):
        for comp, axis in ((2, 0), (3, 1)):
            for v in re.findall(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(load) + ', %d$' % comp, ir):
                tangents[v] = axis
    if not tangents:
        raise ValueError('no b6 row 17 tangent')
    # a value that IS a tangent, or a product with one (z * tangent)
    scaled = dict(tangents)
    for v, axis in tangents.items():
        for out in re.findall(r'(?m)^  (%\d+) = fmul fast float (?:%\d+, ' + re.escape(v) + '|' + re.escape(v) + r', %\d+)$', ir):
            scaled[out] = axis
    ndc = set(re.findall(r'(?m)^  (%\d+) = fadd fast float %\d+, -1\.000000e\+00$', ir))
    sites = []
    for m in re.finditer(r'(?m)^  (%\d+) = fmul fast float (%\d+), (%\d+)$', ir):
        out, a, b = m.groups()
        for n, other in ((a, b), (b, a)):
            if n in ndc and other in scaled:
                sites.append((out, n, other, scaled[other]))
    axes = sorted({s[3] for s in sites})
    if axes != [0, 1]:
        raise ValueError('reconstruction sites found for axes %s, expected both' % axes)
    for k, (out, n, other, axis) in enumerate(sites):
        prelude = PRELUDE.format(q='%%asym.ssr.%d.' % k, h=b6, row=axis, op='fsub' if axis == 0 else 'fadd',
                                 ndc=n, out=out, other=other)
        ir, count = re.subn(r'(?m)^  ' + re.escape(out) + r' = fmul fast float .*$', lambda _m: prelude, ir, count=1)
        if count != 1:
            raise ValueError('site %s not rewritten' % out)
    return ir, len(sites)


EDGE = re.compile(r'(?m)^  (%\d+) = call float @dx\.op\.unary\.f32\(i32 7, float (%\d+)\)')


def shared_view_fade(ir):
    """The screen-edge fade on the hit, sat(10 - 20 |u - 0.5|), also fades the hits the other eye cannot see.

    Each eye's own window reaches further outwards than the other's; a hit there exists in this eye's picture
    only, so this eye reflects it and the other falls back to the probe -- the same window reflects different
    things in the two eyes. With mirrored eyes the shared part of the two windows is, in this eye's UV,
    [max(c, 0), 1 + min(c, 0)], c the NDC x of this eye's projection centre (r0.z / r3.z, as above). The x fade
    becomes min(its own, the same 0.05 ramp at those bounds): without a shift the bounds are the screen's and
    the fade is the engine's. A shader without the fade (the tile classifier) is left as it is."""
    b6 = cbv_handle(ir, 6)
    defs = dict(re.findall(r'(?m)^  (%\d+) = (.*)$', ir))
    fades = []
    for out, arg in EDGE.findall(ir):
        m = re.fullmatch(r'fsub fast float 1\.000000e\+01, (%\d+)', defs.get(arg, ''))
        m = m and re.fullmatch(r'fmul fast float (%\d+), 2\.000000e\+01', defs.get(m.group(1), ''))
        m = m and re.fullmatch(r'call float @dx\.op\.unary\.f32\(i32 6, float (%\d+)\)\s*(;.*)?', defs.get(m.group(1), ''))
        m = m and re.fullmatch(r'fadd fast float (%\d+), -5\.000000e-01', defs.get(m.group(1), ''))
        if m:
            fades.append((out, m.group(1)))
    if not fades:
        return ir, 0
    if len(fades) % 2:
        raise ValueError('expected edge fades in x/y pairs, found %d' % len(fades))
    for k in range(0, len(fades), 2):
        out, u = fades[k]                       # x comes first of each pair
        q = '%%asym.ov.%d.' % k
        block = '\n'.join([
            f'  {q}l0 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b6}, i32 0)',
            f'  {q}c0 = extractvalue %dx.types.CBufRet.f32 {q}l0, 2',
            f'  {q}l3 = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b6}, i32 3)',
            f'  {q}w = extractvalue %dx.types.CBufRet.f32 {q}l3, 2',
            f'  {q}q = fdiv fast float {q}c0, {q}w',
            f'  {q}ok = fcmp fast one float {q}w, 0.000000e+00',
            f'  {q}c = select i1 {q}ok, float {q}q, float 0.000000e+00',
            f'  {q}lo = call float @dx.op.binary.f32(i32 35, float {q}c, float 0.000000e+00)',
            f'  {q}cn = call float @dx.op.binary.f32(i32 36, float {q}c, float 0.000000e+00)',
            f'  {q}hi = fadd fast float {q}cn, 1.000000e+00',
            f'  {q}da = fsub fast float {u}, {q}lo',
            f'  {q}fa = fmul fast float {q}da, 2.000000e+01',
            f'  {q}sa = call float @dx.op.unary.f32(i32 7, float {q}fa)',
            f'  {q}db = fsub fast float {q}hi, {u}',
            f'  {q}fb = fmul fast float {q}db, 2.000000e+01',
            f'  {q}sb = call float @dx.op.unary.f32(i32 7, float {q}fb)',
            f'  {q}s = call float @dx.op.binary.f32(i32 36, float {q}sa, float {q}sb)',
            f'  {q}r = call float @dx.op.binary.f32(i32 36, float {out}, float {q}s)']) + '\n'
        m = re.search(r'(?m)^  ' + re.escape(out) + r' = .*\n', ir)
        after = re.sub(re.escape(out) + r'(?![\w.])', q + 'r', ir[m.end():])
        ir = ir[:m.end()] + block + after
        ir = scale_hit_colour(ir, q)
    return ir, len(fades) // 2


def scale_hit_colour(ir, q):
    """The trace stores the hit's colour and its confidence (edge fade x distance x ...) in two targets; the
    colour carries no fade. Scaling only the confidence left a ghost of the one-eye hit, so the hit's colour
    takes the shared-view factor too: at 0 the ray leaves exactly what a miss leaves (colour 0, confidence 0).

    The confidence is followed from the x/y fade minimum through its fmul chain to the phi that merges it with
    the miss paths; the three phis beside it that take a value from the same block are the colour."""
    lines = ir.split('\n')
    uses = lambda v: [i for i, l in enumerate(lines)
                      if re.search(re.escape(v) + r'(?![\w.])', l) and not l.startswith('  %s =' % v)]
    v = re.search(r'(?m)^  (%\d+) = call float @dx\.op\.binary\.f32\(i32 36, float ' + re.escape(q + 'r') + r', float %\d+\)', ir)
    if not v:
        raise ValueError('no x/y fade minimum after %sr' % q)
    v = v.group(1)
    while True:
        users = uses(v)
        phis = [i for i in users if ' = phi float ' in lines[i]]
        if phis:
            break
        if len(users) != 1 or not re.match(r'  %\d+ = fmul fast float ', lines[users[0]]):
            raise ValueError('confidence chain from %s is not a single fmul' % v)
        v = re.match(r'  (%\d+) =', lines[users[0]]).group(1)
    if len(phis) != 1:
        raise ValueError('confidence %s merges in %d phis' % (v, len(phis)))
    src = re.search(r'\[ ' + re.escape(v) + r', (%\d+) \]', lines[phis[0]]).group(1)
    top = phis[0]
    while not lines[top - 1].startswith('; <label>'):
        top -= 1
    end = top
    while ' = phi ' in lines[end]:
        end += 1
    colour = []
    for i in range(top, end):
        hit = re.search(r'\[ (%\d+), ' + re.escape(src) + r' \]', lines[i])
        if i != phis[0] and hit:
            colour.append((i, hit.group(1)))
    if len(colour) != 3:
        raise ValueError('expected 3 colour phis beside %s, found %d' % (v, len(colour)))
    label = lines[top - 1].split()[1].split(':')[1]
    head = next(i for i, l in enumerate(lines) if re.match(r'; <label>:%s\s' % src[1:], l))
    br = next(i for i in range(head + 1, len(lines)) if lines[i].startswith('  br '))
    if lines[br] != '  br label %%%s' % label:
        raise ValueError('block %s does not fall through to %s: %s' % (src, label, lines[br]))
    scaled = []
    for k, (i, c) in enumerate(colour):
        name = '%scol%d' % (q, k)
        scaled.append('  %s = fmul fast float %s, %ss' % (name, c, q))
        lines[i] = lines[i].replace('[ %s, %s ]' % (c, src), '[ %s, %s ]' % (name, src))
    lines[br:br] = scaled
    return '\n'.join(lines)


# ---- the wide merge: the two eyes' traces as one wide display (Render/SsrWideMerge.cpp) ----------------
#
# Each eye's trace keeps its result in a store of its own (u16) and, where its own ray found less, takes the
# other eye's (u17) at the same surface point.
#
# Store: RWStructuredBuffer<uint3>, HEADER entries then one per pixel (index = HEADER + y * width + x):
#   x = R11G11B10 colour (the half-float bits cut to 11/11/10)
#   y = half view depth | confidence * 65535 << 16
#   z = the stamp of the pass that wrote it
# Header: RING slots of SLOT entries, slot stamp & 3 describing pass `stamp` (34 dwords: world->view rotation
# b6 rows 8-10 .xyz, camera position row 24 .xyz, projection rows 0-3, stamp, width, height, MERGE_MAGIC,
# pixel offset row 19 .zw), and entry LATEST = (newest stamp, MERGE_MAGIC, 0). Written by the first pixel of
# every 32x32 tile before the trace decides anything.
#
# The engine does not trace every tile every frame: its tile classifier (78cc...) sends a tile whose coarse,
# noisy rays all miss to a list that is only cleared (c36da...), and which tiles those are changes from frame
# to frame. So an entry is accepted up to MAX_AGE passes old, looked up through the camera of the pass that
# wrote it (the ring slot): the point is projected with the newest camera, and if the entry there is older,
# projected again with that entry's camera and accepted only if the entry there is from that same pass. The
# cleared tiles get the merge too (fill_merge): an eye's tile can have no hits of its own exactly where the
# other eye sees the reflection.
TRACES = {0x5663e7e429cb06f7, 0x8e29e9279bc461ad, 0x8c54cd58991d44b2, 0xcbf5910cf3345d82, 0x0d0d144348ca7372}
FILL = 0xc36da84c6344378a       # ScreenSpaceReflections: confidence 0 for the tiles without coarse hits
MERGE_MAGIC = 0x41734D31        # Render/SsrWideMerge.hpp kTag
CAMERA_ROW, STORE_ROW, MODE_ROW = 24, 25, 26
BLOCK_BYTES = 432               # CB_SSRRaymarch grown by the three rows
RING, SLOT = 4, 12
LATEST = RING * SLOT            # entry 48
HEADER = 52                     # entries before the pixels (Render/SsrWideMerge.hpp kHeaderEntries)
MAX_AGE = 3
MARGIN = 0.02                   # the other eye's confidence must exceed this eye's by this (ray noise)
OWN_SLOT, OTHER_SLOT = 16, 17   # u16 this eye's store, u17 the other's: in the root signature's u0+19, used by no
                                # SSR pass (the tile classifier binds its lists to u0-u3 AFTER the block upload)
STORE_TYPE = '%"class.RWStructuredBuffer<vector<unsigned int, 3> >"'
STRIDE = 12
STORE = re.compile(r'(?m)^  call void @dx\.op\.textureStore\.f32\(i32 67, %dx\.types\.Handle (%\d+), i32 (%\d+), i32 (%\d+), i32 undef, '
                   r'float (%\d+), float (%\d+), float (%\d+), float (%\d+), i8 15\)')


def i32(v):
    return v if v < 2 ** 31 else v - 2 ** 32


def flt(v):
    v, = struct.unpack('<f', struct.pack('<f', v))
    return '0x%016X' % struct.unpack('<Q', struct.pack('<d', v))[0]


def pixel_ndc(ir, pixel):
    """ndc = (float(pixel) + 0.5 - offset) * scale - 1, the trace's own pixel, as in its prologue."""
    defs = dict(re.findall(r'(?m)^  (%\d+) = (.*)$', ir))
    users = lambda v, pat: [o for o, d in defs.items() if re.fullmatch(pat % re.escape(v), d)]
    f = users(pixel, r'uitofp i32 %s to float')
    c = [x for v in f for x in users(v, r'fadd fast float %s, 5\.000000e-01')]
    b = [x for v in c for x in users(v, r'fsub fast float %s, %%\d+')]
    a = [x for v in b for x in users(v, r'fmul fast float %s, %%\d+')]
    n = [x for v in a for x in users(v, r'fadd fast float %s, -1\.000000e\+00')]
    if len(n) != 1:
        raise ValueError('pixel ndc of %s: %d candidates' % (pixel, len(n)))
    return n[0]


def view_position(ir, px, py):
    """This pixel's view-space position as the (shifted) reconstruction computes it: x and y are the
    reconstruction sites fed by the pixel's ndc, z the depth they scale the tangent by."""
    out = []
    for pixel in (px, py):
        ndc = pixel_ndc(ir, pixel)
        q = re.findall(r'(?m)^  (%asym\.ssr\.\d+\.)n = f(?:sub|add) fast float ' + re.escape(ndc) + r', %asym\.ssr\.\d+\.c$', ir)
        if len(q) != 1:
            raise ValueError('shifted ndc of %s: %d' % (ndc, len(q)))
        m = re.findall(r'(?m)^  (%\d+) = fmul fast float (%\d+), ' + re.escape(q[0] + 'n') + '$', ir)
        if len(m) != 1:
            raise ValueError('reconstruction from %sn: %d' % (q[0], len(m)))
        out.append(m[0])
    defs = dict(re.findall(r'(?m)^  (%\d+) = (.*)$', ir))
    depths = set()
    for _, scaled in out:
        m = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', defs[scaled])
        if not m:
            raise ValueError('tangent product %s' % scaled)
        a, b = m.groups()
        tangent = lambda v: re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 %\d+, [23]', defs.get(v, ''))
        depths.add(b if tangent(a) else a)
    if len(depths) != 1:
        raise ValueError('x and y scale different depths %s' % depths)
    return out[0][0], out[1][0], depths.pop()


class Emit:
    def __init__(self, q):
        self.q, self.lines = q, []

    def __call__(self, name, expr):
        self.lines.append('  %s%s = %s' % (self.q, name, expr))
        return self.q + name

    def raw(self, line):
        self.lines.append(line)

    def cb(self, name, handle, row, kind='f32'):
        return self(name, 'call %%dx.types.CBufRet.%s @dx.op.cbufferLoadLegacy.%s(i32 59, %%dx.types.Handle %s, i32 %d)' % (kind, kind, handle, row))

    def ex(self, name, agg, k, kind='%dx.types.CBufRet.f32'):
        return self(name, 'extractvalue %s %s, %d' % (kind, agg, k))

    def f(self, name, op, a, b):
        return self(name, '%s fast float %s, %s' % (op, a, b))

    def mad(self, name, a, b, c):
        return self(name, 'call float @dx.op.tertiary.f32(i32 46, float %s, float %s, float %s)' % (a, b, c))

    def un(self, name, op, a):
        return self(name, 'call float @dx.op.unary.f32(i32 %d, float %s)' % (op, a))

    def bin(self, name, op, a, b):
        return self(name, 'call float @dx.op.binary.f32(i32 %d, float %s, float %s)' % (op, a, b))

    def i(self, name, op, a, b):
        return self(name, '%s i32 %s, %s' % (op, a, b))

    def bits(self, name, a):
        return self(name, 'bitcast float %s to i32' % a)

    def fl(self, name, a):
        return self(name, 'bitcast i32 %s to float' % a)

    def sel(self, name, c, a, b, t='float'):
        return self(name, 'select i1 %s, %s %s, %s %s' % (c, t, a, t, b))

    def both(self, name, a, b):
        return self(name, 'and i1 %s, %s' % (a, b))

    def load(self, name, handle, index):
        l = self(name, 'call %%dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, %%dx.types.Handle %s, i32 %s, i32 0)' % (handle, index))
        return [self.ex('%s.%d' % (name, j), l, j, '%dx.types.ResRet.i32') for j in range(3)]

    def store(self, handle, index, a, b, c):
        self.raw('  call void @dx.op.bufferStore.i32(i32 69, %%dx.types.Handle %s, i32 %s, i32 0, i32 %s, i32 %s, i32 %s, i32 undef, i8 7)'
                 % (handle, index, a, b, c))


def emit_switch(e, b6):
    """Row 24 (camera, MAGIC) and row 25 (stamp, width, height): the merge is on for this view."""
    r24 = e.cb('r24', b6, CAMERA_ROW)
    cam = [e.ex('c%d' % k, r24, k) for k in range(3)]
    e.ex('tagf', r24, 3); e.bits('tag', e.q + 'tagf')
    on = e('on', 'icmp eq i32 %stag, %d' % (e.q, i32(MERGE_MAGIC)))
    r25 = e.cb('r25', b6, STORE_ROW, 'i32')
    stamp, width, height = (e.ex(n, r25, k, '%dx.types.CBufRet.i32') for k, n in enumerate(('stamp', 'w', 'h')))
    return on, cam, stamp, width, height


def emit_header(e, b6, h_own, cam, stamp, width, height):
    """This pass's ring slot and the LATEST entry (the caller branches here from each tile's first pixel)."""
    d = []
    for row in (8, 9, 10):
        l = e.cb('hr%d' % row, b6, row)
        d += [e.bits('hr%d.%d' % (row, k), e.ex('hr%d.x%d' % (row, k), l, k)) for k in range(3)]
    d += [e.bits('hc%d' % k, cam[k]) for k in range(3)]
    for row in range(4):
        l = e.cb('hp%d' % row, b6, row)
        d += [e.bits('hp%d.%d' % (row, k), e.ex('hp%d.x%d' % (row, k), l, k)) for k in range(4)]
    d += [stamp, width, height, str(i32(MERGE_MAGIC))]
    l = e.cb('h19', b6, 19)
    d += [e.bits('ho%d' % k, e.ex('ho%d.x' % k, l, k)) for k in (2, 3)]
    d += ['0', '0']
    assert len(d) == 3 * SLOT
    e.i('hsl', 'and', stamp, str(RING - 1)); base = e.i('hbs', 'mul', e.q + 'hsl', str(SLOT))
    for k in range(SLOT):
        idx = e.i('hi%d' % k, 'add', base, str(k))
        e.store(h_own, idx, d[3 * k], d[3 * k + 1], d[3 * k + 2])
    e.store(h_own, str(LATEST), stamp, str(i32(MERGE_MAGIC)), '0')




def emit_lookup(m, tag, h_other, base, wr, cam, rotation=False, clip_depth=False):
    """The other eye's entry for the point R_E^T v (wr) + c_E, through the camera in ring slot `base`.
    Returns (valid, entry x, y, z, view depth there, header stamp), and with `rotation` that camera's
    world->view rows (9 values, row-major) after them. With `clip_depth` the depth is the projected w instead
    of the view-space z (for a header whose rotation is the identity and projection the full world->clip)."""
    q = m.q
    od = []
    for k in range(SLOT):
        idx = m.i('%si%d' % (tag, k), 'add', base, str(k))
        od += m.load('%se%d' % (tag, k), h_other, idx)
    oR = [m.fl('%sR%d' % (tag, k), od[k]) for k in range(9)]
    oc = [m.fl('%sC%d' % (tag, k), od[9 + k]) for k in range(3)]
    oP = [m.fl('%sP%d' % (tag, k), od[12 + k]) for k in range(16)]
    ostamp, ow, oh = od[28], od[29], od[30]
    magic = m('%smg' % tag, 'icmp eq i32 %s, %d' % (od[31], i32(MERGE_MAGIC)))
    oo = [m.fl('%sO0' % tag, od[32]), m.fl('%sO1' % tag, od[33])]
    w = [m.f('%sw%d' % (tag, k), 'fadd', wr[k], m.f('%sd%d' % (tag, k), 'fsub', cam[k], oc[k])) for k in range(3)]
    o = []
    for k in range(3):
        t = m.f('%so%da' % (tag, k), 'fmul', oR[3 * k + 2], w[2])
        t = m.mad('%so%db' % (tag, k), oR[3 * k + 1], w[1], t)
        o.append(m.mad('%so%d' % (tag, k), oR[3 * k], w[0], t))
    clip = {}
    for row in (0, 1, 3):
        t = m.mad('%sp%da' % (tag, row), oP[4 * row + 2], o[2], oP[4 * row + 3])
        t = m.mad('%sp%db' % (tag, row), oP[4 * row + 1], o[1], t)
        clip[row] = m.mad('%sp%d' % (tag, row), oP[4 * row], o[0], t)
    okw = m('%sokw' % tag, 'fcmp fast ogt float %s, %s' % (clip[3], flt(1e-4)))
    cw = m.sel('%scw' % tag, okw, clip[3], '1.000000e+00')
    inv = m.f('%sinv' % tag, 'fdiv', '1.000000e+00', cw)
    nx = m.f('%snx' % tag, 'fmul', clip[0], inv); ny = m.f('%sny' % tag, 'fmul', clip[1], inv)
    u = m.mad('%su' % tag, nx, flt(0.5), flt(0.5)); v = m.mad('%sv' % tag, ny, flt(-0.5), flt(0.5))
    owf = m('%swf' % tag, 'uitofp i32 %s to float' % ow); ohf = m('%shf' % tag, 'uitofp i32 %s to float' % oh)
    fx = m.mad('%sfx' % tag, u, owf, oo[0]); fy = m.mad('%sfy' % tag, v, ohf, oo[1])
    g = [m('%sg0' % tag, 'fcmp fast oge float %s, 0.000000e+00' % fx), m('%sg1' % tag, 'fcmp fast olt float %s, %s' % (fx, owf)),
         m('%sg2' % tag, 'fcmp fast oge float %s, 0.000000e+00' % fy), m('%sg3' % tag, 'fcmp fast olt float %s, %s' % (fy, ohf))]
    valid = m.both('%sv0' % tag, g[0], g[1]); valid = m.both('%sv1' % tag, valid, g[2]); valid = m.both('%sv2' % tag, valid, g[3])
    valid = m.both('%sv3' % tag, valid, okw); valid = m.both('%sva' % tag, valid, magic)
    wm1 = m.f('%swm' % tag, 'fadd', owf, '-1.000000e+00'); hm1 = m.f('%shm' % tag, 'fadd', ohf, '-1.000000e+00')
    cx = m.bin('%scx' % tag, 36, m.bin('%scx0' % tag, 35, fx, '0.000000e+00'), wm1)
    cy = m.bin('%scy' % tag, 36, m.bin('%scy0' % tag, 35, fy, '0.000000e+00'), hm1)
    qx = m('%sqx' % tag, 'fptoui float %s to i32' % cx); qy = m('%sqy' % tag, 'fptoui float %s to i32' % cy)
    qi = m.i('%sqp' % tag, 'add', m.i('%sqr' % tag, 'mul', qy, ow), qx)
    qi = m.i('%sqh' % tag, 'add', qi, str(HEADER))
    qi = m.sel('%sqi' % tag, valid, qi, '0', 'i32')
    ex, ey, ez = m.load('%sx' % tag, h_other, qi)
    return (valid, ex, ey, ez, clip[3] if clip_depth else o[2], ostamp) + ((oR,) if rotation else ())


def emit_other(m, h_other, cam, wr, rotation=False, clip_depth=False):
    """The other eye's entry for the world point R^T v (wr) + cam: the newest pass's, else one up to MAX_AGE
    passes old looked up through its own pass's camera. Returns (found, entry x, entry y, view depth of the
    point in that camera) and with `rotation` that camera's world->view rows."""
    q = m.q
    latest = m.load('lt', h_other, str(LATEST))
    m('ltok', 'icmp eq i32 %s, %d' % (latest[1], i32(MERGE_MAGIC)))
    m.i('ls', 'and', latest[0], str(RING - 1)); base1 = m.i('lb', 'mul', q + 'ls', str(SLOT))
    va, ax, ay, az, oza, sa, *ra = emit_lookup(m, 'a', h_other, base1, wr, cam, rotation, clip_depth)
    m.both('a1', va, q + 'ltok')
    m('a2', 'icmp eq i32 %s, %s' % (az, latest[0]))           # newest pass
    age = m.i('ag', 'sub', latest[0], az)
    m('a3', 'icmp ule i32 %s, %d' % (age, MAX_AGE)); m('a4', 'icmp ne i32 %s, 0' % age)
    m.both('a5', q + 'a3', q + 'a4'); old = m.both('a6', q + 'a1', q + 'a5')
    # the older entry, through the camera of its own pass
    m.i('os', 'and', az, str(RING - 1)); base2 = m.i('ob', 'mul', q + 'os', str(SLOT))
    vb, bx, by, bz, ozb, sb, *rb = emit_lookup(m, 'b', h_other, base2, wr, cam, rotation, clip_depth)
    m('b1', 'icmp eq i32 %s, %s' % (sb, az)); m('b2', 'icmp eq i32 %s, %s' % (bz, az))
    m.both('b3', vb, q + 'b1'); m.both('b4', q + 'b3', q + 'b2'); okb = m.both('b5', q + 'b4', old)
    oka = m.both('a7', q + 'a1', q + 'a2')
    ox = m.sel('ex', oka, ax, bx, 'i32'); oy = m.sel('ey', oka, ay, by, 'i32'); oz = m.sel('ez', oka, oza, ozb)
    found = m('fd', 'or i1 %s, %s' % (oka, okb))
    if not rotation:
        return found, ox, oy, oz
    return found, ox, oy, oz, [m.sel('oq%d' % k, oka, ra[0][k], rb[0][k]) for k in range(9)]


def emit_merge(m, b6, h_other, cam, vx, vy, vz, r, g, b, conf):
    """The other eye's result for this pixel and the merged colour/confidence (own r, g, b, conf)."""
    q = m.q
    eR = []
    for row in (8, 9, 10):
        l = m.cb('er%d' % row, b6, row)
        eR += [m.ex('er%d.%d' % (row, k), l, k) for k in range(3)]
    wr = []
    for k in range(3):        # R^T v: column k of this eye's rotation
        t = m.f('wr%da' % k, 'fmul', eR[6 + k], vz)
        t = m.mad('wr%db' % k, eR[3 + k], vy, t)
        wr.append(m.mad('wr%d' % k, eR[k], vx, t))
    found, ox, oy, oz = emit_other(m, h_other, cam, wr)
    unhalf = lambda n, v: m(n, 'call float @dx.op.legacyF16ToF32(i32 131, i32 %s)' % v)
    m.i('ur0', 'and', ox, '2047'); m.i('ur1', 'shl', q + 'ur0', '4'); our = unhalf('our', q + 'ur1')
    m.i('ug0', 'lshr', ox, '11'); m.i('ug1', 'and', q + 'ug0', '2047'); m.i('ug2', 'shl', q + 'ug1', '4'); oug = unhalf('oug', q + 'ug2')
    m.i('ub0', 'lshr', ox, '22'); m.i('ub1', 'and', q + 'ub0', '1023'); m.i('ub2', 'shl', q + 'ub1', '5'); oub = unhalf('oub', q + 'ub2')
    m.i('uz0', 'and', oy, '65535'); ouz = unhalf('ouz', q + 'uz0')
    m.i('uc0', 'lshr', oy, '16'); m('uc2', 'uitofp i32 %suc0 to float' % q)
    ouc = m.f('ouc', 'fmul', q + 'uc2', flt(1.0 / 65535.0))
    r26 = m.cb('r26', b6, MODE_ROW)
    m.ex('modef', r26, 0); mode = m.bits('mode', q + 'modef'); tol = m.ex('tol', r26, 1)
    m.f('dz0', 'fsub', ouz, oz); m.un('dz', 6, q + 'dz0')
    m.mad('lim', tol, oz, flt(0.01))
    m('dok', 'fcmp fast olt float %sdz, %slim' % (q, q))
    same = m.both('same', found, q + 'dok')
    m.f('cm', 'fadd', conf, flt(MARGIN))
    m('bet', 'fcmp fast ogt float %s, %scm' % (ouc, q))
    use = m.both('use', same, q + 'bet')
    m.bin('oc1', 35, ouc, flt(1e-6)); m.f('t', 'fdiv', conf, q + 'oc1')
    mixed = []
    for n, own, other in (('r', r, our), ('g', g, oug), ('b', b, oub)):
        m.f('x' + n, 'fsub', own, other)
        mixed.append(m.mad('m' + n, q + 'x' + n, q + 't', other))
    # diagnostics: mode 1 = the pixels that took the other eye's result red; mode 2 = the other eye's result
    # wherever it is the same surface (black elsewhere); mode 3 = this eye's own result; 2 and 3 at confidence 1
    red = m('red', 'icmp eq i32 %s, 1' % mode)
    shown = [m.sel('s' + n, red, v, mv) for n, v, mv in
             (('r', flt(10.0), mixed[0]), ('g', '0.000000e+00', mixed[1]), ('b', '0.000000e+00', mixed[2]))]
    shownc = m.sel('sc', red, '1.000000e+00', ouc)
    res = [m.sel('n' + n, use, v, own) for n, v, own in (('r', shown[0], r), ('g', shown[1], g), ('b', shown[2], b))]
    resc = m.sel('nc', use, shownc, conf)
    m('m2', 'icmp eq i32 %s, 2' % mode); m('m3', 'icmp eq i32 %s, 3' % mode)
    dbg = m('dbg', 'or i1 %sm2, %sm3' % (q, q))
    dv = []
    for n, own, other in (('r', r, our), ('g', g, oug), ('b', b, oub)):
        o2 = m.sel('dq' + n, same, other, '0.000000e+00')
        dv.append(m.sel('dv' + n, q + 'm2', o2, own))
    res = [m.sel('z' + n, dbg, d_, v) for n, d_, v in zip('rgb', dv, res)]
    resc = m.sel('zc', dbg, '1.000000e+00', resc)
    return use, res, resc


def emit_store_entry(m, h_own, px, py, width, height, stamp, r, g, b, conf, vz):
    """One pixel's entry."""
    q = m.q
    big = flt(60000.0)
    m('inx', 'icmp ult i32 %s, %s' % (px, width)); m('iny', 'icmp ult i32 %s, %s' % (py, height))
    m.both('inb', q + 'inx', q + 'iny')
    m.i('row', 'mul', py, width); m.i('pix', 'add', q + 'row', px); m.i('pix2', 'add', q + 'pix', str(HEADER))
    idx = m.sel('idx', q + 'inb', q + 'pix2', '-1', 'i32')
    half = lambda n, v: m(n, 'call i32 @dx.op.legacyF32ToF16(i32 130, float %s)' % v)
    for n, v in (('r', r), ('g', g), ('b', b)):
        m.bin('k' + n, 36, v, big)
        half('h' + n, q + 'k' + n)
    m.i('pr0', 'lshr', q + 'hr', '4'); m.i('pr', 'and', q + 'pr0', '2047')
    m.i('pg0', 'lshr', q + 'hg', '4'); m.i('pg1', 'and', q + 'pg0', '2047'); m.i('pg', 'shl', q + 'pg1', '11')
    m.i('pb0', 'lshr', q + 'hb', '5'); m.i('pb1', 'and', q + 'pb0', '1023'); m.i('pb', 'shl', q + 'pb1', '22')
    m.i('px0', 'or', q + 'pr', q + 'pg'); sx = m.i('sx', 'or', q + 'px0', q + 'pb')
    m.bin('kz', 36, vz, big); half('hz0', q + 'kz'); m.i('hz', 'and', q + 'hz0', '65535')
    m.un('cs', 7, conf); m.mad('c16f', q + 'cs', flt(65535.0), flt(0.5))
    m('c16', 'fptoui float %sc16f to i32' % q); m.i('c16s', 'shl', q + 'c16', '16')
    sy = m.i('sy', 'or', q + 'hz', q + 'c16s')
    m.store(h_own, idx, sx, sy, stamp)


def add_declarations(ir):
    def attr(fn):
        a = re.search(r'(?m)^declare .*@dx\.op\.%s\.[\w.]*\(.*\) (#\d+)$' % fn, ir)
        return a.group(1) if a else None
    readnone = attr('unary') or attr('threadId') or attr('groupId')
    writes = attr('textureStore') or attr('storeOutput')
    if not readnone or not writes:
        raise ValueError('no attribute groups to reuse')
    decls = []
    if '@dx.op.bufferStore.i32(' not in ir.split('\ndefine ')[0] and not re.search(r'(?m)^declare void @dx\.op\.bufferStore\.i32', ir):
        decls.append('declare void @dx.op.bufferStore.i32(i32, %%dx.types.Handle, i32, i32, i32, i32, i32, i32, i8) %s' % writes)
    for line in ('declare i32 @dx.op.legacyF32ToF16(i32, float) %s' % readnone,
                 'declare float @dx.op.legacyF16ToF32(i32, i32) %s' % readnone,
                 'declare float @dx.op.binary.f32(i32, float, float) %s' % readnone,
                 'declare float @dx.op.tertiary.f32(i32, float, float, float) %s' % readnone,
                 'declare float @dx.op.unary.f32(i32, float) %s' % readnone):
        name = re.search(r'@[\w.]+', line).group(0)
        if not re.search(r'(?m)^declare [^@]*' + re.escape(name) + r'\(', ir):
            decls.append(line)
    readonly = attr('bufferLoad') or attr('cbufferLoadLegacy') or attr('createHandle')
    for line in ('declare %%dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32, %%dx.types.Handle, i32, i32) %s' % readonly,
                 'declare %%dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32, %%dx.types.Handle, i32) %s' % readonly,
                 'declare %%dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32, %%dx.types.Handle, i32) %s' % readonly,
                 'declare %%dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %%dx.types.Handle, i32, i32, i32, i32, i32, i32, i32) %s' % readonly):
        name = re.search(r'@[\w.]+', line).group(0)
        if not re.search(r'(?m)^declare [^@]*' + re.escape(name) + r'\(', ir):
            decls.append(line)
    # the validator refuses a declaration nothing calls
    decls = [d for d in decls if re.search(r'call [^@\n]*' + re.escape(re.search(r'@[\w.]+', d).group(0)) + r'\(', ir)]
    i = ir.index('\nattributes #')
    return ir[:i] + '\n' + '\n\n'.join(decls) + '\n' + ir[i:]


def add_types(ir, types):
    t = ir.index('\ndefine void @')
    new = ''.join('\n%s = type %s' % (n, d) for n, d in types if not re.search(r'(?m)^' + re.escape(n) + ' = type', ir))
    return ir[:t] + new + ir[t:]


def metadata_lists(ir):
    resources = re.search(r'(?m)^!dx\.resources = !\{(!\d+)\}$', ir).group(1)
    lists = re.search(r'(?m)^%s = !\{(.*)\}$' % re.escape(resources), ir).group(1).split(', ')
    if len(lists) != 4:
        raise ValueError('resource lists %s' % lists)
    return resources, lists


def entries_of(ir, node):
    return re.search(r'(?m)^%s = !\{(.*)\}$' % re.escape(node), ir).group(1).split(', ')


def add_resources(ir, cls, nodes):
    """Append metadata `nodes` (texts with {id}) to resource list `cls` (0 SRV, 1 UAV, 2 CB); returns ir and
    the ids given."""
    resources, lists = metadata_lists(ir)
    top = max(int(x) for x in re.findall(r'(?m)^!(\d+) = ', ir))
    names = ['!%d' % (top + 1 + k) for k in range(len(nodes) + 1)]
    if lists[cls] == 'null':
        existing, ids = [], []
        lists[cls] = names[-1]
        ir = re.sub(r'(?m)^%s = !\{.*\}$' % re.escape(resources), lambda _m: '%s = !{%s}' % (resources, ', '.join(lists)), ir, count=1)
        list_node, create = names[-1], True
    else:
        existing = entries_of(ir, lists[cls])
        ids = [int(re.search(r'(?m)^%s = !\{i32 (\d+),' % re.escape(x), ir).group(1)) for x in existing]
        list_node, create = lists[cls], False
    first = max(ids) + 1 if ids else 0
    given = list(range(first, first + len(nodes)))
    texts = ['%s = %s' % (names[k], n.format(id=given[k])) for k, n in enumerate(nodes)]
    all_entries = existing + names[:len(nodes)]
    if create:
        texts.append('%s = !{%s}' % (list_node, ', '.join(all_entries)))
    else:
        ir = re.sub(r'(?m)^%s = !\{.*\}$' % re.escape(list_node), lambda _m: '%s = !{%s}' % (list_node, ', '.join(all_entries)), ir, count=1)
    ir = ir.rstrip('\n') + '\n' + '\n'.join(texts) + '\n'
    return ir, given


def store_metadata(ir):
    """u16 / u17 as RWStructuredBuffer<uint3>."""
    top = max(int(x) for x in re.findall(r'(?m)^!(\d+) = ', ir))
    stride = '!%d' % (top + 1)
    ir = ir.rstrip('\n') + '\n%s = !{i32 1, i32 %d}\n' % (stride, STRIDE)
    node = lambda slot: ('!{{i32 {id}, ' + STORE_TYPE + '* undef, !"", i32 0, i32 ' + str(slot) +
                         ', i32 1, i32 12, i1 false, i1 false, i1 false, ' + stride + '}}')
    ir, ids = add_resources(ir, 1, [node(OWN_SLOT), node(OTHER_SLOT)])
    return add_types(ir, [(STORE_TYPE, '{ <3 x i32> }')]), ids


def grow_b6(ir):
    _, lists = metadata_lists(ir)
    grown = 0
    for x in entries_of(ir, lists[2]):
        mm = re.search(r'(?m)^(%s = !\{i32 \d+, [^,]+\* undef, !"", i32 0, i32 6, i32 1, i32 )(\d+)(, null\})$' % re.escape(x), ir)
        if mm:
            if int(mm.group(2)) > BLOCK_BYTES or int(mm.group(2)) < 368:
                raise ValueError('b6 is %s bytes' % mm.group(2))
            ir = ir[:mm.start()] + mm.group(1) + str(BLOCK_BYTES) + mm.group(3) + ir[mm.end():]
            grown += 1
    if grown != 1:
        raise ValueError('b6 metadata entries: %d' % grown)
    return ir


def wide_merge(ir):
    """The trace: header and own entry into u16, the merge from u17 before the colour/confidence stores."""
    stores = [m for m in STORE.finditer(ir) if '%' in m.group(4)]
    if len(stores) != 2:
        raise ValueError('expected the colour and confidence stores, found %d' % len(stores))
    colour, confidence = stores
    if not (colour.group(4) == colour.group(7) and len({colour.group(i) for i in (4, 5, 6)}) == 3 and
            len({confidence.group(i) for i in (4, 5, 6, 7)}) == 1 and colour.group(2, 3) == confidence.group(2, 3)):
        raise ValueError('stores are not colour (r, g, b, r) then confidence (c, c, c, c)')
    px, py = colour.group(2), colour.group(3)
    r, g, b, conf = colour.group(4), colour.group(5), colour.group(6), confidence.group(4)
    vx, vy, vz = view_position(ir, px, py)
    b6 = cbv_handle(ir, 6)
    lines = ir.split('\n')
    at = lambda m: ir.count('\n', 0, m.start())
    first = at(colour)
    if at(confidence) != first + 1:
        raise ValueError('colour and confidence stores are not adjacent')
    head = first
    while not lines[head].startswith('; <label>:'):
        head -= 1
    final = '%' + lines[head].split()[1].split(':')[1]
    if not lines[first + 2].startswith('  br label %'):
        raise ValueError('final block does not end after the stores: %s' % lines[first + 2])
    if re.search(r'\[ [^\]]+, %s \]' % re.escape(final), ir):
        raise ValueError('a phi takes a value from the final block %s' % final)
    entry = next(i for i, l in enumerate(lines) if l.startswith('define void @cs_main_()'))
    term = next(i for i in range(entry, len(lines)) if lines[i].startswith('  br '))
    if re.search(r'\[ [^\]]+, %0 \]', ir):
        raise ValueError('a phi takes a value from the entry block')
    if re.search(r'@dx\.op\.bufferStore|legacyF16ToF32|legacyF32ToF16', ir):
        raise ValueError('already uses the ops this adds')
    q, lab = '%asym.mg.', 'asym.mg.'
    ir_, ids = store_metadata(ir)
    handles = ['  %sh2 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[0], OWN_SLOT),
               '  %sh3 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[1], OTHER_SLOT)]
    h2, h3 = q + 'h2', q + 'h3'
    e = Emit(q)
    on, cam, stamp, width, height = emit_switch(e, b6)
    e.i('tx', 'and', px, '31'); e.i('ty', 'and', py, '31'); e.i('txy', 'or', q + 'tx', q + 'ty')
    e('corner', 'icmp eq i32 %stxy, 0' % q)
    e.both('hw', on, q + 'corner')
    e.raw('  br i1 %shw, label %%%shdr, label %%%sgo' % (q, lab, lab)); e.raw(''); e.raw('%shdr:' % lab)
    emit_header(e, b6, h2, cam, stamp, width, height)
    e.raw('  br label %%%sgo' % lab); e.raw(''); e.raw('%sgo:' % lab)
    m = Emit(q)
    m.raw('  br i1 %s, label %%%sm1, label %%%sm9' % (on, lab, lab)); m.raw(''); m.raw('%sm1:' % lab)
    emit_store_entry(m, h2, px, py, width, height, stamp, r, g, b, conf, vz)
    use, res, resc = emit_merge(m, b6, h3, cam, vx, vy, vz, r, g, b, conf)
    m.raw('  br label %%%sm9' % lab); m.raw(''); m.raw('%sm9:' % lab)
    fin = [m('f' + n, 'phi float [ %s, %s ], [ %s, %%%sm1 ]' % (own, final, new, lab)) for n, own, new in
           (('r', r, res[0]), ('g', g, res[1]), ('b', b, res[2]), ('c', conf, resc))]
    lines[first] = lines[first].replace('float %s, float %s, float %s, float %s, i8 15' % (r, g, b, r),
                                        'float %s, float %s, float %s, float %s, i8 15' % (fin[0], fin[1], fin[2], fin[0]))
    lines[first + 1] = lines[first + 1].replace('float %s, float %s, float %s, float %s, i8 15' % ((conf,) * 4),
                                                'float %s, float %s, float %s, float %s, i8 15' % ((fin[3],) * 4))
    lines[first:first] = m.lines
    lines[term:term] = e.lines
    lines[entry + 1:entry + 1] = handles
    ir = '\n'.join(lines)
    # metadata on the edited text: the same additions store_metadata made on the copy
    ir, _ = store_metadata(ir)
    return add_declarations(grow_b6(ir)), 1


PLANAR = 0x057c1b9099263e6d     # ScreenSpacePlanarReflections: one trace per pixel, its temporal blend inline


def planar_merge(ir):
    """The planar trace: the same merge, on the ray's own result before the shader's own temporal blend.

    The planar trace has no tile lists: every pixel with depth traces, and the block after the march takes
    the hit (colour and confidence, four phis: zero on every miss path) through a history blend into the two
    stores. The merge goes between the phis and that blend, so each eye's history accumulates the merged
    result and its store keeps the ray's own."""
    stores = [m for m in STORE.finditer(ir) if '%' in m.group(4)]
    if len(stores) != 2:
        raise ValueError('expected the colour and confidence stores, found %d' % len(stores))
    colour, confidence = stores
    if colour.group(2, 3) != confidence.group(2, 3):
        raise ValueError('the stores write different pixels')
    px, py = colour.group(2), colour.group(3)
    vx, vy, vz = view_position(ir, px, py)
    b6 = cbv_handle(ir, 6)
    lines = ir.split('\n')
    first = ir.count('\n', 0, colour.start())
    head = first
    while not lines[head].startswith('; <label>:'):
        head -= 1
    block = '%' + lines[head].split()[1].split(':')[1]
    phis, k = [], head + 1
    while ' = phi ' in lines[k]:
        phis.append(re.match(r'  (%\d+) = phi float ', lines[k]).group(1))
        k += 1
    body = k
    clamped = lambda v: re.search(r'@dx\.op\.binary\.f32\(i32 36, float %s, float 3\.200000e\+01\)' % re.escape(v), ir)
    colours = [v for v in phis if clamped(v)]
    if len(phis) != 4 or len(colours) != 3:
        raise ValueError('expected the hit colour (3) and confidence phis, found %s' % phis)
    r, g, b = colours
    conf = next(v for v in phis if v not in colours)
    if any(l.startswith(('  br ', '; <label>')) for l in lines[body:first]):
        raise ValueError('the hit block branches before the stores')
    if re.search(r'\[ [^\]]+, %s \]' % re.escape(block), ir):
        raise ValueError('a phi takes a value from the hit block %s' % block)
    entry = next(i for i, l in enumerate(lines) if l.startswith('define void @cs_main_()'))
    term = next(i for i in range(entry, len(lines)) if lines[i].startswith('  br '))
    if re.search(r'\[ [^\]]+, %0 \]', ir):
        raise ValueError('a phi takes a value from the entry block')
    if re.search(r'@dx\.op\.bufferStore|legacyF16ToF32|legacyF32ToF16', ir):
        raise ValueError('already uses the ops this adds')
    q, lab = '%asym.mg.', 'asym.mg.'
    _, ids = store_metadata(ir)
    handles = ['  %sh2 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[0], OWN_SLOT),
               '  %sh3 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[1], OTHER_SLOT)]
    h2, h3 = q + 'h2', q + 'h3'
    e = Emit(q)
    on, cam, stamp, width, height = emit_switch(e, b6)
    e.i('tx', 'and', px, '31'); e.i('ty', 'and', py, '31'); e.i('txy', 'or', q + 'tx', q + 'ty')
    e('corner', 'icmp eq i32 %stxy, 0' % q)
    e.both('hw', on, q + 'corner')
    e.raw('  br i1 %shw, label %%%shdr, label %%%sgo' % (q, lab, lab)); e.raw(''); e.raw('%shdr:' % lab)
    emit_header(e, b6, h2, cam, stamp, width, height)
    e.raw('  br label %%%sgo' % lab); e.raw(''); e.raw('%sgo:' % lab)
    m = Emit(q)
    m.raw('  br i1 %s, label %%%sm1, label %%%sm9' % (on, lab, lab)); m.raw(''); m.raw('%sm1:' % lab)
    emit_store_entry(m, h2, px, py, width, height, stamp, r, g, b, conf, vz)
    use, res, resc = emit_merge(m, b6, h3, cam, vx, vy, vz, r, g, b, conf)
    m.raw('  br label %%%sm9' % lab); m.raw(''); m.raw('%sm9:' % lab)
    fin = {}
    for n, own, new in (('r', r, res[0]), ('g', g, res[1]), ('b', b, res[2]), ('c', conf, resc)):
        fin[own] = m('f' + n, 'phi float [ %s, %s ], [ %s, %%%sm1 ]' % (own, block, new, lab))
    rename = re.compile('(?:' + '|'.join(re.escape(v) for v in fin) + r')(?![\w.])')
    tail = [rename.sub(lambda x: fin[x.group(0)], l) for l in lines[body:]]
    lines = lines[:body] + m.lines + tail
    lines[term:term] = e.lines
    lines[entry + 1:entry + 1] = handles
    ir = '\n'.join(lines)
    ir, _ = store_metadata(ir)
    ir = add_types(ir, [('%dx.types.ResRet.i32', '{ i32, i32, i32, i32, i32 }'), ('%dx.types.CBufRet.i32', '{ i32, i32, i32, i32 }')])
    return add_declarations(grow_b6(ir)), 1


def fill_merge(ir):
    """The tiles without coarse hits (the engine only zeroes their confidence): the other eye's result where
    it has one. The twin declares what the trace before it left bound -- depth t1, colour u0, b6, b12 -- and
    rebuilds the pixel's view position as the trace does (depth linearised by b12 rows 25-26, the shifted
    reconstruction of asym_ssr)."""
    m0 = re.search(r'(?m)^  call void @dx\.op\.textureStore\.f32\(i32 67, %dx\.types\.Handle (%\d+), i32 (%\d+), i32 (%\d+), i32 undef, '
                   r'float 0\.000000e\+00, float 0\.000000e\+00, float 0\.000000e\+00, float 0\.000000e\+00, i8 15\)', ir)
    if not m0 or len(re.findall(r'@dx\.op\.textureStore\.f32\(i32 67', ir)) != 1:
        raise ValueError('expected the one confidence clear')
    conf_handle, px, py = m0.groups()
    _, lists = metadata_lists(ir)
    if entries_of(ir, lists[1]) and len(entries_of(ir, lists[1])) != 1:
        raise ValueError('expected one UAV')
    q, lab = '%asym.mg.', 'asym.mg.'
    types = [('%hostlayout.SsrMergeBlock', '{ [27 x <4 x float>] }'), ('%hostlayout.SsrMergePixel', '{ [99 x <4 x float>] }'),
             ('%"class.Texture2D<float>"', '{ float, %"class.Texture2D<float>::mips_type" }'),
             ('%"class.Texture2D<float>::mips_type"', '{ i32 }'),
             ('%"class.RWTexture2D<vector<float, 3> >"', '{ <3 x float> }'),
             ('%dx.types.CBufRet.f32', '{ float, float, float, float }'), ('%dx.types.CBufRet.i32', '{ i32, i32, i32, i32 }'),
             ('%dx.types.ResRet.f32', '{ float, float, float, float, i32 }')]
    ir = add_types(ir, [types[3]] + [t for t in types if t is not types[3]])
    top = max(int(x) for x in re.findall(r'(?m)^!(\d+) = ', ir))
    f32 = '!%d' % (top + 1)
    ir = ir.rstrip('\n') + '\n%s = !{i32 0, i32 9}\n' % f32
    ir, (cb6, cb12) = add_resources(ir, 2, ['!{{i32 {id}, %hostlayout.SsrMergeBlock* undef, !"", i32 0, i32 6, i32 1, i32 432, null}}',
                                           '!{{i32 {id}, %hostlayout.SsrMergePixel* undef, !"", i32 0, i32 12, i32 1, i32 1584, null}}'])
    ir, (sdepth,) = add_resources(ir, 0, ['!{{i32 {id}, %"class.Texture2D<float>"* undef, !"", i32 0, i32 1, i32 1, i32 2, i32 0, ' + f32 + '}}'])
    ir, (ucol,) = add_resources(ir, 1, ['!{{i32 {id}, %"class.RWTexture2D<vector<float, 3> >"* undef, !"", i32 0, i32 0, i32 1, i32 2, i1 false, i1 false, i1 false, ' + f32 + '}}'])
    ir, ids = store_metadata(ir)
    handles = ['  %sb6 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 %d, i32 6, i1 false)' % (q, cb6),
               '  %sb12 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 %d, i32 12, i1 false)' % (q, cb12),
               '  %st1 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 %d, i32 1, i1 false)' % (q, sdepth),
               '  %su0 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 0, i1 false)' % (q, ucol),
               '  %sh2 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[0], OWN_SLOT),
               '  %sh3 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[1], OTHER_SLOT)]
    b6, b12, t1, u0, h3 = q + 'b6', q + 'b12', q + 't1', q + 'u0', q + 'h3'
    m = Emit(q)
    on, cam, stamp, width, height = emit_switch(m, b6)
    m.raw('  br i1 %s, label %%%sm1, label %%%sm9' % (on, lab, lab)); m.raw(''); m.raw('%sm1:' % lab)
    l = m('dl', 'call %%dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %%dx.types.Handle %s, i32 0, i32 %s, i32 %s, i32 undef, i32 undef, i32 undef, i32 undef)' % (t1, px, py))
    depth = m.ex('dd', l, 0, '%dx.types.ResRet.f32')
    r26 = m.cb('s26', b12, 26); r25 = m.cb('s25', b12, 25)
    a = m.mad('z0', m.ex('s26x', r26, 0), depth, m.ex('s26y', r26, 1))
    a = m.mad('z1', a, m.ex('s25x', r25, 0), m.ex('s25y', r25, 1))
    a = m.bin('z2', 35, a, flt(1e-7))
    vz = m.f('vz', 'fdiv', '1.000000e+00', a)
    r16, r17, r19 = m.cb('k16', b6, 16), m.cb('k17', b6, 17), m.cb('k19', b6, 19)
    r0, r1, r3 = m.cb('k0', b6, 0), m.cb('k1', b6, 1), m.cb('k3', b6, 3)
    w3 = m.ex('k3z', r3, 2)
    okw = m('k3ok', 'fcmp fast one float %s, 0.000000e+00' % w3)
    view = []
    for axis, pix in ((0, px), (1, py)):
        n = m('pf%d' % axis, 'uitofp i32 %s to float' % pix)
        n = m.f('ph%d' % axis, 'fadd', n, '5.000000e-01')
        n = m.f('pc%d' % axis, 'fsub', n, m.ex('off%d' % axis, r19, 2 + axis))
        n = m.mad('pn%d' % axis, n, m.ex('sc%d' % axis, r16, 2 + axis), '-1.000000e+00')
        c = m.f('sq%d' % axis, 'fdiv', m.ex('sh%d' % axis, r0 if axis == 0 else r1, 2), w3)
        c = m.sel('sk%d' % axis, okw, c, '0.000000e+00')
        n = m.f('sn%d' % axis, 'fsub' if axis == 0 else 'fadd', n, c)
        t = m.f('tz%d' % axis, 'fmul', vz, m.ex('tan%d' % axis, r17, 2 + axis))
        view.append(m.f('v%d' % axis, 'fmul', t, n))
    zero = '0.000000e+00'
    use, res, resc = emit_merge(m, b6, h3, cam, view[0], view[1], vz, zero, zero, zero, zero)
    # the tile writes that it found nothing, for the other eye's next pass: no stale entries
    emit_store_entry(m, q + 'h2', px, py, width, height, stamp, zero, zero, zero, zero, vz)
    wr = m('wrc', 'or i1 %s, %sdbg' % (use, q))
    m.raw('  br i1 %s, label %%%sw1, label %%%sm9' % (wr, lab, lab)); m.raw(''); m.raw('%sw1:' % lab)
    m.raw('  call void @dx.op.textureStore.f32(i32 67, %%dx.types.Handle %s, i32 %s, i32 %s, i32 undef, float %s, float %s, float %s, float %s, i8 15)'
          % (u0, px, py, res[0], res[1], res[2], res[0]))
    m.raw('  br label %%%sm9' % lab); m.raw(''); m.raw('%sm9:' % lab)
    fc = m('fc', 'phi float [ 0.000000e+00, %%0 ], [ 0.000000e+00, %%%sm1 ], [ %s, %%%sw1 ]' % (lab, resc, lab))
    lines = ir.split('\n')
    store = next(i for i, x in enumerate(lines) if x.startswith('  call void @dx.op.textureStore.f32(i32 67'))
    lines[store] = lines[store].replace('float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, i8 15',
                                        'float %s, float %s, float %s, float %s, i8 15' % ((fc,) * 4))
    entry = next(i for i, x in enumerate(lines) if x.startswith('define void @cs_main_()'))
    if any(x.startswith('; <label>:') for x in lines[entry:store]):
        raise ValueError('the clear is not in the entry block')
    lines[store:store] = m.lines
    lines[entry + 1:entry + 1] = handles
    ir = '\n'.join(lines)
    return add_declarations(ir), 1


def build(original, key=None):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir = ham.disassemble(original, work)
        count = fades = merges = 0
        if key == FILL:
            ir, merges = fill_merge(ir)
        else:
            ir, count = shift_reconstruction(ir)
            if key in TRACES:
                ir, merges = wide_merge(ir)
            elif key == PLANAR:
                ir, merges = planar_merge(ir)
            else:
                ir, fades = shared_view_fade(ir)
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, count, fades, merges


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
            patched, count, fades, merges = build(original, key)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d reconstruction site(s) shifted, %d shared-view fade(s), %d wide merge(s), %d -> %d bytes'
              % (key, name, count, fades, merges, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
