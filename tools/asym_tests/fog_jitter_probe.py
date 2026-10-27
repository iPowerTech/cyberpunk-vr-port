"""DIAGNOSTIC variants of the deployed volumetric-fog scattering twins (asym_fog), for a live A/B of the fog that
"jumps at rest" (Medium jumps ~11 levels per frame on a still camera, Ultra ~3).

The scattering pass samples each froxel at  cell + frac(COM row 10 - 0.5 + noise(x&31, y&31, z&31))  -- a
per-frame offset (row 10) over a static 32^3 noise -- and blends the result into the reprojected history at
0.98. Two variants separate the candidate causes:

  --freeze    row 10 replaced by 0.5: no temporal jitter. If the still-camera jumps vanish, they are the
              jitter's own noise and the history is not averaging it away.
  --history W the history weight 0.98 replaced by W (e.g. 0.995). If the jumps shrink by about
              sqrt((1-W)/(1+W)) / sqrt(0.02/1.98), the history works and is merely too short.
  --jitter-scale S
              the offset narrowed around the froxel centre: 0.5 + S * (frac(..) - 0.5). Between the
              native jitter (S = 1, jumps) and --freeze (S = 0, point lights resolve into round blobs).

Input is a twin built without motion_history (--history looks for the native 0.98; the shipped twins weigh the
history by motion), output a signed container
under the same name for the plugin's asym\\ folder. Never ship these: they are measurement tools.

  py -3.13 -B fog_jitter_probe.py --twin SRC.dxil --out DIR [--freeze] [--history W]"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc


def com_handle(ir):
    found = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 6, i1 false\)', ir)
    if len(found) != 1:
        raise ValueError('COM (b6) handles: %d' % len(found))
    return found[0]


def freeze(ir):
    h = com_handle(ir)
    loads = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                       r'%dx\.types\.Handle ' + re.escape(h) + r', i32 10\)', ir)
    if len(loads) != 1:
        raise ValueError('row 10 loads: %d' % len(loads))
    n = 0
    for c in range(3):
        ir, k = re.subn(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(loads[0]) + ', ' + str(c) + '$',
                        r'  \1 = fadd fast float 5.000000e-01, 0.000000e+00', ir)
        n += k
    if n != 3:
        raise ValueError('row 10 components rewritten: %d' % n)
    return ir


def jitter_scale(ir, scale):
    """The sample offset inside the froxel narrowed around its centre: 0.5 + scale * (frac(..) - 0.5)."""
    h = com_handle(ir)
    load = re.search(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                     r'%dx\.types\.Handle ' + re.escape(h) + r', i32 10\)', ir)
    if not load:
        raise ValueError('row 10 load not found')
    frcs = re.findall(r'(?m)^  (%\d+) = call float @dx\.op\.unary\.f32\(i32 22, float %\d+\)', ir[load.end():])[:3]
    if len(frcs) != 3:
        raise ValueError('jitter Frc calls: %d' % len(frcs))
    for k, frc in enumerate(frcs):
        use = re.compile(r'(?m)^(  %\d+ = fadd fast float %\d+, )' + re.escape(frc) + '$')
        if len(use.findall(ir)) != 1:
            raise ValueError('jitter use of %s: %d' % (frc, len(use.findall(ir))))
        bias = 0.5 - 0.5 * scale
        ir = use.sub(lambda m: '  %%jit_s%d = fmul fast float %s, %.6e\n  %%jit_o%d = fadd fast float %%jit_s%d, %.6e\n%s%%jit_o%d'
                     % (k, frc, scale, k, k, bias, m.group(1), k), ir)
    return ir


def history(ir, weight):
    old = 'float 0x3FEF5C2900000000, float 0.000000e+00'
    if ir.count(old) != 1:
        raise ValueError('history weight sites: %d' % ir.count(old))
    import struct
    bits = struct.unpack('<d', struct.pack('<d', struct.unpack('<f', struct.pack('<f', weight))[0]))[0]
    return ir.replace(old, 'float 0x%016X, float 0.000000e+00' % struct.unpack('<Q', struct.pack('<d', bits))[0])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--twin', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--freeze', action='store_true')
    ap.add_argument('--history', type=float)
    ap.add_argument('--jitter-scale', type=float)
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir = ham.disassemble(a.twin.read_bytes(), work)
        if a.freeze:
            ir = freeze(ir)
        if a.jitter_scale is not None:
            ir = jitter_scale(ir, a.jitter_scale)
        if a.history is not None:
            ir = history(ir, a.history)
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / a.twin.name).write_bytes(container)
    print('OK', a.twin.name, len(container), 'bytes', 'freeze' if a.freeze else '', a.history or '',
          'jitter x%s' % a.jitter_scale if a.jitter_scale is not None else '')


if __name__ == '__main__':
    main()
