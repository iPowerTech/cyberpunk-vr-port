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


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, count = shift_rays(ham.disassemble(original, work))
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, count


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
            patched, count = build(original)
        except Exception as error:
            print('FAILED %016x %s: %s' % (key, name, error)); failed += 1; continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        print('OK %016x %s: %d ray component(s) shifted, %d -> %d bytes' % (key, name, count, len(original), len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
