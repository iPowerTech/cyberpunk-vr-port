"""Auto-exposure shaders (render-graph node 0x774CF8, HistogramUpdate) metered from the head's centre
(docs: memory cpvr-asym-projection).

The engine meters exposure from a small luminance image (df89...) that samples the scene through a radial
magnifier centred on the middle of the screen -- the centre importance of the environment's exposure
settings:

    p = 2 * uv - 1          (the output pixel, y from the top)
    s = (p + p * |p|^k) / (4 * sqrt(e^(k/2)))          k = b6 row 1.x
    sample at  0.5 + s

On a monitor the middle of the screen is where the player looks. Under the asymmetric projection it is not:
each eye's window reaches 14 degrees further outwards than inwards, so MAIN metered a region centred ~15
degrees to the right of the view and took in the right-hand side the left eye never sees -- and since the
second eye takes MAIN's exposure, a head turn to the right and one to the left moved both eyes' brightness
by different amounts. The recipe meters each eye as a piece of the one wide display the two eyes make: the
magnifier goes onto the middle of that display -- horizontally the head's view, x = c.x, the centre of the
mirrored windows' union -- and spans what it spans on that display, (1 + |c.x|) eye widths. Vertically the
two windows are the same, so the eye's middle already is the display's and stays:

    sample.x at  0.5 + c.x / 2 + (1 + |c.x|) * s.x      c.x = shift.x: the view's NDC
    sample.y at  0.5 + s.y

Measured 2026-10-15 (Quest 3, k = 2.25): the meter spans +-0.57 of the display, +-38 degrees around the head,
inside both windows (x -0.95..0.47 of MAIN's NDC), so both eyes meter the same region. Two earlier
variants moved more under a 17-degree turn than the engine's own meter (-17/+13 levels): the magnifier on
the view in both axes and shrunk to the nearer edge (-27/+21), and on the view in both axes at the eye's
extent (-23/+21) -- the vertical move put it on brighter sky than the display's middle.

The centre comes from the block itself: Stereo/Grading.cpp writes it into row 1.zw of the node's 24-byte
block (z = two signed 16-bit values, x low, y high, 1/16384 units; w = MAGIC), growing the upload to 32
bytes. Without the tag the twin samples exactly what the engine's shader does.

The adaptation (b059...) moves the adapted luminance and the compensated exposure towards their targets by
per-frame factors from its block (row 0.y/.z, measured 0.064 / 0.117 a frame: time constants of 0.15-0.25
s, near instant; row 3.x/.y the second stage); in a headset every glance changes what the meter sees, so the
twin scales them by --adapt (default 0.15: ~1.2 s to brighten, ~0.6 s to darken).

The environment meter (2026-10-16). A camera's auto-exposure meters the picture: a head turn is a new picture
and moves the exposure (measured: 0.55 EV for a 20-degree turn under an overpass, the engine's own meter
0.40), and in a headset that reads as the world's light changing -- "тень здания меняется при поворотах". An
eye adapts to the light around it, and someone turning their head where they stand does not see the world
get darker. So the twins keep the light of every direction the meter has seen, as cells over the sphere
around the player (12 yaw sectors x 5 pitch bands, world space, in u16), and expose for the place -- the
light of the whole sphere -- not for the picture:

    luminance image (df89)  one sample per 8x8 group (1024 a frame): its uv through MAIN's lens window and the
                  camera's world basis is a direction; the nearest cell gets ln(luminance) * ENV_SCALE and a
                  count added (atomics into entries ENV_ACC + cell). Grading.cpp grows its block to 96:
                  row 2 the window (l, r, d, u tangents), rows 3-5 the camera's right / up / forward,
                  row 3.w ENV_ACC_MAGIC (MAIN only).
    adaptation (b059)  each cell seen this frame moves its log luminance to the frame's mean at the rate
                  row 4.w (by how many samples it got) and gains confidence; every cell's confidence fades by
                  row 5.x (time and the player's movement: a new place is metered afresh), and faster while
                  the cells in view read a light more than ENV_SURPRISE off what they remembered (the light
                  changed with the player standing still: row 3.w = dt over that fade's time; their rate
                  rises the same way); the cells out of view move by the in-view cells' mean difference at
                  the same rate, so a light changing everywhere slowly (the hour) is followed by the whole
                  map. The sums are cleared for the next frame.
    target        E = sum(conf * cos(band) * (1 - h + h * max(0, cos(cell, view))) * cell) / sum(...): the
                  sphere's mean light, h (row 5.w bits 8-15, default 0) leaning it towards the view's
                  cosine-weighted hemisphere; plus the view's own difference (the meter's reading against
                  E) scaled by row 5.y and limited to +-row 5.z, so a glance at the sun still narrows the
                  pupil a little. E is put in the engine's units: the cells hold a plain mean of log
                  luminance, the engine meters the mean between its histogram's percentiles, centre-
                  weighted; their difference on the frame's own samples, smoothed at row 6.x, is added
                  (without it the place came out 10-30 % darker than the game's tuning).

A cell holds the light of its own direction whichever way the head looked when it was seen, so a turn only
re-weights what is remembered. Measured 2026-10-16 under the overpass: the first version wrote the meter's
one reading into every cell near the view, a cell between two views flipped between their readings and a
20-degree turn still moved the exposure half as much as the engine's meter; the hemisphere (h = 1) is the
light reaching the eye, and there it moves MORE than the meter (0.8 against 0.36 natural log for 20
degrees: bright street on one side, the dark underside on the other). E replaces the meter's log luminance (the histogram's mean between the
percentiles) before the clamps and both adaptation stages, which stay as they were. Grading.cpp grows the
adaptation's 56-byte block to 112: row 3.z ENV_MAGIC (MAIN only, 0 otherwise), row 4 the view's world forward
+ write rate, row 5 fade, gain, limit, mode (0 the environment, 1 the engine's reading -- the cells still
learn --, 2 without the view's own part; bits 8-15 the hemisphere lean), row 6.x the level offset's rate.

Output: <FNV-1a of the original>.dxil, a signed container with nothing appended, loaded from `asym_expo\\`
beside the plugin (Render/HiddenAreaShaders.cpp, CyberpunkVR_ExposureCentre).

  py -3.13 -B asym_expo_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR [--adapt 0.15]
"""
import argparse
import math
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import ham_shaders as ham          # disassemble / assemble / fnv1a
import dxil_container as dc
from asym_ssr_shaders import Emit, add_resources, add_types, metadata_lists, entries_of

LUMINANCE = 0xdf89e9d648ff7b36     # the centre-weighted luminance image (32x32 groups)
ADAPTATION = 0xb059d1d55826b33f    # the 1x1x1 adaptation
REGISTER = 6
CENTRE_ROW = 1              # .z the packed centre, .w MAGIC -- must match Stereo/Grading.cpp
MAGIC = 0x41734531
UNIT = 1.0 / 16384.0
ENV_MAGIC = 0x41734532      # row 3.z of the adaptation's block -- must match Stereo/Grading.cpp kExpoEnvTag
ENV_BYTES = 112             # the adaptation's block as grown (rows 4-6)
ENV_LUM_BYTES = 96          # the luminance image's block as grown (rows 2-5)
ENV_SLOT = 16               # u16, the cells: in the compute root signature's u0+19, which the node does not use
ENV_YAW = 12
ENV_PITCH = (-60.0, -30.0, 0.0, 30.0, 60.0)
ENV_SURPRISE = 0.5          # natural log (0.72 EV): a known direction reading further off than this is a new light
ENV_ACC = 60                # entries ENV_ACC + cell: this frame's sum of ln(luminance) * ENV_SCALE and count
ENV_SCALE = 256.0
ENV_FULL = 32.0             # samples for a cell to take the frame's mean at the full rate (a cell at the edge of the view, seen in part, follows slower)
ENV_ACC_MAGIC = 0x41734533  # row 3.w of the luminance image's block -- Stereo/Grading.cpp kExpoAccTag
ENV_ACC_TYPE = '%"class.RWStructuredBuffer<vector<unsigned int, 3> >"'
ENV_TYPE = '%"class.RWStructuredBuffer<vector<float, 3> >"'

HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')


def cbv_handle(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    if len(found) != 1:
        raise ValueError('expected one handle for b%d, found %d' % (register, len(found)))
    return found[0]


def float_literal(value):
    value, = struct.unpack('<f', struct.pack('<f', value))
    return '0x%016X' % struct.unpack('<Q', struct.pack('<d', value))[0]


def centre_magnifier(ir):
    """The `x = fadd s, 0.5` that turns the magnified offset into the sample u becomes
    x = (1 + |c.x|) * s + (0.5 + c.x / 2); the v one is left as it is. x comes first."""
    b6 = cbv_handle(ir, REGISTER)
    sites = [(out, s) for out, s in re.findall(r'(?m)^  (%\d+) = fadd fast float (%\d+), 5\.000000e-01$', ir)
             if re.search(r'@dx\.op\.unary\.f32\(i32 7, float ' + re.escape(out) + r'\)', ir)]
    if len(sites) != 2:
        raise ValueError('expected the x and y sample offsets, found %d' % len(sites))
    q = '%asym.ex.'
    magic = MAGIC if MAGIC < 2 ** 31 else MAGIC - 2 ** 32
    block = [
        f'  {q}l = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b6}, i32 {CENTRE_ROW})',
        f'  {q}zf = extractvalue %dx.types.CBufRet.f32 {q}l, 2',
        f'  {q}wf = extractvalue %dx.types.CBufRet.f32 {q}l, 3',
        f'  {q}z = bitcast float {q}zf to i32',
        f'  {q}w = bitcast float {q}wf to i32',
        f'  {q}ok = icmp eq i32 {q}w, {magic}',
        f'  {q}xs = shl i32 {q}z, 16',
        f'  {q}xi = ashr i32 {q}xs, 16',
        f'  {q}xf = sitofp i32 {q}xi to float',
        f'  {q}xc = fmul fast float {q}xf, {float_literal(UNIT)}',
        f'  {q}c0 = select i1 {q}ok, float {q}xc, float 0.000000e+00',
        f'  {q}a0 = call float @dx.op.unary.f32(i32 6, float {q}c0)',
        f'  {q}r0 = fadd fast float {q}a0, 1.000000e+00',
        f'  {q}h0 = fmul fast float {q}c0, 5.000000e-01',
        f'  {q}o0 = fadd fast float {q}h0, 5.000000e-01',
    ]
    out, offset = sites[0]
    first = re.search(r'(?m)^  ' + re.escape(out) + r' = ', ir).start()
    ir = ir[:first] + '\n'.join(block) + '\n' + ir[first:]
    for out, offset in sites[:1]:
        ir, n = re.subn(r'(?m)^  ' + re.escape(out) + r' = fadd fast float ' + re.escape(offset) + r', 5\.000000e-01$',
                        f'  {q}s0 = fmul fast float {offset}, {q}r0\n  {out} = fadd fast float {q}s0, {q}o0', ir)
        if n != 1:
            raise ValueError('offset %s not rewritten' % out)
    return ir, 1


def slow_adaptation(ir, adapt):
    """Every `select` between two of the block's per-frame adaptation factors is scaled by `adapt` where it is
    used (stage 1: row 0.y/.z, stage 2: row 3.x/.y)."""
    b6 = cbv_handle(ir, REGISTER)
    defs = dict(re.findall(r'(?m)^  (%\d+) = (.*)$', ir))
    rows = {v: int(r) for v, r in re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle '
                                            + re.escape(b6) + r', i32 (\d+)\)', ir)}
    def factor(v):
        m = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 (%\d+), (\d)', defs.get(v, ''))
        return m and (rows.get(m.group(1)), int(m.group(2))) in ((0, 1), (0, 2), (3, 0), (3, 1))
    sites = [out for out, a, b in re.findall(r'(?m)^  (%\d+) = select i1 %\d+, float (%\d+), float (%\d+)$', ir) if factor(a) and factor(b)]
    if len(sites) != 2:
        raise ValueError('expected the two adaptation factor selects, found %d' % len(sites))
    for k, out in enumerate(sites):
        name = '%%asym.ex.k%d' % k
        m = re.search(r'(?m)^  ' + re.escape(out) + r' = .*\n', ir)
        tail = re.sub(re.escape(out) + r'(?![\w.])', name, ir[m.end():])
        ir = ir[:m.end()] + f'  {name} = fmul fast float {out}, {float_literal(adapt)}\n' + tail
    return ir, len(sites)


def env_cells():
    """(x, y, z, band weight) per cell, world space (z up), yaw sectors inside pitch bands."""
    cells = []
    for el in ENV_PITCH:
        for i in range(ENV_YAW):
            az, e = math.radians((i + 0.5) * 360.0 / ENV_YAW), math.radians(el)
            cells.append((math.cos(e) * math.cos(az), math.cos(e) * math.sin(az), math.sin(e), math.cos(e)))
    return cells


def env_uav(ir, type_name, type_body, handle):
    """u16 as a 12-byte RWStructuredBuffer of `type_name`, its handle created after the shader's own."""
    top = max(int(x) for x in re.findall(r'(?m)^!(\d+) = ', ir))
    stride = '!%d' % (top + 1)
    ir = ir.rstrip('\n') + '\n%s = !{i32 1, i32 12}\n' % stride
    ir, ids = add_resources(ir, 1, ['!{{i32 {id}, ' + type_name + '* undef, !"", i32 0, i32 ' + str(ENV_SLOT) +
                                    ', i32 1, i32 12, i1 false, i1 false, i1 false, ' + stride + '}}'])
    ir = add_types(ir, [(type_name, type_body)])
    line = '  %s = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (handle, ids[0], ENV_SLOT)
    last = list(re.finditer(r'(?m)^  %\d+ = call %dx\.types\.Handle @dx\.op\.createHandle\(.*\n', ir))[-1]
    return ir[:last.end()] + line + '\n' + ir[last.end():]


def grow_cb(ir, size):
    """b6's metadata size to `size` bytes (the rows the twin reads past the engine's block)."""
    _, lists = metadata_lists(ir)
    grown = 0
    for x in entries_of(ir, lists[2]):
        mm = re.search(r'(?m)^(%s = !\{i32 \d+, [^,]+\* undef, !"", i32 0, i32 %d, i32 1, i32 )(\d+)(, null\})$' % (re.escape(x), REGISTER), ir)
        if mm:
            if int(mm.group(2)) > size:
                raise ValueError('b%d is %s bytes' % (REGISTER, mm.group(2)))
            ir = ir[:mm.start()] + mm.group(1) + str(size) + mm.group(3) + ir[mm.end():]
            grown += 1
    if grown != 1:
        raise ValueError('b%d metadata entries: %d' % (REGISTER, grown))
    return ir


def declare(ir, decls):
    for decl in decls:
        name = re.search(r'@[\w.]+', decl).group(0)
        if not re.search(r'(?m)^declare [^@]*' + re.escape(name) + r'\(', ir):
            i = ir.index('\nattributes #')
            ir = ir[:i] + '\n' + decl + '\n' + ir[i:]
    return ir


def direction_samples(ir):
    """The luminance image's samples into the environment cells (see the module notes): the first thread of
    every group, where row 3.w is ENV_ACC_MAGIC and its luminance is positive, adds ln(luminance) *
    ENV_SCALE and 1 to the nearest cell's sums -- a branch before the shader's return."""
    b6 = cbv_handle(ir, REGISTER)
    flat = re.findall(r'(?m)^  (%\d+) = call i32 @dx\.op\.flattenedThreadIdInGroup\.i32\(i32 96\)', ir)
    uv = re.findall(r'(?m)^  %\d+ = call %dx\.types\.ResRet\.f32 @dx\.op\.sampleLevel\.f32\(i32 62, %dx\.types\.Handle %\d+, '
                    r'%dx\.types\.Handle %\d+, float (%[\w.]+), float (%[\w.]+),', ir)
    value = re.findall(r'(?m)^  call void @dx\.op\.textureStore\.f32\(i32 67, %dx\.types\.Handle %\d+, i32 %\d+, i32 %\d+, i32 undef, '
                       r'float (%\d+), float \1, float \1, float \1, i8 15\)', ir)
    rets = re.findall(r'(?m)^  ret void$', ir)
    if len(flat) != 1 or not uv or len(set(uv)) != 1 or len(value) != 1 or len(rets) != 1:
        raise ValueError('luminance image: thread %d, uv %s, value %d, returns %d' % (len(flat), set(uv), len(value), len(rets)))
    (u, v), lum = uv[0], value[0]
    q = '%asym.ed.'
    h = q + 'h'
    ir = env_uav(ir, ENV_ACC_TYPE, '{ <3 x i32> }', h)
    ir = grow_cb(ir, ENV_LUM_BYTES)
    flt = float_literal
    e = Emit(q)
    r3 = e.cb('r3', b6, 3)
    tag = e.bits('tag', e.ex('tagf', r3, 3))
    on = e.both('on1', e('acc', 'icmp eq i32 %s, %d' % (tag, ENV_ACC_MAGIC)), e('first', 'icmp eq i32 %s, 0' % flat[0]))
    on = e.both('on', on, e('pos', 'fcmp fast ogt float %s, %s' % (lum, flt(1e-7))))
    e.raw('  br i1 %s, label %sbody, label %sdone' % (on, q, q))
    e.raw('')
    e.raw('asym.ed.body:')
    r2 = e.cb('r2', b6, 2); l, r, d_, t_ = (e.ex(n, r2, k) for k, n in enumerate(('wl', 'wr', 'wd', 'wu')))
    tx = e.mad('tx', u, e.f('ww', 'fsub', r, l), l)
    ty = e.f('ty', 'fsub', t_, e.f('tv', 'fmul', v, e.f('wh', 'fsub', t_, d_)))
    rows = [e.cb('b%d' % k, b6, k) for k in (3, 4, 5)]
    basis = [[e.ex('m%d%d' % (k, j), rows[k], j) for j in range(3)] for k in range(3)]
    ray = [e.mad('ray%d' % j, tx, basis[0][j], e.mad('rup%d' % j, ty, basis[1][j], basis[2][j])) for j in range(3)]
    best_d, best_i = None, None
    for c, (x, y, z, _) in enumerate(env_cells()):
        dc = e('o%d' % c, 'call float @dx.op.dot3.f32(i32 55, float %s, float %s, float %s, float %s, float %s, float %s)'
               % (ray[0], ray[1], ray[2], flt(x), flt(y), flt(z)))
        if best_d is None:
            best_d, best_i = dc, str(ENV_ACC)
            continue
        gt = e('g%d' % c, 'fcmp fast ogt float %s, %s' % (dc, best_d))
        best_d = e.sel('bd%d' % c, gt, dc, best_d)
        best_i = e.sel('bi%d' % c, gt, str(ENV_ACC + c), best_i, 'i32')
    qv = e('qi', 'fptosi float %s to i32' % e.f('qf', 'fmul', e.un('ln', 23, lum), flt(ENV_SCALE)))
    e('a0', 'call i32 @dx.op.atomicBinOp.i32(i32 78, %%dx.types.Handle %s, i32 0, i32 %s, i32 0, i32 undef, i32 %s)' % (h, best_i, qv))
    e('a1', 'call i32 @dx.op.atomicBinOp.i32(i32 78, %%dx.types.Handle %s, i32 0, i32 %s, i32 4, i32 undef, i32 1)' % (h, best_i))
    e.raw('  br label %sdone' % q)
    e.raw('')
    e.raw('asym.ed.done:')
    ir = re.sub(r'(?m)^  ret void$', lambda _m: '\n'.join(e.lines) + '\n  ret void', ir, count=1)
    writes = re.search(r'(?m)^declare void @dx\.op\.textureStore\.f32\(.*\) (#\d+)$', ir).group(1)
    readnone = re.search(r'(?m)^declare float @dx\.op\.unary\.f32\(.*\) (#\d+)$', ir).group(1)
    ir = declare(ir, ['declare i32 @dx.op.atomicBinOp.i32(i32, %%dx.types.Handle, i32, i32, i32, i32, i32) %s' % writes,
                      'declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) %s' % readnone,
                      'declare float @dx.op.tertiary.f32(i32, float, float, float) %s' % readnone])
    return ir, 1


def environment_meter(ir):
    """The meter's log luminance (`fadd mean, -8` before the Exp) becomes the environment's (see the module
    notes): a branch on row 3.z == ENV_MAGIC into a block that updates the cells in u16 and computes the
    target, joined by a phi that the Exp takes instead."""
    b6 = cbv_handle(ir, REGISTER)
    sites = re.findall(r'(?m)^  (%\d+) = fadd fast float %\d+, -8\.000000e\+00$', ir)
    if len(sites) != 1:
        raise ValueError('expected the log luminance, found %d' % len(sites))
    L = sites[0]
    uses = re.findall(re.escape(L) + r'(?![\w.])', ir)
    exp_use = re.findall(r'(?m)^  %\d+ = call float @dx\.op\.unary\.f32\(i32 21, float ' + re.escape(L) + r'\)', ir)
    if len(exp_use) != 1 or len(uses) != 2:
        raise ValueError('log luminance %s: %d uses, %d through Exp' % (L, len(uses) - 1, len(exp_use)))
    at = re.search(r'(?m)^  ' + re.escape(L) + r' = .*\n', ir)
    label = re.findall(r'(?m)^; <label>:(\d+)', ir[:at.start()])[-1]
    pred = '%' + label

    # u16: RWStructuredBuffer<float3>: cells (x log luminance, y confidence), then the frame's sums (bits)
    q = '%asym.en.'
    h = q + 'h'
    ir = env_uav(ir, ENV_TYPE, '{ <3 x float> }', h)
    ir = grow_cb(ir, ENV_BYTES)

    flt = float_literal
    head = Emit(q)
    r3 = head.cb('r3', b6, 3); head.ex('tf', r3, 2); head.bits('ti', q + 'tf')
    on = head('on', 'icmp eq i32 %sti, %d' % (q, ENV_MAGIC if ENV_MAGIC < 2 ** 31 else ENV_MAGIC - 2 ** 32))
    head.raw('  br i1 %s, label %sbody, label %sjoin' % (on, q, q))

    e = Emit(q)
    e.raw('')
    e.raw('asym.en.body:                                     ; preds = %s' % pred)
    r4 = e.cb('r4', b6, 4); g = [e.ex('g%d' % k, r4, k) for k in range(3)]; rate = e.ex('rate', r4, 3)
    r5 = e.cb('r5', b6, 5); fade, gain, lim, modef = (e.ex(n, r5, k) for k, n in enumerate(('fade', 'gain', 'lim', 'modef')))
    surprise_dt = e.ex('sdt', r3, 3)
    level_rate = e.ex('krate', e.cb('r6', b6, 6), 0)
    cells = env_cells()
    # pass 1: each cell, the light its samples read this frame, and how far that is from what it remembered
    loaded, sn, sd = [], '0.000000e+00', '0.000000e+00'
    vsum, vcnt = '0.000000e+00', '0.000000e+00'
    for c, (x, y, z, band) in enumerate(cells):
        ld = e('l%d' % c, 'call %%dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %%dx.types.Handle %s, i32 %d, i32 0)' % (h, c))
        p = e.ex('p%d' % c, ld, 0, '%dx.types.ResRet.f32'); c0 = e.ex('c%d' % c, ld, 1, '%dx.types.ResRet.f32')
        la = e('la%d' % c, 'call %%dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %%dx.types.Handle %s, i32 %d, i32 0)' % (h, ENV_ACC + c))
        si = e.bits('si%d' % c, e.ex('sf%d' % c, la, 0, '%dx.types.ResRet.f32'))
        ni = e.bits('ni%d' % c, e.ex('nf%d' % c, la, 1, '%dx.types.ResRet.f32'))
        n = e('n%df' % c, 'sitofp i32 %s to float' % ni)
        s = e('s%df' % c, 'sitofp i32 %s to float' % si)
        vsum = e.f('vs%d' % c, 'fadd', vsum, s)
        vcnt = e.f('vc%d' % c, 'fadd', vcnt, n)
        mean = e.f('mu%d' % c, 'fdiv', s, e.f('nd%d' % c, 'fmul', e.bin('nm%d' % c, 35, n, '1.000000e+00'), flt(ENV_SCALE)))
        cover = e.un('w%d' % c, 7, e.f('t%d' % c, 'fmul', n, flt(1.0 / ENV_FULL)))
        e_ = e.f('e%d' % c, 'fsub', mean, p)
        # z: how steadily the cell has been in view (its cover, smoothed at the write rate). A cell that just came
        # into view, often in part, reads off what it remembers for the head turning, not the light changing.
        cz = e.ex('cz%d' % c, ld, 2, '%dx.types.ResRet.f32')
        wc = e.f('s%d' % c, 'fmul', e.f('sc%d' % c, 'fmul', cover, c0), cz)
        sn = e.mad('sn%d' % c, wc, e_, sn)
        sd = e.f('sd%d' % c, 'fadd', sd, wc)
        loaded.append((p, c0, cover, cz, e_, x, y, z, band))
    # Where the meter sees directions it knows, steadily, and reads a different light, the world changed, not the
    # view: everything remembered fades, and the cells take the new light faster, by how far the surprise is past
    # ENV_SURPRISE (row 3.w: dt over the fade's time).
    sm = e.f('sm', 'fdiv', sn, e.bin('sdx', 35, sd, flt(1e-3)))
    sx = e.un('sa', 6, sm)
    sx = e.f('sx', 'fmul', e.bin('sp', 35, e.f('sq', 'fsub', sx, flt(ENV_SURPRISE)), '0.000000e+00'), flt(1.0 / ENV_SURPRISE))
    sx = e.sel('sk', e('sok', 'fcmp fast ogt float %s, %s' % (sd, flt(0.3))), sx, '0.000000e+00')
    sfade = e.un('sfd', 21, e.f('sg', 'fsub', '0.000000e+00', e.f('sh', 'fmul', sx, surprise_dt)))
    fade = e.f('fade2', 'fmul', fade, sfade)
    rate = e.f('rate2', 'fsub', '1.000000e+00', e.f('rr', 'fmul', e.f('r1m', 'fsub', '1.000000e+00', rate), sfade))
    # The same signed difference, where the view knows enough, moves the cells out of view too: a light that
    # changes everywhere (the hour, the weather) and slowly enough never to surprise is followed by the whole
    # map; in a still world the cells in view read what they remember and it is zero.
    shift = e.sel('sv', q + 'sok', sm, '0.000000e+00')
    # row 5.w: the mode in the low byte, the lean towards the view's hemisphere (0-255 -> 0-1) in the next
    mode = e.bits('mode', modef)
    lean = e.f('hl', 'fmul', e('hf', 'uitofp i32 %s to float' % e.i('hq', 'and', e.i('hs', 'lshr', mode, '8'), '255')), flt(1.0 / 255.0))
    level = e.f('h0', 'fsub', '1.000000e+00', lean)
    mode = e.i('mlo', 'and', mode, '255')
    num, den = '0.000000e+00', '0.000000e+00'
    for c, (p, c0, cover, cz, e_, x, y, z, band) in enumerate(loaded):
        c1 = e.f('d%d' % c, 'fmul', c0, fade)
        a = e.f('a%d' % c, 'fmul', rate, cover)
        drift = e.f('dr%d' % c, 'fmul', rate, e.f('nc%d' % c, 'fsub', '1.000000e+00', cover))
        p2 = e.mad('q%d' % c, a, e_, e.mad('ps%d' % c, drift, shift, p))
        c2 = e.mad('v%d' % c, a, e.f('u%d' % c, 'fsub', '1.000000e+00', c1), c1)
        z2 = e.mad('zz%d' % c, rate, e.f('zd%d' % c, 'fsub', cover, cz), cz)
        e.raw('  call void @dx.op.bufferStore.f32(i32 69, %%dx.types.Handle %s, i32 %d, i32 0, float %s, float %s, float %s, float undef, i8 7)'
              % (h, c, p2, c2, z2))
        e.raw('  call void @dx.op.bufferStore.f32(i32 69, %%dx.types.Handle %s, i32 %d, i32 0, float 0.000000e+00, float 0.000000e+00, float undef, float undef, i8 3)'
              % (h, ENV_ACC + c))
        d = e('o%d' % c, 'call float @dx.op.dot3.f32(i32 55, float %s, float %s, float %s, float %s, float %s, float %s)'
              % (g[0], g[1], g[2], flt(x), flt(y), flt(z)))
        k = e.f('x%d' % c, 'fmul', e.mad('kh%d' % c, lean, e.bin('k%d' % c, 35, d, '0.000000e+00'), level), flt(band))
        ck = e.f('y%d' % c, 'fmul', c2, k)
        num = e.mad('n%d' % c, ck, p2, num)
        den = e.f('z%d' % c, 'fadd', den, ck)
    # The cells keep a plain mean of log luminance; the engine meters the mean between the histogram's
    # percentiles, centre-weighted. Their difference on this frame's own samples -- the same pixels -- smoothed
    # at row 6.x (seconds), puts the environment in the engine's units (entry ENV_ACC .z keeps it).
    off0 = e.ex('of0', q + 'la0', 2, '%dx.types.ResRet.f32')
    view = e.f('vm', 'fdiv', vsum, e.f('vd', 'fmul', e.bin('vx', 35, vcnt, '1.000000e+00'), flt(ENV_SCALE)))
    off1 = e.mad('of1', level_rate, e.f('od', 'fsub', e.f('ol', 'fsub', L, view), off0), off0)
    off = e.sel('of', e('vok', 'fcmp fast ogt float %s, %s' % (vcnt, flt(16.0))), off1, off0)
    e.raw('  call void @dx.op.bufferStore.f32(i32 69, %%dx.types.Handle %s, i32 %d, i32 8, float %s, float undef, float undef, float undef, i8 1)'
          % (h, ENV_ACC, off))
    ok = e('ok', 'fcmp fast ogt float %s, %s' % (den, flt(1e-4)))
    env = e.f('env', 'fadd', e.sel('envr', ok, e.f('mean', 'fdiv', num, den), e.f('lo', 'fsub', L, off)), off)
    own = e.f('own', 'fmul', e.f('dv', 'fsub', L, env), gain)
    own = e.bin('own2', 36, e.bin('own1', 35, own, e.f('nlim', 'fsub', '0.000000e+00', lim)), lim)
    target = e.f('tgt', 'fadd', env, own)
    target = e.sel('tgt1', e('m1', 'icmp eq i32 %s, 1' % mode), L, target)
    target = e.sel('tgt2', e('m2', 'icmp eq i32 %s, 2' % mode), env, target)
    e.raw('  br label %sjoin' % q)
    e.raw('')
    e.raw('asym.en.join:                                     ; preds = %sbody, %s' % (q, pred))
    t = e('t', 'phi float [ %s, %s ], [ %s, %sbody ]' % (L, pred, target, q))

    at = re.search(r'(?m)^  ' + re.escape(L) + r' = .*\n', ir)
    tail = ir[at.end():]
    tail = re.sub(r'(?m)^(  %\d+ = call float @dx\.op\.unary\.f32\(i32 21, float )' + re.escape(L) + r'\)', lambda m: m.group(1) + t + ')', tail, count=1)
    # the blocks this one branched to now come from the join
    tail = re.sub(r'(?m)^(  %\d+ = phi .*)$', lambda m: re.sub(r'(, )' + re.escape(pred) + r'( \])', r'\1%sjoin\2' % q, m.group(1)), tail)
    ir = ir[:at.end()] + '\n'.join(head.lines + e.lines) + '\n' + tail
    ir = declare(ir, ['declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float) #0',
                      'declare float @dx.op.tertiary.f32(i32, float, float, float) #0'])
    return ir, len(env_cells())


def build(original, recipe):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, count = recipe(ham.disassemble(original, work))
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True, help='original shaders named <fnv1a>.dxil')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--adapt', type=float, default=0.15, help='scale of the per-frame adaptation factors')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    shaders = {LUMINANCE: ('HistogramUpdate luminance image, magnifier on the view centre + direction samples',
                           lambda ir: direction_samples(centre_magnifier(ir)[0])),
               ADAPTATION: ('HistogramUpdate adaptation, x%g per-frame factors + environment meter' % args.adapt,
                            lambda ir: environment_meter(slow_adaptation(ir, args.adapt)[0]))}
    failed = 0
    for key, (name, recipe) in shaders.items():
        source = args.shaders / ('%016x.dxil' % key)
        if not source.exists():
            print('MISSING %016x %s' % (key, name)); failed += 1; continue
        original = source.read_bytes()
        if ham.fnv1a(original) != key:
            raise RuntimeError('%s does not hash to its name' % source)
        try:
            patched, count = build(original, recipe)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d site(s), %d -> %d bytes' % (key, name, count, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
