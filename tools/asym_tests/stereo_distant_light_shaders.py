"""Per-eye HiZ occlusion for the 2.5 DistantLight culler.

Native frustum/LOD culling and light animation remain unchanged. The stereo path
projects light volumes with SharedPixelConsts eye VP rows 93/113, reads each eye's
own HiZ rectangle, and retains a light visible to either eye. Mono bypasses the new
resource reads and keeps the native occlusion result. A one-base-texel margin
covers raster jitter/rounding; a volume crossing the eye plane is kept.

The native CS already binds b12: RenderDoc verified both matrices exactly match
those used by the subsequent light-volume raster draw. Shader FNV gates the ABI.
"""
import argparse
import itertools
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc
KEY = 0x6a1544708bf0067e


def eye_cull(ir):
    assert '!14 = !{!15, !16, !17}' in ir
    ir = ir.replace('%COM = type', '%cvr.SharedPixel = type { [117 x <4 x float>] }\n%COM = type')
    ir = ir.replace('!14 = !{!15, !16, !17}', '!14 = !{!15, !16, !17, !27}')
    ir += '\n!27 = !{i32 3, %cvr.SharedPixel* undef, !"cvr_shared", i32 0, i32 12, i32 1, i32 1872, null}\n'
    ir = ir.replace('define void @cs_main_() {', 'define void @cs_main_() {\n  %dc.shared = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 3, i32 12, i1 false)')
    lines = []
    def emit(name, expression):
        name = '%dc.'+name
        lines.append(f'  {name} = {expression}')
        return name
    def binary(name, op, a, b):
        return emit(name, f'call float @dx.op.binary.f32(i32 {op}, float {a}, float {b})')
    def unary(name, op, a):
        return emit(name, f'call float @dx.op.unary.f32(i32 {op}, float {a})')
    def row(name, handle, index, integer=False):
        typ = 'i32' if integer else 'f32'
        v = emit(name, f'call %dx.types.CBufRet.{typ} @dx.op.cbufferLoadLegacy.{typ}(i32 59, %dx.types.Handle {handle}, i32 {index})')
        return [emit(name+'.'+str(c), f'extractvalue %dx.types.CBufRet.{typ} {v}, {c}') for c in range(4)]
    def dot(name, a, b):
        return emit(name, 'call float @dx.op.dot4.f32(i32 56, '+', '.join('float '+v for v in a+b)+')')

    g = row('global', '%7', 32, True)
    stereo = emit('stereo', f'icmp ne i32 {g[0]}, 0')
    lines.append(f'  br i1 {stereo}, label %dc.stereo_path, label %dc.mono_path')
    lines.append('dc.mono_path:\n  br label %dc.join\n\ndc.stereo_path:')
    size = row('size', '%7', 2)
    half_px = emit('halfpx', f'fmul fast float {size[0]}, 5.000000e-01')
    half_tiles = emit('halftiles', f'fmul fast float {half_px}, 6.250000e-02')
    half_ceil = unary('halfceil', 28, half_tiles)
    stride_i = emit('stride0', f'fptoui float {half_ceil} to i32')
    stride_i = emit('stride1', f'add i32 {stride_i}, 1')
    stride_i = emit('stride', f'and i32 {stride_i}, -2')
    stride = emit('stridef', f'uitofp i32 {stride_i} to float')
    last_x = emit('lastx', f'fadd fast float {stride}, -1.000000e+00')
    height_tiles = emit('heighttiles', f'fmul fast float {size[1]}, 6.250000e-02')
    grid = row('grid', '%6', 52, True)
    last_y = emit('lasty', f'uitofp i32 {grid[1]} to float')

    # Native decoded light centre and radius; verified against the source FNV above.
    position = ['%38', '%39', '%44']
    radius = '%45'
    eyes = []
    for eye, first in [(0, 93), (1, 113)]:
        pre = 'e%d.' % eye
        matrix = [row(pre+'vp'+str(r), '%dc.shared', first+r) for r in [0, 1, 3]]
        clip_points = []
        for corner, signs in enumerate(itertools.product([-1, 1], repeat=3)):
            cp = pre+'c%d.' % corner
            point = [emit(cp+str(c), f'{"fadd" if sign>0 else "fsub"} fast float {position[c]}, {radius}') for c, sign in enumerate(signs)]+['1.000000e+00']
            clip = [dot(cp+'clip'+str(r), point, matrix[r]) for r in range(3)]
            safe_w = binary(cp+'safew', 35, clip[2], '1.000000e-04')
            clip_points.append([emit(cp+'ndc'+str(r), f'fdiv fast float {clip[r]}, {safe_w}') for r in range(2)]+[clip[2]])
        lo, hi = clip_points[0][:], clip_points[0][:]
        for c in range(1, 8):
            lo = [binary(pre+'min%d.%d'%(c,k), 36, lo[k], clip_points[c][k]) for k in range(3)]
            hi = [binary(pre+'max%d.%d'%(c,k), 35, hi[k], clip_points[c][k]) for k in range(3)]
        valid = []
        for k in range(2):
            valid += [emit(pre+'inlo'+str(k), f'fcmp fast oge float {hi[k]}, -1.000000e+00'), emit(pre+'inhi'+str(k), f'fcmp fast ole float {lo[k]}, 1.000000e+00')]
        overlap = valid[0]
        for k, v in enumerate(valid[1:]):overlap = emit(pre+'inside'+str(k), f'and i1 {overlap}, {v}')
        coords=[]
        # Pad by one base HiZ texel so jitter and raster rounding remain conservative.
        for name, ndc, scale, limit, inverse_y, upper in [
            ('x0',lo[0],half_tiles,last_x,False,False),('x1',hi[0],half_tiles,last_x,False,True),
            ('y0',hi[1],height_tiles,last_y,True,False),('y1',lo[1],height_tiles,last_y,True,True)]:
            uv=emit(pre+name+'.uv0', f'fmul fast float {ndc}, {"-5.000000e-01" if inverse_y else "5.000000e-01"}')
            uv=emit(pre+name+'.uv',f'fadd fast float {uv}, 5.000000e-01')
            px=emit(pre+name+'.px',f'fmul fast float {uv}, {scale}')
            px=emit(pre+name+'.pad',f'fadd fast float {px}, {"1.000000e+00" if upper else "-1.000000e+00"}')
            px=binary(pre+name+'.clo',35,px,'0.000000e+00');px=binary(pre+name+'.chi',36,px,limit)
            if eye and name.startswith('x'):px=emit(pre+name+'.offset',f'fadd fast float {px}, {stride}')
            coords.append(px)
        dx=emit(pre+'dx',f'fsub fast float {coords[1]}, {coords[0]}');dy=emit(pre+'dy',f'fsub fast float {coords[3]}, {coords[2]}')
        span=binary(pre+'span0',35,dx,dy);span=binary(pre+'span',35,span,'1.000000e+00')
        mip=unary(pre+'log',23,span);mip=unary(pre+'ceil',28,mip);mip=emit(pre+'mip',f'fptoui float {mip} to i32')
        texels=[]
        for k, v in enumerate(coords):
            v=emit(pre+'i'+str(k),f'fptoui float {v} to i32');texels.append(emit(pre+'m'+str(k),f'lshr i32 {v}, {mip}'))
        depths=[]
        for k,(x,y) in enumerate(itertools.product(texels[:2],texels[2:])):
            s=emit(pre+'sample'+str(k),f'call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %3, i32 {mip}, i32 {x}, i32 {y}, i32 undef, i32 undef, i32 undef, i32 undef)')
            z=emit(pre+'z'+str(k),f'extractvalue %dx.types.ResRet.f32 {s}, 1')
            sky=emit(pre+'sky'+str(k),f'fcmp fast olt float {z}, 0.000000e+00')
            depths.append(emit(pre+'depth'+str(k),f'select i1 {sky}, float 1.600000e+04, float {z}'))
        max_z=depths[0]
        for k,z in enumerate(depths[1:]):max_z=binary(pre+'far'+str(k),35,max_z,z)
        visible=emit(pre+'visible',f'fcmp fast olt float %299, {max_z}')
        visible=emit(pre+'visibleinside',f'and i1 {visible}, {overlap}')
        crossing=emit(pre+'crossing',f'fcmp fast ole float {lo[2]}, 1.000000e-04')
        eyes.append(emit(pre+'keep',f'or i1 {visible}, {crossing}'))
    any_eye=emit('either',f'or i1 {eyes[0]}, {eyes[1]}')
    lines.append('  br label %dc.join\n\ndc.join:')
    lines.append(f'  %300 = phi i1 [ %dc.native, %dc.mono_path ], [ {any_eye}, %dc.stereo_path ]')
    anchor='  %300 = fcmp fast olt float %299, %298\n'
    assert ir.count(anchor)==1
    ir=ir.replace(anchor,'  %dc.native = fcmp fast olt float %299, %298\n'+'\n'.join(lines)+'\n')
    ir=ir.replace('1.000000e-04','0x3F1A36E2E0000000')
    return ir


def build(original):
    if ham.fnv1a(original) != KEY:
        raise ValueError('Unsupported distant-light shader')
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        result = ham.assemble(eye_cull(ham.disassemble(original, work)), work)
    if dc.dxil_hash(result) != result[4:20]:
        raise RuntimeError('DXIL hash mismatch')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = build((args.shaders/('%016x.dxil'%KEY)).read_bytes())
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/('%016x.dxil'%KEY)).write_bytes(result)
    print('Validated %016x: %d bytes'%(KEY,len(result)))


if __name__ == '__main__':
    main()
