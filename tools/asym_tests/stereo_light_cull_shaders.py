"""2.5 clustered-light bounds must use the current eye's inverse projection scale.

The native light culler uses each eye's projection for its six planes, but a shared
scale in COM row 1 for its bounding sphere and AABB. Under Engine Stereo the shared
scale describes the union camera, whose horizontal FOV is wider than either eye.
The inconsistent bounds reject lights from visible cells, leaving hard 32-pixel
lighting borders that move across geometry when the head turns.

In the stereo path, derive (1/P00, -1/P11) from the same projection as the native
planes. The COM's projection rows are 11/12 for eye index 0, 2/3 for eye index 1
(the native branch order). The existing eye-local coordinates, projection shift,
planes, depth range, light tests and mono scale remain unchanged.

Verified in both checkShadowBordersOrWhatisThis captures. Original shader comes
from engine/staticshader_final.cache; --shaders takes the directory produced by
extract_shader_cache.py. Output belongs in src/Shaders/stereo_light_cull.
"""
import argparse
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

KEY = 0x37851ec24d58e673


def eye_scale(ir):
    # %43 is the native eye index, %100 means mono. %110/%111 are COM row 1.
    anchor = '  %111 = extractvalue %dx.types.CBufRet.f32 %109, 1\n'
    if ir.count(anchor) != 1:
        raise ValueError('Unexpected culling scale load')
    before, after = ir.split(anchor)
    block = '''  %lc.left = icmp eq i32 %43, 0
  %lc.rowx = select i1 %lc.left, i32 11, i32 2
  %lc.rowy = add i32 %lc.rowx, 1
  %lc.px = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %lc.rowx)
  %lc.py = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %9, i32 %lc.rowy)
  %lc.p00 = extractvalue %dx.types.CBufRet.f32 %lc.px, 0
  %lc.p11 = extractvalue %dx.types.CBufRet.f32 %lc.py, 1
  %lc.ex = fdiv fast float 1.000000e+00, %lc.p00
  %lc.ey = fdiv fast float -1.000000e+00, %lc.p11
  %lc.sx = select i1 %100, float %110, float %lc.ex
  %lc.sy = select i1 %100, float %111, float %lc.ey
'''
    for old, new in [('%110', '%lc.sx'), ('%111', '%lc.sy')]:
        after, count = re.subn(re.escape(old) + r'(?![\w.])', new, after)
        if count != 9:
            raise ValueError('Unexpected scale use count: %s = %d' % (old, count))
    return before + anchor + block + after


def build(original):
    if ham.fnv1a(original) != KEY:
        raise ValueError('Unsupported light culler; expected %016x' % KEY)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = ham.assemble(eye_scale(ham.disassemble(original, work)), work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('DXIL validator output hash mismatch')
    return container


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    original = (args.shaders / ('%016x.dxil' % KEY)).read_bytes()
    container = build(original)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / ('%016x.dxil' % KEY)).write_bytes(container)
    print('Validated %016x: %d -> %d bytes' % (KEY, len(original), len(container)))


if __name__ == '__main__':
    main()
