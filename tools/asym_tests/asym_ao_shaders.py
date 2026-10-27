"""Cone AO shaders (render-graph node 0x61EE78, DrawConeAO) patched for the asymmetric projection
(docs: memory cpvr-asym-projection).

The cone AO works in a view space of its own making. Every position it needs -- the pixel's, its neighbours'
for the normal, each cone sample's -- is rebuilt from NDC and linear depth with the symmetric tangents of
its block (b3 row 0.xy):

    p = ( tanX * z * ndc.x,  tanY * z * ndc.y,  z )          (ndc.y from the top)

and every sample is projected back with the same tangents (uv = 0.5 +- 0.5 * p.xy / (tan * z)). That is
self-consistent, so nothing moves on screen, but under the NDC shift Stereo/AsymProjection.cpp gives each
eye it is not the view space the pixels were rendered from: it is sheared by shift * tan * z (Quest 3:
~0.27 sideways, mirrored between the eyes, ~0.21 vertically). Corners between surfaces that face the
camera and surfaces that run along it open or close by up to ~15 degrees, so the occlusion in them comes
out different in the two eyes. The temporal pass (1902...) rebuilds the same positions for its history
test.

Each recipe takes the projection's own centre off the NDC wherever it is scaled into a position, and puts
it back wherever a position is divided back into NDC, so the AO runs in the true view space and still lands
on the same pixels:

    ndc' = ndc - c                  c = (shift.x, -shift.y): the pixel's NDC, x right, y down
    uv = 0.5 +- 0.5 * (p / (tan * z) +- c)   with the sign each projection's uv uses (y is flipped there)

The centre comes from the block itself. Its 208 bytes end in padding -- row 12.z holds whatever memory was
there (1e25 and the like) -- and Stereo/Grading.cpp writes this view's centre into it: 12.z = two signed
16-bit fixed-point values (x low, y high, 1/16384 units), 12.w = MAGIC. The twin uses the centre only when
12.w is MAGIC, so with the write off, missing or out of date it computes exactly what the engine's shader
does. --centre const:X,Y builds a test twin with a fixed centre instead (AsymProjection mode 2, where both
views get CyberpunkVR_AsymTest's shift).

The cone trace (34d5...) also has a hidden-area recipe (tools/hidden_area_tests/ham_shaders.py); --ham-out
writes that recipe applied on top of this one: the hidden_area\\ file to use beside the twin.

Output: <FNV-1a of the original>.dxil, a signed container with nothing appended, loaded from `asym_ao\\`
beside the plugin (Render/HiddenAreaShaders.cpp, CyberpunkVR_AsymAoPatch).

  py -3.13 -B asym_ao_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR [--ham-out DIR] [--centre const:X,Y]
"""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham          # disassemble / assemble / fnv1a / hidden-area recipes
import dxil_container as dc
import asym_ssr_shaders as ssr     # the wide merge's store, header ring and lookup

# The cone trace differs by AmbientOcclusion quality (UserSettings, applied at launch): Low 9009, Medium 34d5,
# High 1014; the temporal pass and the node's other three shaders are the same at every quality (all_census.py).
SHADERS = {
    0x34d5c22bdeded25c: 'DrawConeAO cone trace (Medium)',
    0x900998f12942de54: 'DrawConeAO cone trace (Low)',
    0x1014a66e76b130c3: 'DrawConeAO cone trace (High)',
    0x1902e65cecef16ef: 'DrawConeAO temporal (history test)',
}
REGISTER = 3
CENTRE_ROW = 12             # .z the packed centre, .w MAGIC -- must match Stereo/Grading.cpp
MAGIC = 0x41734331
UNIT = 1.0 / 16384.0
# 2.5 ENGINE STEREO (Stereo/EngineStereo.cpp, render feature 98). The engine runs the node over the side-by-side
# frame with its two-halves mode (row 14.y) OFF, so the trace treats both eyes as one picture with the centre's
# tangents (row 0); its own two-halves mode folds each half to an eye but builds positions with row 0's tangents
# and no window shift, and a cone step past an eye's inner edge lands in the other eye's half, whose depth it then
# rebuilds as if it lay at its own far edge -- the AO thinned out along both inner edges, different in each eye
# (2026-10-23: the brick wall lit differently in the two eyes, gone with AO off). Stereo/Grading.cpp turns the
# mode on and tags the block with STEREO_MAGIC in 12.w: 12.z the left eye's centre, 13.w the right eye's (packed
# like the mono centre), 14.w the eye separation (m), row 0.xy the eyes' tangents (rows 12/13, equal for mirrored
# windows). The twins then centre every position and projection on the pixel's own eye, and a cone step whose uv
# leaves its eye's window continues in the other eye: its ray, taken at the pixel's depth, is moved by the
# separation and projected into that eye, the depth gathered there and the point moved back. A step that leaves
# both windows is open, as at the screen's edges.
STEREO_MAGIC = 0x41735331
RIGHT_CENTRE = (13, 3)
SEPARATION = (14, 3)
HALVES = (14, 1)

HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')
DEF = re.compile(r'(?m)^  (%[\w.]+) = (.*)$')


def cbv_handle(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    if len(found) != 1:
        raise ValueError('expected one handle for b%d, found %d' % (register, len(found)))
    return found[0]


def float_literal(value):
    value, = struct.unpack('<f', struct.pack('<f', value))
    return '0x%016X' % struct.unpack('<Q', struct.pack('<d', value))[0]


def centre_prelude(b3):
    """The view's centre from b3 row 12 (packed .z, MAGIC in .w), 0 when the tag is missing."""
    q = '%asym.ao.'
    magic = MAGIC if MAGIC < 2 ** 31 else MAGIC - 2 ** 32
    lines = [
        f'  {q}l = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b3}, i32 {CENTRE_ROW})',
        f'  {q}zf = extractvalue %dx.types.CBufRet.f32 {q}l, 2',
        f'  {q}wf = extractvalue %dx.types.CBufRet.f32 {q}l, 3',
        f'  {q}z = bitcast float {q}zf to i32',
        f'  {q}w = bitcast float {q}wf to i32',
        f'  {q}ok = icmp eq i32 {q}w, {magic}',
        f'  {q}xs = shl i32 {q}z, 16',
        f'  {q}xi = ashr i32 {q}xs, 16',
        f'  {q}yi = ashr i32 {q}z, 16',
        f'  {q}xf = sitofp i32 {q}xi to float',
        f'  {q}yf = sitofp i32 {q}yi to float',
        f'  {q}xc = fmul fast float {q}xf, {float_literal(UNIT)}',
        f'  {q}yc = fmul fast float {q}yf, {float_literal(UNIT)}',
        f'  {q}cx = select i1 {q}ok, float {q}xc, float 0.000000e+00',
        f'  {q}cy = select i1 {q}ok, float {q}yc, float 0.000000e+00',
    ]
    return '\n'.join(lines) + '\n', (q + 'cx', q + 'cy')


def signed(value):
    return value if value < 2 ** 31 else value - 2 ** 32


def unpack(q, name, packed):
    """Two signed 16-bit fixed-point values (x low, y high, UNIT) -> (x, y) floats."""
    lines = [
        f'  {q}{name}xs = shl i32 {packed}, 16',
        f'  {q}{name}xi = ashr i32 {q}{name}xs, 16',
        f'  {q}{name}yi = ashr i32 {packed}, 16',
        f'  {q}{name}xf = sitofp i32 {q}{name}xi to float',
        f'  {q}{name}yf = sitofp i32 {q}{name}yi to float',
        f'  {q}{name}x = fmul fast float {q}{name}xf, {float_literal(UNIT)}',
        f'  {q}{name}y = fmul fast float {q}{name}yf, {float_literal(UNIT)}',
    ]
    return lines, (f'{q}{name}x', f'{q}{name}y')


def stereo_prelude(b3):
    """The centre of the pixel's own eye (engine stereo) or of the view (mono), and what a step that crosses to
    the other eye needs: whether the mode is on, the other eye's centre, the separation towards it, the half
    offsets. Without a tag both centres are 0 and crossing is off: the engine's shader."""
    q = '%asym.ao.'
    F = '%dx.types.CBufRet.f32'
    load = lambda name, row: f'  {q}{name} = call {F} @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b3}, i32 {row})'
    lines = [
        load('l', CENTRE_ROW),
        f'  {q}zf = extractvalue {F} {q}l, 2',
        f'  {q}wf = extractvalue {F} {q}l, 3',
        f'  {q}z = bitcast float {q}zf to i32',
        f'  {q}w = bitcast float {q}wf to i32',
        load('lr', RIGHT_CENTRE[0]),
        f'  {q}rf = extractvalue {F} {q}lr, {RIGHT_CENTRE[1]}',
        f'  {q}r = bitcast float {q}rf to i32',
        load('ls', SEPARATION[0]),
        f'  {q}sep0 = extractvalue {F} {q}ls, {SEPARATION[1]}',
        f'  {q}hf = extractvalue {F} {q}ls, {HALVES[1]}',
        f'  {q}h = bitcast float {q}hf to i32',
        load('l1', 1),
        f'  {q}wd = extractvalue {F} {q}l1, 2',
        f'  {q}hw = fmul fast float {q}wd, 5.000000e-01',
        load('l0', 0),
        f'  {q}tanx = extractvalue {F} {q}l0, 0',
        f'  {q}tid = call i32 @dx.op.threadId.i32(i32 93, i32 0)',
        f'  {q}tidf = uitofp i32 {q}tid to float',
        f'  {q}tidc = fadd fast float {q}tidf, 5.000000e-01',
        f'  {q}left = fcmp fast olt float {q}tidc, {q}hw',
        f'  {q}ok = icmp eq i32 {q}w, {signed(MAGIC)}',
        f'  {q}st = icmp eq i32 {q}w, {signed(STEREO_MAGIC)}',
        f'  {q}hon = icmp ne i32 {q}h, 0',
        f'  {q}on = and i1 {q}st, {q}hon',
    ]
    more, (lx, ly) = unpack(q, 'L', q + 'z')
    lines += more
    more, (rx, ry) = unpack(q, 'R', q + 'r')
    lines += more
    lines += [
        f'  {q}mx = select i1 {q}ok, float {lx}, float 0.000000e+00',
        f'  {q}my = select i1 {q}ok, float {ly}, float 0.000000e+00',
        f'  {q}ax = select i1 {q}left, float {lx}, float {rx}',
        f'  {q}ay = select i1 {q}left, float {ly}, float {ry}',
        f'  {q}cbx = select i1 {q}left, float {rx}, float {lx}',
        f'  {q}cx = select i1 {q}st, float {q}ax, float {q}mx',
        f'  {q}cy = select i1 {q}st, float {q}ay, float {q}my',
        f'  {q}nsep = fsub fast float -0.000000e+00, {q}sep0',
        f'  {q}sep = select i1 {q}left, float {q}sep0, float {q}nsep',
        f'  {q}offa2 = select i1 {q}left, float 0.000000e+00, float 1.000000e+00',
        f'  {q}offb = select i1 {q}left, float 5.000000e-01, float 0.000000e+00',
    ]
    values = dict(cx=q + 'cx', cy=q + 'cy', on=q + 'on', cbx=q + 'cbx', sep=q + 'sep', offa2=q + 'offa2',
                  offb=q + 'offb', tanx=q + 'tanx')
    return '\n'.join(lines) + '\n', values


def cross_steps(ir, defs, b3, s):
    """Each depth gather of a cone step (textureGather on t0), continued in the other eye when its uv leaves the
    pixel's eye (module comment at STEREO_MAGIC). Returns the ir, {frc phi of the gather: (cx, dx)} for the
    reconstruction of its x, and the number of steps."""
    t0 = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 0, i32 \d+, i32 0, i1 false\)', ir)
    if len(t0) != 1:
        raise ValueError('stereo cross: no single t0')
    t0 = t0[0]
    tid = re.findall(r'(?m)^  (%\d+) = call i32 @dx\.op\.threadId\.i32\(i32 93, i32 0\)', ir)
    pix = re.search(r'(?m)^  (%\d+) = call %dx\.types\.ResRet\.f32 @dx\.op\.textureLoad\.f32\(i32 66, %dx\.types\.Handle '
                    + re.escape(t0) + r', i32 0, i32 ' + re.escape(tid[0]) + r', ', ir) if len(tid) == 1 else None
    if not pix:
        raise ValueError('stereo cross: the pixel depth load not found')
    zp_line = re.search(r'(?m)^  (%\d+) = extractvalue %dx\.types\.ResRet\.f32 ' + re.escape(pix.group(1)) + r', 0\n', ir)
    zp = zp_line.group(1)
    q = '%asym.ao.'
    kab = (f'  {q}kd = fmul fast float {s["tanx"]}, {zp}\n'
           f'  {q}kab = fdiv fast float {s["sep"]}, {q}kd\n')
    ir = ir[:zp_line.end()] + kab + ir[zp_line.end():]
    gathers = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.ResRet\.f32 @dx\.op\.textureGather\.f32\(i32 73, %dx\.types\.Handle '
                         + re.escape(t0) + r', %dx\.types\.Handle %\d+, float (%\d+), float (%\d+), ', ir)
    if not gathers:
        raise ValueError('stereo cross: no depth gathers')
    tests = offscreen_tests(defs)
    x_sites = {}
    for k, (g, u, v) in enumerate(gathers):
        twice = re.findall(r'(?m)^  (%\d+) = fmul fast float ' + re.escape(u) + r', 2\.000000e\+00$', ir)
        frc = [t for t in twice if re.search(r'(?m)^  (%\d+) = call float @dx\.op\.unary\.f32\(i32 22, float ' + re.escape(t) + r'\)', ir)]
        if len(frc) != 1:
            raise ValueError('stereo cross: gather %s has no single frc(2u)' % g)
        fr = re.search(r'(?m)^  (%\d+) = call float @dx\.op\.unary\.f32\(i32 22, float ' + re.escape(frc[0]) + r'\)', ir).group(1)
        phi = re.findall(r'(?m)^  (%\d+) = phi float \[ ' + re.escape(fr) + r', %\d+ \], \[ ' + re.escape(u) + r', %\d+ \]$', ir)
        sel = [(name, cond) for name, cond in tests if reaches(defs, cond, u) and reaches(defs, cond, v)]
        if len(phi) != 1 or len(sel) != 1:
            raise ValueError('stereo cross: gather %s: %d phis, %d offscreen selects' % (g, len(phi), len(sel)))
        p = '%%asym.ao.s%d.' % k
        code = [
            f'  {p}ue = fmul fast float {u}, 2.000000e+00',
            f'  {p}ua = fsub fast float {p}ue, {s["offa2"]}',
            f'  {p}lo = fcmp fast olt float {p}ua, 0.000000e+00',
            f'  {p}hi = fcmp fast ogt float {p}ua, 1.000000e+00',
            f'  {p}ob = or i1 {p}lo, {p}hi',
            f'  {p}x = and i1 {p}ob, {s["on"]}',
            f'  {p}n2 = fmul fast float {p}ua, 2.000000e+00',
            f'  {p}na = fadd fast float {p}n2, -1.000000e+00',
            f'  {p}t1 = fsub fast float {p}na, {s["cx"]}',
            f'  {p}t2 = fsub fast float {p}t1, {q}kab',
            f'  {p}nb = fadd fast float {p}t2, {s["cbx"]}',
            f'  {p}h = fmul fast float {p}nb, 5.000000e-01',
            f'  {p}ub = fadd fast float {p}h, 5.000000e-01',
            f'  {p}uh = fmul fast float {p}ub, 5.000000e-01',
            f'  {p}uf = fadd fast float {p}uh, {s["offb"]}',
            f'  {p}u = select i1 {p}x, float {p}uf, float {u}',
            f'  {p}bl = fcmp fast olt float {p}ub, 0.000000e+00',
            f'  {p}bh = fcmp fast ogt float {p}ub, 1.000000e+00',
            f'  {p}vl = fcmp fast olt float {v}, 0.000000e+00',
            f'  {p}vh = fcmp fast ogt float {v}, 1.000000e+00',
            f'  {p}b1 = or i1 {p}bl, {p}bh',
            f'  {p}b2 = or i1 {p}vl, {p}vh',
            f'  {p}bo = or i1 {p}b1, {p}b2',
            f'  {p}cx = select i1 {p}x, float {s["cbx"]}, float {s["cx"]}',
            f'  {p}dx = select i1 {p}x, float {s["sep"]}, float 0.000000e+00',
        ]
        line = re.search(r'(?m)^  ' + re.escape(g) + r' = call .*\n', ir)
        gather = line.group(0).replace('float %s, float %s,' % (u, v), 'float %s, float %s,' % (p + 'u', v), 1)
        ir = ir[:line.start()] + '\n'.join(code) + '\n' + gather + ir[line.end():]
        ir, n = re.subn(r'(?m)^  ' + re.escape(frc[0]) + r' = fmul fast float ' + re.escape(u) + r', 2\.000000e\+00$',
                        '  %s = fmul fast float %su, 2.000000e+00' % (frc[0], p), ir, count=1)
        name, cond = sel[0]
        ir, m = re.subn(r'(?m)^  ' + re.escape(name) + r' = select i1 ' + re.escape(cond) + ',',
                        '  %soc = select i1 %sx, i1 %sbo, i1 %s\n  %s = select i1 %soc,' % (p, p, p, cond, name, p), ir, count=1)
        if n != 1 or m != 1:
            raise ValueError('stereo cross: gather %s not rewired' % g)
        x_sites[phi[0]] = (p + 'cx', p + 'dx')
    return ir, x_sites, len(gathers)


def patch(ir, centre):
    b3 = cbv_handle(ir, REGISTER)
    defs = dict(DEF.findall(ir))
    loads = {k for k, v in defs.items() if re.fullmatch(r'call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle '
                                                         + re.escape(b3) + r', i32 0\)\s*(;.*)?', v)}
    tangents = {}
    for k, v in defs.items():
        m = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 (%\d+), ([01])', v)
        if m and m.group(1) in loads:
            tangents[k] = int(m.group(2))
    if sorted(set(tangents.values())) != [0, 1]:
        raise ValueError('no b3 row 0 tangents')
    # 2.5 projects the samples with tangents picked per pixel: row 0 normally, rows 12 / 13 when row 14.y turns
    # on the engine's two-halves mode (each half its own tangents). The pick is a phi with row 0 on one edge:
    # its quotient is a projection all the same (the port never turns that mode on).
    for k, v in defs.items():
        m = re.fullmatch(r'phi float (.*)', v)
        if m:
            axes = {tangents[x] for x in re.findall(r'\[ (%\d+), %\d+ \]', m.group(1)) if x in tangents}
            if len(axes) == 1:
                tangents[k] = axes.pop()
    scaled = {}
    for k, v in defs.items():
        m = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', v)
        if m:
            for a in m.groups():
                if a in tangents:
                    scaled[k] = tangents[a]
    ndc = {k for k, v in defs.items() if re.fullmatch(r'fadd fast float %\d+, -1\.000000e\+00', v)}
    recon, proj = [], []
    for k, v in defs.items():
        m = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', v)
        if m:
            a, b = m.groups()
            for n, o in ((a, b), (b, a)):
                if n in ndc and o in scaled:
                    recon.append((k, n, o, scaled[o]))
                    break
        m = re.fullmatch(r'fdiv fast float (%\d+), (%\d+)', v)
        if m and m.group(2) in scaled:
            proj.append((k, m.group(1), m.group(2), scaled[m.group(2)]))
    if sorted({s[3] for s in recon}) != [0, 1]:
        raise ValueError('reconstruction sites for axes %s, expected both' % sorted({s[3] for s in recon}))

    # 2.5's two-halves mode (row 14.y read as an int): the engine-stereo recipe (module comment at STEREO_MAGIC)
    halves = re.search(r'(?m)^  %\d+ = call %dx\.types\.CBufRet\.i32 @dx\.op\.cbufferLoadLegacy\.i32\(i32 59, '
                       r'%dx\.types\.Handle ' + re.escape(b3) + r', i32 %d\)' % HALVES[0], ir)
    x_sites, steps = {}, 0
    if centre[0] == 'const':
        c = (float_literal(centre[1][0]), float_literal(centre[1][1]))
    else:
        # once, right after the b3 handle: it dominates every site, and stays in the entry block when a
        # hidden-area recipe splits that block later
        if halves:
            prelude, s = stereo_prelude(b3)
            c = (s['cx'], s['cy'])
        else:
            prelude, c = centre_prelude(b3)
        anchor = re.search(r'(?m)^  ' + re.escape(b3) + r' = call %dx\.types\.Handle @dx\.op\.createHandle\(.*\n', ir)
        ir = ir[:anchor.end()] + prelude + ir[anchor.end():]
        if halves and re.search(r'@dx\.op\.textureGather\.f32', ir):
            ir, x_sites, steps = cross_steps(ir, defs, b3, s)

    for i, (out, n, other, axis) in enumerate(recon):
        q = '%%asym.ao.r%d.' % i
        step = next((v for phi, v in x_sites.items() if axis == 0 and reaches(defs, n, phi)), None)
        if step:
            # a cone step's x: the centre of the eye it was gathered in, then back into the pixel's eye
            text = ('  {q}n = fsub fast float {n}, {c}\n  {q}m = fmul fast float {other}, {q}n\n'
                    '  {out} = fadd fast float {q}m, {d}').format(q=q, n=n, c=step[0], other=other, out=out, d=step[1])
        else:
            text = '  {q}n = fsub fast float {n}, {c}\n  {out} = fmul fast float {other}, {q}n'.format(q=q, n=n, c=c[axis], out=out, other=other)
        ir, count = re.subn(r'(?m)^  ' + re.escape(out) + r' = fmul fast float .*$', lambda _m: text, ir, count=1)
        if count != 1:
            raise ValueError('site %s not rewritten' % out)
    if x_sites and sum(1 for _, n, _, axis in recon if axis == 0 and any(reaches(defs, n, phi) for phi in x_sites)) != steps:
        raise ValueError('stereo cross: %d steps but not one x reconstruction each' % steps)
    for i, (out, num, den, axis) in enumerate(proj):
        q = '%%asym.ao.p%d.' % i
        # how this quotient reaches uv: 0.5 + 0.5*q keeps the pixel's NDC (add c), 0.5 - 0.5*q flips it (subtract)
        half = re.findall(r'(?m)^  (%\d+) = fmul fast float ' + re.escape(out) + r', 5\.000000e-01$', ir)
        if len(half) != 1:
            raise ValueError('projection %s: no single q * 0.5' % out)
        plus = re.search(r'(?m)^  %\d+ = fadd fast float ' + re.escape(half[0]) + r', 5\.000000e-01$', ir)
        minus = re.search(r'(?m)^  %\d+ = fsub fast float 5\.000000e-01, ' + re.escape(half[0]) + r'$', ir)
        if bool(plus) == bool(minus):
            raise ValueError('projection %s: uv sign not found' % out)
        # the quotient gets a fresh name; every use of the original name now reads quotient +- c
        text = '  {q}d = fdiv fast float {num}, {den}\n  {out} = {op} fast float {q}d, {c}'.format(
            q=q, num=num, den=den, out=out, c=c[axis], op='fadd' if plus else 'fsub')
        ir, count = re.subn(r'(?m)^  ' + re.escape(out) + r' = fdiv fast float .*$', lambda _m: text, ir, count=1)
        if count != 1:
            raise ValueError('projection %s not rewritten' % out)
    return ir, len(recon), len(proj)


# ---- the wide merge: the two eyes' cone AO as one wide display (Render/SsrWideMerge.cpp, channel kConeAo) --
#
# A cone sample whose uv leaves the picture counts as unoccluded (the trace selects its "open" value for it),
# so near an eye's inner edge, where the other eye still sees the occluders, each eye's AO comes out lighter
# than the other's at the same surface. The trace twin counts those samples; each eye keeps its result --
# bent normal, AO, view depth, the fraction of samples that left its picture -- in a store of its own (u16)
# and, where the other eye's store (u17) holds the same surface with fewer samples lost, moves towards the
# other eye's result by STRENGTH x the difference of the lost fractions. Inside both windows nothing
# changes; at an inner edge the eye that saw the neighbourhood supplies it for both.
#
# The store, header ring and lookup are the SSR merge's (asym_ssr_shaders.py). The header comes from the
# block itself: rows 4-7 are the view->world matrix (view x right, y up, z forward; row 7 the eye's
# position), the projection is the block's tangents (row 0) with the plugin-written centre (row 12.z). Row 11
# (the 4th row of the history matrix in rows 8-11, which no pass of the node reads) carries what the plugin
# adds: x the pass stamp, y AO_TAG, z the depth tolerance, w STRENGTH; 12.y the diagnostic mode. Entry:
#   x = bent normal as stored (0.5 + 0.5 n, 8 bits each) | lost fraction * 255 << 24
#   y = half view depth | AO * 65535 << 16,   z = stamp
AO_TRACES = {0x34d5c22bdeded25c, 0x900998f12942de54, 0x1014a66e76b130c3}
MERGE_ROW = 11
# The diagnostic mode's slot: 12.y in 2.31's 208-byte block. Game 2.5's block is 240 bytes and its shaders read
# 12.xy and 13.xy (a second pair of tangents) and 14.xy, so there it is 13.z (tools/patchday/regen_twins.py
# LAYOUT; Render/SsrWideMerge.hpp kAoModeRow).
MODE_ROW, MODE_COMP = CENTRE_ROW, 1
AO_TAG = 0x41734D41               # Render/SsrWideMerge.hpp kAoTag
VIEW_ROWS = (4, 5, 6, 7)


def offscreen_tests(defs):
    """Selects that give a cone sample its open value when its uv is outside [0, 1]: (select, condition)."""
    def tests(c, depth=0):
        d = defs.get(c, '')
        if re.fullmatch(r'fcmp fast olt float %\d+, 0\.000000e\+00', d) or re.fullmatch(r'fcmp fast ogt float %\d+, 1\.000000e\+00', d):
            return 1
        # cross_steps' wrapper: the engine's test, or the other eye's for a step that crossed
        m = re.fullmatch(r'select i1 %asym\.ao\.s\d+\.x, i1 %asym\.ao\.s\d+\.bo, i1 (%\d+)', d)
        if m:
            return tests(m.group(1), depth)
        m = re.fullmatch(r'or i1 (%\d+), (%\d+)', d)
        return tests(m.group(1), depth + 1) + tests(m.group(2), depth + 1) if m and depth < 6 else 0
    out = []
    for k, v in defs.items():
        m = re.fullmatch(r'select i1 (%[\w.]+), float %\d+, float %\d+', v)
        if m and tests(m.group(1)) == 4:
            out.append((k, m.group(1)))
    return out


def reaches(defs, value, target, depth=0):
    """Whether `value` is computed from `target` within a few instructions."""
    if value == target:
        return True
    if depth > 6:
        return False
    return any(reaches(defs, a, target, depth + 1) for a in re.findall(r'%[\w.]+', defs.get(value, '')))


def emit_ao_merge(m, h_other, cam, R, v, e, ao, lost, tol, strength, mode):
    """The other eye's result at this pixel's surface point and the merged (bent normal, AO)."""
    q = m.q
    wr = []
    for k in range(3):        # R^T v: column k of this eye's rotation
        t = m.f('wr%da' % k, 'fmul', R[6 + k], v[2])
        t = m.mad('wr%db' % k, R[3 + k], v[1], t)
        wr.append(m.mad('wr%d' % k, R[k], v[0], t))
    found, ox, oy, oz, oR = ssr.emit_other(m, h_other, cam, wr, rotation=True)
    byte = []
    for k in range(4):
        s = m.i('ob%ds' % k, 'lshr', ox, str(8 * k)) if k else ox
        s = m.i('ob%d' % k, 'and', s, '255')
        byte.append(m.f('of%d' % k, 'fmul', m('of%di' % k, 'uitofp i32 %s to float' % s), ssr.flt(1.0 / 255.0)))
    oe, olost = byte[:3], byte[3]
    m.i('oz0', 'and', oy, '65535'); ozs = m('ozs', 'call float @dx.op.legacyF16ToF32(i32 131, i32 %soz0)' % q)
    m.i('oa0', 'lshr', oy, '16'); m('oa1', 'uitofp i32 %soa0 to float' % q); oao = m.f('oao', 'fmul', q + 'oa1', ssr.flt(1.0 / 65535.0))
    m.f('dz0', 'fsub', ozs, oz); m.un('dz', 6, q + 'dz0')
    m.mad('lim', tol, oz, ssr.flt(0.01))
    m('dok', 'fcmp fast olt float %sdz, %slim' % (q, q))
    same = m.both('same', found, q + 'dok')
    m.f('gap', 'fsub', lost, olost); m.f('gs', 'fmul', q + 'gap', strength); t = m.un('t', 7, q + 'gs')
    m('tp', 'fcmp fast ogt float %s, 0.000000e+00' % t)
    use = m.both('use', same, q + 'tp')
    m.f('ad', 'fsub', oao, ao); mao = m.mad('mao', q + 'ad', t, ao)
    # bent normals: stored as (0.5 + 0.5 x, 0.5 - 0.5 y, 0.5 + 0.5 z) of a vector in the trace's normal space,
    # which is its reconstruction space with y and z negated (S); the other's goes S R R_o^T S into this one's
    sb = [m.mad('sb0', oe[0], '2.000000e+00', '-1.000000e+00'),
          m.mad('sb1', oe[1], '2.000000e+00', '-1.000000e+00'),          # -(1 - 2e) = 2e - 1
          m.mad('sb2', oe[2], '-2.000000e+00', '1.000000e+00')]          # -(2e - 1)
    w = []
    for k in range(3):        # R_o^T (S b)
        t2 = m.f('ow%da' % k, 'fmul', oR[6 + k], sb[2])
        t2 = m.mad('ow%db' % k, oR[3 + k], sb[1], t2)
        w.append(m.mad('ow%d' % k, oR[k], sb[0], t2))
    rv = []
    for j in range(3):        # R (that)
        t2 = m.f('rv%da' % j, 'fmul', R[3 * j + 2], w[2])
        t2 = m.mad('rv%db' % j, R[3 * j + 1], w[1], t2)
        rv.append(m.mad('rv%d' % j, R[3 * j], w[0], t2))
    rot = [rv[0], m.f('rn1', 'fsub', '-0.000000e+00', rv[1]), m.f('rn2', 'fsub', '-0.000000e+00', rv[2])]
    own = [m.mad('zo0', e[0], '2.000000e+00', '-1.000000e+00'),
           m.mad('zo1', e[1], '-2.000000e+00', '1.000000e+00'),
           m.mad('zo2', e[2], '2.000000e+00', '-1.000000e+00')]
    mix = [m.mad('zm%d' % k, m.f('zd%d' % k, 'fsub', rot[k], own[k]), t, own[k]) for k in range(3)]
    m('zl', 'call float @dx.op.dot3.f32(i32 55, float %s, float %s, float %s, float %s, float %s, float %s)' % tuple(mix + mix))
    m.bin('zl1', 35, q + 'zl', ssr.flt(1e-8)); inv = m.un('zli', 25, q + 'zl1')
    nb = [m.f('zn%d' % k, 'fmul', mix[k], inv) for k in range(3)]
    enc = [m.mad('ze0', nb[0], '5.000000e-01', '5.000000e-01'),
           m.mad('ze1', nb[1], '-5.000000e-01', '5.000000e-01'),
           m.mad('ze2', nb[2], '5.000000e-01', '5.000000e-01')]
    # diagnostics: 1 = the pixels that moved towards the other eye black (AO 0); 2 = the other eye's AO wherever
    # it is the same surface (open elsewhere); 3 = this eye's own result
    m('md1', 'icmp eq i32 %s, 1' % mode); m('md2', 'icmp eq i32 %s, 2' % mode); m('md3', 'icmp eq i32 %s, 3' % mode)
    shown = m.sel('sh', q + 'md1', '0.000000e+00', mao)
    rao = m.sel('ra', use, shown, ao)
    res = [m.sel('re%d' % k, use, enc[k], e[k]) for k in range(3)]
    m.sel('d2', same, oao, '1.000000e+00')
    rao = m.sel('rb', q + 'md2', q + 'd2', rao)
    rao = m.sel('rc', q + 'md3', ao, rao)
    res = [m.sel('rf%d' % k, q + 'md3', e[k], m.sel('rd%d' % k, q + 'md2', e[k], res[k])) for k in range(3)]
    return res, rao


def block_at(blocks, line):
    """The label of the block holding line index `line`."""
    return next(label for label, start, end, _ in blocks if start <= line <= end)


def loop_of(lines, first):
    """The loop around line `first`: (header label line, latch label, latch terminator line, blocks) where
    blocks = [(label, start, terminator line, successors)] from the header to the latch in program order.
    The header is the block whose i32 phi counts the trips and takes its next value from the latch."""
    starts = [i for i, l in enumerate(lines) if l.startswith('; <label>:')]
    label = lambda i: '%' + lines[i].split()[1].split(':')[1]
    for h in reversed([i for i in starts if i <= first]):
        k = h + 1
        while ' = phi ' in lines[k]:
            m = re.fullmatch(r'  %\d+ = phi i32 \[ 0, %\d+ \], \[ %\d+, (%\d+) \]', lines[k])
            if m:
                latch = m.group(1)
                ls = next((i for i in starts if label(i) == latch), None)
                if ls is None or ls < h:
                    break
                blocks = []
                for s in [i for i in starts if h <= i <= ls]:
                    t = s + 1
                    while not lines[t].startswith(('  br ', '  ret')):
                        t += 1
                    blocks.append((label(s), s, t, re.findall(r'label (%\d+)', lines[t])))
                if blocks[-1][2] >= first:
                    return h, latch, blocks[-1][2], blocks
            k += 1
    raise ValueError('no counted loop around line %d' % first)


def dominators(blocks, target):
    """The labels of the blocks between the header and `target` that every path to `target` crosses."""
    names = [b[0] for b in blocks]
    preds = {n: [b[0] for b in blocks if n in b[3] and b[0] != target] for n in names}
    dom = {n: set(names) for n in names}
    dom[names[0]] = {names[0]}
    changed = True
    while changed:
        changed = False
        for n in names[1:]:
            ps = [p for p in preds[n] if p in dom]
            new = {n} | (set.intersection(*(dom[p] for p in ps)) if ps else set())
            if new != dom[n]:
                dom[n], changed = new, True
    return dom[target]


def ao_merge(ir):
    """The cone trace: the lost-sample count in its loop, header and own entry into u16, the merge from u17
    before its one store."""
    b3 = cbv_handle(ir, REGISTER)
    found = re.findall(r'(?m)^  call void @dx\.op\.textureStore\.f32\(i32 67, %dx\.types\.Handle (%\d+), i32 (%\d+), i32 (%\d+), '
                       r'i32 undef, float (%\d+), float (%\d+), float (%\d+), float (%\d+), i8 15\)', ir)
    if len(found) != 1:
        raise ValueError('expected the one bent normal / AO store, found %d' % len(found))
    uav, px, py, e0, e1, e2, ao = found[0]
    lines = ir.split('\n')
    store = next(i for i, l in enumerate(lines) if l.startswith('  call void @dx.op.textureStore.f32(i32 67, %%dx.types.Handle %s, ' % uav))
    if lines[store + 1] != '  ret void':
        raise ValueError('the store is not the last thing the trace does')
    defs = dict(DEF.findall(ir))
    # the samples that leave the picture, all in the loop body
    tests = offscreen_tests(defs)
    if not tests:
        raise ValueError('no off-screen sample tests')
    at = lambda v: next(i for i, l in enumerate(lines) if l.startswith('  %s = ' % v))
    # 2.31 runs the six samples in one block that is the whole loop. 2.5 wraps each sample's x in a branch
    # (frac(2x) on one side), so the loop is a chain of diamonds from its header (the trip counter's phi) to
    # its latch (the block that branches back): the count is summed in the latch, which every test's block
    # must dominate.
    head, body, term, blocks = loop_of(lines, at(tests[0][0]))
    doms = dominators(blocks, body)
    for s, _ in tests:
        if block_at(blocks, at(s)) not in doms:
            raise ValueError('off-screen test %s is not on every path through the loop' % s)
    mt = re.fullmatch(r'  br i1 (%\d+), label (%\d+), label ' + re.escape(blocks[0][0]), lines[term])
    if not mt:
        raise ValueError('loop latch %s does not branch back to its header: %s' % (body, lines[term]))
    counter = re.search(r'(?m)^  (%\d+) = phi i32 \[ 0, (%\d+) \], \[ (%\d+), ' + re.escape(body) + r' \]$', ir)
    if not counter:
        raise ValueError('no loop counter phi in %s' % body)
    pre, nxt = counter.group(2), counter.group(3)
    trips = re.fullmatch(r'icmp eq i32 ' + re.escape(nxt) + r', (\d+)', defs.get(mt.group(1), ''))
    if not trips:
        raise ValueError('loop exit is not a fixed trip count')
    total = int(trips.group(1)) * len(tests)
    # the pixel's view position as the (centred) reconstruction computes it: z the depth loaded at the
    # pixel, x and y the sites patch() rewrote whose ndc comes from the pixel's own coordinate
    depth = [k for k, v in defs.items() if re.fullmatch(r'extractvalue %dx\.types\.ResRet\.f32 (%\d+), 0', v) and
             re.fullmatch(r'call %dx\.types\.ResRet\.f32 @dx\.op\.textureLoad\.f32\(i32 66, %dx\.types\.Handle %\d+, i32 0, i32 '
                          + re.escape(px) + ', i32 ' + re.escape(py) + r', i32 undef, .*', defs[re.fullmatch(r'extractvalue \S+ (%\d+), 0', v).group(1)])]
    if len(depth) != 1:
        raise ValueError('pixel depth loads: %s' % depth)
    vz = depth[0]
    view = {}
    for out, scaled, site in re.findall(r'(?m)^  (%\d+) = fmul fast float (%\d+), (%asym\.ao\.r\d+\.)n$', ir):
        mm = re.fullmatch(r'fmul fast float (%\d+), (%\d+)', defs.get(scaled, ''))
        if not mm or vz not in mm.groups():
            continue
        tan = mm.group(1) if mm.group(2) == vz else mm.group(2)
        axis = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 %\d+, ([01])', defs.get(tan, ''))
        ndc = re.search(r'(?m)^  ' + re.escape(site) + r'n = fsub fast float (%\d+), ', ir).group(1)
        if axis and int(axis.group(1)) not in view and reaches(defs, ndc, (px, py)[int(axis.group(1))]):
            view[int(axis.group(1))] = out
    if sorted(view) != [0, 1]:
        raise ValueError('pixel reconstruction sites for axes %s' % sorted(view))
    if not re.search(r'(?m)^  %asym\.ao\.cx = ', ir):
        raise ValueError('the merge needs the block-written centre (--centre row)')
    if re.search(r'@dx\.op\.bufferStore|legacyF16ToF32|legacyF32ToF16', ir):
        raise ValueError('already uses the ops this adds')
    q, lab = '%asym.mg.', 'asym.mg.'
    # the lost-sample count
    count = ['  %sms = phi i32 [ 0, %s ], [ %sms%d, %s ]' % (q, pre, q, len(tests), body)]
    sums, prev = [], q + 'ms'
    for k, (_, cond) in enumerate(tests):
        sums.append('  %smz%d = zext i1 %s to i32' % (q, k, cond))
        sums.append('  %sms%d = add i32 %s, %smz%d' % (q, k + 1, prev, q, k))
        prev = '%sms%d' % (q, k + 1)
    _, ids = ssr.store_metadata(ir)
    handles = ['  %sh2 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[0], ssr.OWN_SLOT),
               '  %sh3 = call %%dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 %d, i32 %d, i1 false)' % (q, ids[1], ssr.OTHER_SLOT)]
    h2, h3 = q + 'h2', q + 'h3'
    e = ssr.Emit(q)
    r11 = e.cb('r11', b3, MERGE_ROW, 'i32')
    stamp = e.ex('stamp', r11, 0, '%dx.types.CBufRet.i32'); tag = e.ex('tag', r11, 1, '%dx.types.CBufRet.i32')
    on = e('on', 'icmp eq i32 %s, %d' % (tag, ssr.i32(AO_TAG)))
    r11f = e.cb('r11f', b3, MERGE_ROW)
    tol, strength = e.ex('tol', r11f, 2), e.ex('str', r11f, 3)
    r12 = e.cb('r12', b3, MODE_ROW, 'i32'); mode = e.ex('mode', r12, MODE_COMP, '%dx.types.CBufRet.i32')
    r1 = e.cb('r1', b3, 1)
    width = e('w', 'fptoui float %s to i32' % e.ex('wf', r1, 2)); height = e('h', 'fptoui float %s to i32' % e.ex('hf', r1, 3))
    rows = [e.cb('v%d' % r, b3, r) for r in VIEW_ROWS]
    # world -> reconstruction space: x right (row 4), y down (-row 5: the trace's ndc y runs down the picture
    # with a positive tangent), z forward (row 6)
    R = [e.ex('R0%d' % k, rows[0], k) for k in range(3)]
    R += [e.f('R1%d' % k, 'fsub', '-0.000000e+00', e.ex('U%d' % k, rows[1], k)) for k in range(3)]
    R += [e.ex('R2%d' % k, rows[2], k) for k in range(3)]
    cam = [e.ex('c%d' % k, rows[3], k) for k in range(3)]
    r0 = e.cb('r0', b3, 0)
    tx, ty = e.ex('tx', r0, 0), e.ex('ty', r0, 1)
    ix = e.f('ix', 'fdiv', '1.000000e+00', tx); iy = e.f('iy', 'fdiv', '-1.000000e+00', ty)
    ncy = e.f('ncy', 'fsub', '-0.000000e+00', '%asym.ao.cy')
    lost = e.f('lost', 'fmul', e('lostf', 'uitofp i32 %s to float' % prev), ssr.flt(1.0 / total))
    e.i('qx', 'and', px, '31'); e.i('qy', 'and', py, '31'); e.i('qxy', 'or', q + 'qx', q + 'qy')
    e('corner', 'icmp eq i32 %sqxy, 0' % q); e.both('hw', on, q + 'corner')
    e.raw('  br i1 %shw, label %%%shdr, label %%%sgo' % (q, lab, lab)); e.raw(''); e.raw('%shdr:' % lab)
    zero = '0'
    d = [e.bits('hR%d' % k, R[k]) for k in range(9)] + [e.bits('hc%d' % k, cam[k]) for k in range(3)]
    d += [e.bits('hp0', ix), zero, e.bits('hp2', '%asym.ao.cx'), zero,
          zero, e.bits('hp5', iy), e.bits('hp6', ncy), zero,
          zero, zero, zero, zero,
          zero, zero, str(ssr.i32(struct.unpack('<I', struct.pack('<f', 1.0))[0])), zero]
    d += [stamp, width, height, str(ssr.i32(ssr.MERGE_MAGIC)), zero, zero, zero, zero]
    assert len(d) == 3 * ssr.SLOT
    e.i('hsl', 'and', stamp, str(ssr.RING - 1)); hb = e.i('hbs', 'mul', q + 'hsl', str(ssr.SLOT))
    for k in range(ssr.SLOT):
        e.store(h2, e.i('hi%d' % k, 'add', hb, str(k)), d[3 * k], d[3 * k + 1], d[3 * k + 2])
    e.store(h2, str(ssr.LATEST), stamp, str(ssr.i32(ssr.MERGE_MAGIC)), '0')
    e.raw('  br label %%%sgo' % lab); e.raw(''); e.raw('%sgo:' % lab)
    e.raw('  br i1 %s, label %%%sm1, label %%%sm9' % (on, lab, lab)); e.raw(''); e.raw('%sm1:' % lab)
    m = e
    # own entry
    m('inx', 'icmp ult i32 %s, %s' % (px, width)); m('iny', 'icmp ult i32 %s, %s' % (py, height))
    m.both('inb', q + 'inx', q + 'iny')
    m.i('row', 'mul', py, width); m.i('pix', 'add', q + 'row', px); m.i('pix2', 'add', q + 'pix', str(ssr.HEADER))
    idx = m.sel('idx', q + 'inb', q + 'pix2', '-1', 'i32')
    packed = None
    for k, val in enumerate((e0, e1, e2, lost)):
        s = m.un('k%d' % k, 7, val); s = m.mad('k%dr' % k, s, ssr.flt(255.0), ssr.flt(0.5))
        s = m('k%di' % k, 'fptoui float %s to i32' % s)
        s = m.i('k%ds' % k, 'shl', s, str(8 * k)) if k else s
        packed = s if packed is None else m.i('kx%d' % k, 'or', packed, s)
    m.bin('kz', 36, vz, ssr.flt(60000.0)); m('hz0', 'call i32 @dx.op.legacyF32ToF16(i32 130, float %skz)' % q)
    m.i('hz', 'and', q + 'hz0', '65535')
    m.un('ka', 7, ao); m.mad('kaf', q + 'ka', ssr.flt(65535.0), ssr.flt(0.5)); m('kai', 'fptoui float %skaf to i32' % q)
    m.i('kas', 'shl', q + 'kai', '16'); sy = m.i('sy', 'or', q + 'hz', q + 'kas')
    m.store(h2, idx, packed, sy, stamp)
    # only a pixel that lost samples can gain from the other eye: the lookup runs there (and everywhere
    # under a diagnostic mode)
    m('lpos', 'fcmp fast ogt float %s, 0.000000e+00' % lost); m('mdon', 'icmp ne i32 %s, 0' % mode)
    m('look', 'or i1 %slpos, %smdon' % (q, q))
    m.raw('  br i1 %slook, label %%%slk, label %%%sm9' % (q, lab, lab)); m.raw(''); m.raw('%slk:' % lab)
    res, rao = emit_ao_merge(m, h3, cam, R, (view[0], view[1], vz), (e0, e1, e2), ao, lost, tol, strength, mode)
    m.raw('  br label %%%sm9' % lab); m.raw(''); m.raw('%sm9:' % lab)
    fin = [m('f%d' % k, 'phi float [ %s, %%%sgo ], [ %s, %%%sm1 ], [ %s, %%%slk ]' % (own, lab, own, lab, new, lab))
           for k, (own, new) in enumerate(((e0, res[0]), (e1, res[1]), (e2, res[2]), (ao, rao)))]
    lines[store] = lines[store].replace('float %s, float %s, float %s, float %s, i8 15' % (e0, e1, e2, ao),
                                        'float %s, float %s, float %s, float %s, i8 15' % tuple(fin))
    lines[store:store] = m.lines
    lines[term:term] = sums
    phis_end = head + 1
    while ' = phi ' in lines[phis_end]:
        phis_end += 1
    lines[phis_end:phis_end] = count
    entry = next(i for i, l in enumerate(lines) if l.startswith('define void @cs_main_()'))
    lines[entry + 1:entry + 1] = handles
    ir = '\n'.join(lines)
    ir, _ = ssr.store_metadata(ir)
    ir = ssr.add_types(ir, [('%dx.types.ResRet.i32', '{ i32, i32, i32, i32, i32 }'), ('%dx.types.CBufRet.i32', '{ i32, i32, i32, i32 }')])
    return ssr.add_declarations(ir), total


def structured_flag(ir):
    """The shader-flags entry property (tag 0) with raw/structured buffers (0x10) set: the stores are the
    trace's first structured buffers."""
    entry = re.search(r'(?m)^!\d+ = !\{void \(\)\* @cs_main_, !"cs_main_", null, !\d+, (!\d+)\}$', ir)
    props = entry.group(1)
    line = re.search(r'(?m)^%s = !\{(.*)\}$' % re.escape(props), ir)
    items = line.group(1)
    m = re.match(r'i32 0, i64 (\d+)', items)
    items = ('i32 0, i64 %d' % (int(m.group(1)) | 0x10)) + items[m.end():] if m else 'i32 0, i64 16, ' + items
    return ir[:line.start()] + '%s = !{%s}' % (props, items) + ir[line.end():]


def build(original, centre, ham_recipe=None, merge=False):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, nr, np_ = patch(ham.disassemble(original, work), centre)
        if merge:
            ir, _ = ao_merge(ir)
            ir = structured_flag(ir)
        container = ham.assemble(ham_recipe(ir) if ham_recipe else ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    if ham_recipe:
        # the hidden-area trailer, as ham_shaders.build writes it
        offsets = []
        for value in ham.PLACEHOLDERS:
            found = dc.find_bits(container, dc.float_pattern(value))
            if len(found) != 1:
                raise RuntimeError('placeholder %r found %d times' % (value, len(found)))
            offsets += found
        container += b'HAM1' + struct.pack('<I%dI' % len(offsets), len(offsets), *offsets)
    return container, nr, np_


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True, help='original shaders named <fnv1a>.dxil')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--ham-out', type=Path, help='also write the hidden-area recipe on top, where one exists')
    parser.add_argument('--centre', default='row', help='row (the plugin-written centre, default) or const:X,Y')
    args = parser.parse_args()
    kind, _, value = args.centre.partition(':')
    centre = ('row', CENTRE_ROW) if kind == 'row' else ('const', tuple(float(x) for x in value.split(',')))
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
            merge = key in AO_TRACES and kind == 'row'
            patched, nr, np_ = build(original, centre, merge=merge)
            (args.out / ('%016x.dxil' % key)).write_bytes(patched)
            print('OK %016x %s: %d reconstruction, %d projection site(s)%s, %d -> %d bytes' % (key, name, nr, np_, ', wide merge' if merge else '', len(original), len(patched)))
            if args.ham_out and key in ham.RECIPES:
                args.ham_out.mkdir(parents=True, exist_ok=True)
                combined, _, _ = build(original, centre, ham.RECIPES[key][1], merge=merge)
                (args.ham_out / ('%016x.dxil' % key)).write_bytes(combined)
                print('OK %016x hidden-area recipe on top: %d bytes' % (key, len(combined)))
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
