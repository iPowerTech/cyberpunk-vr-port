"""Volumetric-fog shaders patched for the asymmetric projection (docs: memory cpvr-asym-projection).

The engine's volumetric fog (render-graph node 0x61C3BC) builds the ray of every froxel as

    ray = right * (tanX * ndc.x) + up * (tanY * ndc.y) + forward

with tanX/tanY from the prepare-scene block (b12 row 63, row 64 an additive term that is 0). There is no
off-centre term: P20/P21 -- where the view producer puts the projection's NDC shift next to the TAA
jitter -- never reach the ray. Under Stereo/AsymProjection.cpp's shift the froxels are lit for rays
~14 degrees away from the pixels they are drawn on, while the history is reprojected with the true
(shifted) matrix: light shafts hang in the wrong place, smear and break into blocks.

Each recipe subtracts the shift from the froxel's NDC wherever it is scaled into a ray:

    ndc' = ndc - (P2x - jitter)       P20/P21 = camera b1 row 8.z / 9.z, NDC jitter = b1 row 51.xy

all from the camera block the shader already binds, so the three numbers belong to one view and one
frame. With no shift (P2x == jitter) the result equals the original shader's. The jitter is excluded
because the native froxels are unjittered (the engine ignores P2x altogether).

2.5's engine stereo (Stereo/EngineStereo.cpp) runs the froxels of both eyes in one dispatch, the ray
from each eye's own inverse projection and position (b6 rows 24/29-31 left, 37/42-44 right) -- a path
the recipe above does not touch. But the scattering pass culls the fog's lights per 4x4x4 group with
tile planes from the CENTRE's projection and position (b6 rows 0-3 and 7), each eye's tiles folded onto
it. The engine's eyes differ from the centre by a convergence term; under the port's per-eye windows
(P20 +-0.2425, the centre the union of both) the planes miss each eye's tiles and the two eyes' fog lit
different lights (2026-10-23: green shafts and haze on a wall in one eye only, gone with the union
window). The second recipe gives the group's thread 0 its eye's numbers in the stereo branch only:

    P00 = 1 / inv.x     P20 = -inv.w / inv.x     position = the eye's    (inv = the eye's row 29 / 42)

the eye's vertical terms equal the centre's. The mono branch keeps the original values.

Output: <FNV-1a of the original>.dxil, a signed container with nothing appended; the plugin loads
them from `asym\\` beside itself and binds the twin while CyberpunkVR_AsymFogPatch is on.

  py -3.13 -B asym_fog_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham          # disassemble / assemble / fnv1a / dxc + validator paths
import dxil_container as dc

# FNV-1a of the original -> name. All of them build froxel rays as above. The same three run at every
# VolumetricFogResolution (Low to Ultra); with ray tracing on the scattering pass is 168d... instead.
SHADERS = {
    0x1dc449918634d656: 'VolumetricFog scattering (froxel inject + history)',
    0x2ba4e029e1931fa9: 'VolumetricFog indirect pass',
    0x9535892c88d0f345: 'VolumetricFog integration',
    0x168d9acf8a2d71f2: 'VolumetricFog scattering, ray tracing on',
}

HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')
# x: P20 = camera row 8.z, jitter row 51.x; y: P21 = row 9.z, jitter 51.y
AXES = ((8, 0), (9, 1))
PRELUDE = (
    '  {q}cp = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {h}, i32 {row})\n'
    '  {q}p = extractvalue %dx.types.CBufRet.f32 {q}cp, 2\n'
    '  {q}cj = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {h}, i32 51)\n'
    '  {q}j = extractvalue %dx.types.CBufRet.f32 {q}cj, {comp}\n'
    '  {q}s = fsub fast float {q}p, {q}j\n'
    '  {q}n = fsub fast float {ndc}, {q}s\n'
    '  {out} = fmul fast float {scale}, {q}n')


def cbv_handle(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    if len(found) != 1:
        raise ValueError('expected one handle for b%d, found %d' % (register, len(found)))
    return found[0]


def shift_rays(ir):
    b12, b1 = cbv_handle(ir, 12), cbv_handle(ir, 1)
    loads = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                       r'%dx\.types\.Handle ' + re.escape(b12) + r', i32 64\)', ir)
    if not loads:
        raise ValueError('no ray-scale load (b12 row 64)')
    sites = 0
    for k, load in enumerate(loads):
        for c, (row, comp) in enumerate(AXES):
            m = re.search(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(load) + ', %d$' % c, ir)
            if not m:
                raise ValueError('row 64 component %d of %s not extracted' % (c, load))
            value = m.group(1)
            adds = re.findall(r'(?m)^  (%\d+) = fadd fast float (?:' + re.escape(value) + r', %\d+|%\d+, ' + re.escape(value) + r')$', ir)
            if len(adds) != 1:
                raise ValueError('ray scale %s: %d adds' % (value, len(adds)))
            scale = adds[0]
            # every ray built with this scale: scale * ndc, either operand order
            muls = re.findall(r'(?m)^  (%\d+) = fmul fast float (?:' + re.escape(scale) + r', (%\d+)|(%\d+), ' + re.escape(scale) + r')$', ir)
            if not muls:
                raise ValueError('ray scale %s: no multiply' % scale)
            for j, (out, ndc_a, ndc_b) in enumerate(muls):
                prelude = PRELUDE.format(q='%%asym.%d.%d.%d.' % (k, c, j), h=b1, row=row, comp=comp,
                                         ndc=ndc_a or ndc_b, out=out, scale=scale)
                ir, n = re.subn(r'(?m)^  ' + re.escape(out) + r' = fmul fast float .*$', lambda _m: prelude, ir, count=1)
                if n != 1:
                    raise ValueError('ray multiply %s not rewritten' % out)
                sites += 1
    return ir, sites


# b6 rows of the stereo branch: (eye position, inverse projection row 0) per eye; the centre's P00 / P20
# (rows 0 / 2, .x) and position (row 7) feed the light tiles.
EYE_ROWS = ((24, 29), (37, 42))
CENTRE = (('p00', 0, 0), ('p20', 2, 0), ('x', 7, 0), ('y', 7, 1), ('z', 7, 2))


def cb_load(ir, handle, row):
    return re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                      r'%dx\.types\.Handle ' + re.escape(handle) + r', i32 %d\)' % row, ir)


def extracted(ir, load, comp):
    m = re.search(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(load) + ', %d$' % comp, ir)
    if not m:
        raise ValueError('component %d of %s not extracted' % (comp, load))
    return m.group(1)


def eye_tiles(ir):
    """The stereo branch's light tiles from the eye's own frustum (module docstring); 0 sites for a shader
    without them (the indirect pass, the integration, every 2.31 original)."""
    b6 = cbv_handle(ir, 6)
    centre_loads = {row: cb_load(ir, b6, row) for row in (0, 2, 7)}
    eye_loads = [(cb_load(ir, b6, pos), cb_load(ir, b6, inv)) for pos, inv in EYE_ROWS]
    if not centre_loads[0] or not eye_loads[0][0]:
        return ir, 0
    if any(len(v) != 1 for v in centre_loads.values()) or any(len(p) != 1 or len(i) != 1 for p, i in eye_loads):
        raise ValueError('eye tiles: b6 rows 0/2/7/24/29/37/42 not loaded once each')
    # the branch into the left eye's loads: br i1 <mono>, label %merge, label %left
    left = ir[:ir.index('  ' + eye_loads[0][0][0] + ' = ')].rsplit('; <label>:', 1)[1].split()[0]
    m = re.search(r'(?m)^  br i1 (%\d+), label %\d+, label %' + left + '$', ir)
    if not m:
        raise ValueError('eye tiles: no stereo branch into label %s' % left)
    mono = m.group(1)

    def phi(right, left_value):
        found = re.findall(r'(?m)^  (%\d+) = phi float \[ ' + re.escape(right) + r', %\d+ \], \[ ' + re.escape(left_value) +
                           r', %\d+ \], \[ undef, %0 \]$', ir)
        if len(found) != 1:
            raise ValueError('eye tiles: %d phis of %s / %s' % (len(found), right, left_value))
        return found[0]
    (lp, li), (rp, ri) = [(p[0], i[0]) for p, i in eye_loads]
    pos = [phi(extracted(ir, rp, c), extracted(ir, lp, c)) for c in range(3)]
    inv_x, inv_w = (phi(extracted(ir, ri, c), extracted(ir, li, c)) for c in (0, 3))
    eye = {
        'p00': ['  %eyetile.p00.e = fdiv fast float 1.000000e+00, ' + inv_x],
        'p20': ['  %eyetile.p20.q = fdiv fast float ' + inv_w + ', ' + inv_x,
                '  %eyetile.p20.e = fsub fast float -0.000000e+00, %eyetile.p20.q'],
    }
    sites = 0
    for name, row, comp in CENTRE:
        load = centre_loads[row][0]
        target = extracted(ir, load, comp)
        if ir.index('  ' + target + ' = ') < ir.index('  ' + pos[0] + ' = '):
            raise ValueError('eye tiles: centre %s read before the eye phis' % name)
        value = '%eyetile.' + name + '.e' if name in eye else pos['xyz'.index(name)]
        lines = ['  %%eyetile.%s.c = extractvalue %%dx.types.CBufRet.f32 %s, %d' % (name, load, comp)] + eye.get(name, []) + \
                ['  %s = select i1 %s, float %%eyetile.%s.c, float %s' % (target, mono, name, value)]
        ir, n = re.subn(r'(?m)^  ' + re.escape(target) + r' = extractvalue .*$', lambda _m: '\n'.join(lines), ir, count=1)
        if n != 1:
            raise ValueError('eye tiles: %s not rewritten' % target)
        sites += 1
    return ir, sites


# THE SCATTERING HISTORY, LONGER WHILE THE VIEW HOLDS STILL. Each froxel is sampled at
# cell + frac(COM row 10 - 0.5 + noise)  -- a new offset every frame -- and blended into its reprojected history at
# 0.98. Near a light's cone edge the samples land in and out of the cone, and the 2 % of a new sample left the haze
# under lit signs stepping on a still camera (2026-10-27, crossing at -2413.6, -958.2, 23:00, cloudy). Canopy band,
# mean per-pixel max step over 12 grabs: Medium 3.7, Ultra 1.0-1.1 (smaller froxels). The per-frame offset frozen
# removes the steps but resolves every point light into a round blob; half the offset changes nothing. A longer
# history keeps less of each new sample: 0.99 gave 1.7-2.2, 0.995 gave 1.0 = Ultra -- but a long history also
# smears the haze while the view moves and leaves a long trail behind moving lights. So the weight follows how far
# the reprojection moved this froxel: the history sample's texel against the froxel's own (row 43 .yzw = the
# volume's size), 0.995 at rest, down to the native 0.98 at a quarter texel and beyond (a 30 deg/s head turn is
# about a third of a texel per frame at 80 fps).
NATIVE_HISTORY = 'float 0x3FEF5C2900000000, float 0.000000e+00'   # select(valid, 0.98, 0)
REST_WEIGHT, MOVING_WEIGHT, FULL_MOTION_TEXELS = 0.995, 0.98, 0.25


def ir_float(value):
    import struct
    single = struct.unpack('<f', struct.pack('<f', value))[0]
    return '0x%016X' % struct.unpack('<Q', struct.pack('<d', single))[0]


def motion_history(ir):
    """0 sites for a shader without the scattering history (the indirect pass, the integration)."""
    sites = re.findall(r'(?m)^  (%\d+) = select i1 (%\d+), ' + re.escape(NATIVE_HISTORY) + '$', ir)
    if not sites:
        return ir, 0
    if len(sites) != 1:
        raise ValueError('history weight: %d sites' % len(sites))
    weight, valid = sites[0]
    alpha = re.search(r'(?m)^  ' + re.escape(valid) + r' = fcmp fast ogt float (%\d+), 0\.000000e\+00$', ir).group(1)
    sample = re.search(r'(?m)^  ' + re.escape(alpha) + r' = extractvalue %dx\.types\.ResRet\.f32 (%\d+), 3$', ir).group(1)
    coords = re.search(r'(?m)^  ' + re.escape(sample) + r' = call %dx\.types\.ResRet\.f32 @dx\.op\.sampleLevel\.f32\(i32 62, '
                       r'%dx\.types\.Handle %\d+, %dx\.types\.Handle %\d+, float (%\d+), float (%\d+), float (%\d+), ', ir)
    if not coords:
        raise ValueError('history sample coordinates not found')
    threads = [re.search(r'(?m)^  (%\d+) = call i32 @dx\.op\.threadId\.i32\(i32 93, i32 ' + str(c) + r'\)', ir).group(1)
               for c in range(3)]
    size = cbv_handle(ir, 12)   # the prepare-scene block: row 43 .yzw = the froxel volume's size
    if not cb_load(ir, size, 43):
        raise ValueError('volume size (b12 row 43) not loaded')
    lines = ['  %%mh.size = call %%dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %%dx.types.Handle %s, i32 43)' % size]
    for axis in range(3):
        a = 'xyz'[axis]
        lines += [
            '  %%mh.n%s = extractvalue %%dx.types.CBufRet.f32 %%mh.size, %d' % (a, axis + 1),
            '  %%mh.t%s = uitofp i32 %s to float' % (a, threads[axis]),
            '  %%mh.c%s = fadd fast float %%mh.t%s, 5.000000e-01' % (a, a),
            '  %%mh.h%s = fmul fast float %s, %%mh.n%s' % (a, coords.group(axis + 1), a),
            '  %%mh.d%s = fsub fast float %%mh.h%s, %%mh.c%s' % (a, a, a),
            '  %%mh.a%s = call float @dx.op.unary.f32(i32 6, float %%mh.d%s)' % (a, a),
        ]
    lines += [
        '  %mh.xy = call float @dx.op.binary.f32(i32 35, float %mh.ax, float %mh.ay)',
        '  %mh.m = call float @dx.op.binary.f32(i32 35, float %mh.xy, float %mh.az)',
        '  %%mh.s = fmul fast float %%mh.m, %s' % ir_float(1.0 / FULL_MOTION_TEXELS),
        '  %mh.k = call float @dx.op.binary.f32(i32 36, float %mh.s, float 1.000000e+00)',
        '  %%mh.r = fmul fast float %%mh.k, %s' % ir_float(REST_WEIGHT - MOVING_WEIGHT),
        '  %%mh.w = fsub fast float %s, %%mh.r' % ir_float(REST_WEIGHT),
        '  %s = select i1 %s, float %%mh.w, float 0.000000e+00' % (weight, valid),
    ]
    ir, n = re.subn(r'(?m)^  ' + re.escape(weight) + r' = select i1 .*$', lambda _m: '\n'.join(lines), ir, count=1)
    if n != 1:
        raise ValueError('history weight not rewritten')
    return ir, 1


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, count = shift_rays(ham.disassemble(original, work))
        ir, tiles = eye_tiles(ir)
        ir, _ = motion_history(ir)
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, count, tiles


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
            patched, count, tiles = build(original)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d ray component(s) shifted, %d eye tile value(s), %d -> %d bytes'
              % (key, name, count, tiles, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
