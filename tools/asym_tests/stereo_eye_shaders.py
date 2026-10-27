"""2.5 engine stereo: one dispatch over both eyes must take each pixel's eye from the pixel, not from the camera
block it was dispatched with (2026-10-23, clouds dark and sharp in the left eye, soft and nearly gone in the
right, CPCHECKSKYLIGHTFOG.rdc).

The volumetric clouds march at half resolution in ONE dispatch over the side-by-side frame (event 42315 of the
capture, the twins' original 8a6c46c528ce5160). The ray already comes from the pixel's own half: x past the
half width is eye 2 and takes the right eye's inverse (b12 rows 109-112), else the left's (rows 89-92). But the
aerial-perspective lookup into the froxel fog volume (192 x 104 over the whole frame, each eye's half its own)
turns the pixel's eye-local position back into a frame position with the CAMERA's eye -- b1 row 53.x, 2 for
the right eye --

    u = ((kind == 2 ? W : 0) + x_eye) * 0.5 / W,  then clamped into the half u fell in

and the dispatch has one camera block, the right eye's (P20 -0.2428). Every left-eye cloud pixel read the
fog of the right eye's half at its own eye-local position, a different direction: the left eye's clouds lost
the haze in front of them (measured in the low-res cloud target: same shape, offset 186 px, the right eye
1.25x brighter, up to 1.9x on the dark parts; composited, the left eye's clouds changed the sky 6x more).
The second lookup in the same shader, after the march, takes the frame position directly and is right.

The twin compares the pixel's eye (the shader's own phi, `select i1 x > W/2, i32 2, i32 1` in the two-halves
branch and 1 outside it) with 2 instead of the camera's kind. In the mono branch the camera kind is 0 and the
pixel's eye 1: both compare false, so the mono path is the engine's.

The recipe is structural, so every quality of the march (and any other shader with the same lookup) is found
by scanning: `--shaders` is a folder of original compute shaders named <FNV-1a>.dxil
(extract_shader_cache.py --kind cs over engine\\staticshader_final.cache).

Output: <FNV-1a of the original>.dxil signed containers for `stereo_eye\\` (Render/HiddenAreaShaders.cpp
group 9, CyberpunkVR_StereoEyePatch).

  py -3.13 -B stereo_eye_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham          # disassemble / assemble / fnv1a / dxc + validator paths
import dxil_container as dc

HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')
KIND_ROW = 53                      # b1 CameraShaderConsts +848: the camera's eye, 0 centre / 1 left / 2 right
FRAME_ROW = 3                      # b0 row 3.x: the frame's full width


def cbv_handle(ir, register):
    found = [h for h, r in HANDLE.findall(ir) if int(r) == register]
    return found[0] if len(found) == 1 else None


def pixel_eye(ir):
    """The two-halves branch's eye of the pixel: phi i32 [ select(x > W/2, 2, 1) ], [ 1 ] -- one per shader."""
    sels = set(re.findall(r'(?m)^  (%\d+) = select i1 %\d+, i32 2, i32 1$', ir))
    phis = [p for p, s in re.findall(r'(?m)^  (%\d+) = phi i32 \[ (%\d+), %\d+ \], \[ 1, %\d+ \]$', ir) if s in sels]
    phis += [p for p, s in re.findall(r'(?m)^  (%\d+) = phi i32 \[ 1, %\d+ \], \[ (%\d+), %\d+ \]$', ir) if s in sels]
    return phis[0] if len(phis) == 1 else None


def eye_lookups(ir):
    """Every `kind == 2` that picks the right half's frame offset -- select(kind == 2, W + x, x) -- compares the
    pixel's eye instead. 0 sites for a shader without such a lookup."""
    b1, b0 = cbv_handle(ir, 1), cbv_handle(ir, 0)
    if not b1 or not b0:
        return ir, 0
    loads = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.i32 @dx\.op\.cbufferLoadLegacy\.i32\(i32 59, '
                       r'%dx\.types\.Handle ' + re.escape(b1) + r', i32 ' + str(KIND_ROW) + r'\)', ir)
    widths = set()
    for load in re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                           r'%dx\.types\.Handle ' + re.escape(b0) + r', i32 ' + str(FRAME_ROW) + r'\)', ir):
        widths |= set(re.findall(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(load) + ', 0$', ir))
    sites = []
    for load in loads:
        for kind in re.findall(r'(?m)^  (%\d+) = extractvalue %dx\.types\.CBufRet\.i32 ' + re.escape(load) + ', 0$', ir):
            for cmp in re.findall(r'(?m)^  (%\d+) = icmp eq i32 ' + re.escape(kind) + r', 2$', ir):
                for chosen, x in re.findall(r'(?m)^  %\d+ = select i1 ' + re.escape(cmp) + r', float (%\d+), float (%\d+)$', ir):
                    m = re.search(r'(?m)^  ' + re.escape(chosen) + r' = fadd fast float (%\d+), (%\d+)$', ir)
                    others = [o for o in m.groups() if o != x] if m and x in m.groups() else []
                    if len(others) == 1 and others[0] in widths:
                        sites.append((cmp, kind))
    if not sites:
        return ir, 0
    eye = pixel_eye(ir)
    if not eye:
        raise ValueError('eye lookup without the pixel eye phi')
    if ir.index('  ' + eye + ' = ') > min(ir.index('  ' + c + ' = ') for c, _ in sites):
        raise ValueError('pixel eye defined after the lookup')
    for cmp, kind in set(sites):
        ir, n = re.subn(r'(?m)^  ' + re.escape(cmp) + r' = icmp eq i32 ' + re.escape(kind) + r', 2$',
                        '  %s = icmp eq i32 %s, 2' % (cmp, eye), ir, count=1)
        if n != 1:
            raise ValueError('lookup %s not rewritten' % cmp)
    return ir, len(set(sites))


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, sites = eye_lookups(ham.disassemble(original, work))
        if not sites:
            return None, 0
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, sites


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True, help='original compute shaders named <fnv1a>.dxil')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    made = failed = 0
    for source in sorted(args.shaders.glob('*.dxil')):
        original = source.read_bytes()
        key = ham.fnv1a(original)
        if '%016x' % key != source.stem:
            raise RuntimeError('%s does not hash to its name' % source)
        try:
            patched, sites = build(original)
        except Exception as error:
            print('FAILED %016x: %s' % (key, error)); failed += 1; continue
        if patched is None:
            continue
        (args.out / ('%016x.dxil' % key)).write_bytes(patched)
        made += 1
        print('OK %016x: %d eye lookup(s), %d -> %d bytes' % (key, sites, len(original), len(patched)))
    print('%d twin(s), %d failed' % (made, failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
