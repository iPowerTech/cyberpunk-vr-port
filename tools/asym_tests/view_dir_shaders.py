"""Material pixel shaders that take the camera's forward axis for the direction to the viewer, made to take the
pixel's own view ray instead (docs: memory cpvr-view-dir).

Screen and hologram materials fake depth with copies of their picture shifted along the surface by the view
direction, and some lit materials shade with it -- but they read the camera's forward axis (b1 row 41) for that
direction, one value for the whole screen:

    uv1 = uv + k * (dot(forward, T), dot(forward, B))        (T, B the surface's tangent frame)

On a monitor that is invisible. In a headset the camera is the head, so turning the head slides the shifted
copies over the surface -- "the picture in the holographic billboards moves when I turn my head" -- and the
picture moves against the surface's own motion vectors, which the upscaler smears.

Each twin computes the pixel's own ray and uses it wherever the shader used the forward axis:

    ndc = (2 x / W - 1, 1 - 2 y / H)                         x, y = SV_Position; W, H = b0 row 3.xy
    A = M (ndc.x, ndc.y, 0, 1),  C = M[:, 2]                 M = inverse(world->clip) = b1 rows 4-7
    d = A.xyz C.w - C.xyz A.w                                the far point minus the near point of the ray
    v = d / |d|, turned to the forward side                  (the far point may be at infinity: A.w = 0)

At the centre of the picture v is the forward axis, so a shader looking straight at its surface draws what the
engine draws; elsewhere it draws what that pixel sees -- the same for every head orientation. Only shaders that
read the forward axis WITHOUT the camera position (row 36) are taken: with the position the forward axis may be
a depth axis (a view depth to compare with the depth buffer), which must stay. Of those, the ones whose forward
axis is a direction to the viewer: two or more dot products with it (the parallax layers, possibly next to a
facing term), or its negation (a direction to the viewer for lighting or a facing term). A single dot product
alone may be a view depth of a camera-relative position and is left alone.

Shaders come from the game's engine\\shader_final.cache (extract_shader_cache.py --kind ps); output:
<FNV-1a>.dxil signed containers for `view_dir\\` (Render/HiddenAreaShaders.cpp, CyberpunkVR_ViewDirPatch).

  py -3.13 -B view_dir_shaders.py --shaders DIR_OF_PS --out OUT_DIR [--only HASH ...]"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

GLOBAL, ROW_SCREEN = 0, 3            # b0: the global block, row 3 .xy = render width, height
CAMERA, ROW_INVERSE, ROW_EYE, ROW_FORWARD = 1, 4, 36, 41   # b1: the camera block (Render/StereoShaderCamera.hpp)
DEF = re.compile(r'(?m)^  (%[\w.]+) = (.*)$')


class Skip(Exception):
    pass


def handle(ir, register):
    found = re.findall(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 '
                       + str(register) + r', i1 false\)', ir)
    if len(found) != 1:
        raise Skip('cb%d handles: %d' % (register, len(found)))
    return found[0]


def loads(ir, h, row):
    return re.findall(r'(?m)^  (%\d+) = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, '
                      r'%dx\.types\.Handle ' + re.escape(h) + r', i32 ' + str(row) + r'\)', ir)


def token(v):
    return re.compile(re.escape(v) + r'(?![\w.])')


def classify(ir, defs, comps):
    """'parallax' (two or more dot products with the axis), 'facing' (its negation), or None."""
    dots = negations = 0
    for c in comps:
        t = token(c)
        for k, v in defs.items():
            if k == c or not t.search(v.split(';')[0]):
                continue
            if v.startswith('call float @dx.op.dot3.f32'):
                dots += 1
            elif re.fullmatch(r'fsub fast float -0\.000000e\+00, ' + re.escape(c) + r'(\s*;.*)?', v):
                negations += 1
    if dots >= 6:
        return 'parallax'
    if negations and dots % 3 == 0 and dots <= 3 and negations >= 2:
        return 'facing'
    if negations >= 3:
        return 'facing'
    return None


def view_ray(q, b0, b1, sv, forward):
    """Lines computing the pixel's unit view ray %{q}v0..2 (forward side)."""
    L = []
    a = L.append
    for k, ax in enumerate('xyz'):
        a(f'  {q}f{ax} = extractvalue %dx.types.CBufRet.f32 {forward}, {k}')
    a(f'  {q}px = call float @dx.op.loadInput.f32(i32 4, i32 {sv}, i32 0, i8 0, i32 undef)')
    a(f'  {q}py = call float @dx.op.loadInput.f32(i32 4, i32 {sv}, i32 0, i8 1, i32 undef)')
    a(f'  {q}scr = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b0}, i32 {ROW_SCREEN})')
    a(f'  {q}sw = extractvalue %dx.types.CBufRet.f32 {q}scr, 0')
    a(f'  {q}sh = extractvalue %dx.types.CBufRet.f32 {q}scr, 1')
    a(f'  {q}u = fdiv fast float {q}px, {q}sw')
    a(f'  {q}v = fdiv fast float {q}py, {q}sh')
    a(f'  {q}nx0 = fmul fast float {q}u, 2.000000e+00')
    a(f'  {q}nx = fadd fast float {q}nx0, -1.000000e+00')
    a(f'  {q}ny0 = fmul fast float {q}v, -2.000000e+00')
    a(f'  {q}ny = fadd fast float {q}ny0, 1.000000e+00')
    A, C = [], []
    for k in range(4):
        a(f'  {q}r{k} = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b1}, i32 {ROW_INVERSE + k})')
        for c, n in enumerate('xyzw'):
            a(f'  {q}r{k}{n} = extractvalue %dx.types.CBufRet.f32 {q}r{k}, {c}')
        a(f'  {q}a{k}0 = fmul fast float {q}r{k}x, {q}nx')
        a(f'  {q}a{k}1 = fmul fast float {q}r{k}y, {q}ny')
        a(f'  {q}a{k}2 = fadd fast float {q}a{k}0, {q}a{k}1')
        a(f'  {q}a{k} = fadd fast float {q}a{k}2, {q}r{k}w')
        A.append(f'{q}a{k}'); C.append(f'{q}r{k}z')
    for k in range(3):
        a(f'  {q}d{k}0 = fmul fast float {A[k]}, {C[3]}')
        a(f'  {q}d{k}1 = fmul fast float {C[k]}, {A[3]}')
        a(f'  {q}d{k} = fsub fast float {q}d{k}0, {q}d{k}1')
    a(f'  {q}dd = call float @dx.op.dot3.f32(i32 55, float {q}d0, float {q}d1, float {q}d2, float {q}d0, float {q}d1, float {q}d2)')
    a(f'  {q}df = call float @dx.op.dot3.f32(i32 55, float {q}d0, float {q}d1, float {q}d2, float {q}fx, float {q}fy, float {q}fz)')
    a(f'  {q}ok = fcmp fast ogt float {q}dd, 0x3BC79CA100000000')
    a(f'  {q}dds = select i1 {q}ok, float {q}dd, float 1.000000e+00')
    a(f'  {q}rl = call float @dx.op.unary.f32(i32 25, float {q}dds)')
    a(f'  {q}back = fcmp fast olt float {q}df, 0.000000e+00')
    a(f'  {q}nrl = fsub fast float -0.000000e+00, {q}rl')
    a(f'  {q}s = select i1 {q}back, float {q}nrl, float {q}rl')
    for k, ax in enumerate('xyz'):
        a(f'  {q}m{k} = fmul fast float {q}d{k}, {q}s')
        # a degenerate ray (never seen) keeps the forward axis
        a(f'  {q}v{k} = select i1 {q}ok, float {q}m{k}, float {q}f{ax}')
    return L


def patch(ir):
    """Returns (ir, kind, number of forward loads rewritten)."""
    b0, b1 = handle(ir, GLOBAL), handle(ir, CAMERA)
    if loads(ir, b1, ROW_EYE):
        raise Skip('reads the camera position')
    forward = loads(ir, b1, ROW_FORWARD)
    if not forward:
        raise Skip('no forward axis')
    m = re.search(r'!\{i32 (\d+), !"SV_Position", i8 9, i8 3,', ir)
    if not m:
        raise Skip('no SV_Position input')
    sv = int(m.group(1))
    if 'declare float @dx.op.loadInput.f32(' not in ir or 'declare float @dx.op.dot3.f32(' not in ir:
        raise Skip('missing declarations')
    if 'declare float @dx.op.unary.f32(' not in ir:
        ir = ir.replace('declare float @dx.op.dot3.f32(', 'declare float @dx.op.unary.f32(i32, float) #0\n\n'
                        'declare float @dx.op.dot3.f32(', 1)
    defs = {k: v for k, v in DEF.findall(ir)}
    kinds = set()
    for j, L in enumerate(forward):
        comps = {}
        for k, v in defs.items():
            mm = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 ' + re.escape(L) + r', ([0-2])(\s*;.*)?', v)
            if mm:
                comps[k] = int(mm.group(1))
        kind = classify(ir, defs, comps)
        if not kind:
            raise Skip('forward axis use is not a view direction')
        kinds.add(kind)
        q = '%%vd%d.' % j
        line = re.compile(r'(?m)^  ' + re.escape(L) + r' = call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(.*\n')
        hit = line.search(ir)
        insert = '\n'.join(view_ray(q, b0, b1, sv, L)) + '\n'
        head, tail = ir[:hit.end()], ir[hit.end():]
        for c, k in comps.items():
            # every use (not the definition) of the component moves to the ray's component
            pat = token(c)
            out = []
            for ln in tail.splitlines(keepends=True):
                if ln.startswith('  ' + c + ' = '):
                    out.append(ln)
                    continue
                code, sep, comment = ln.partition(';') if ln.lstrip().startswith('%') or ln.startswith('  ') else (ln, '', '')
                out.append(pat.sub(q + 'v%d' % k, code) + sep + comment)
            tail = ''.join(out)
        ir = head + insert + tail
    return ir, '+'.join(sorted(kinds)), len(forward)


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, kind, count = patch(ham.disassemble(original, work))
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, kind, count


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--shaders', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--only', nargs='*')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    names = args.only or sorted(p.stem for p in args.shaders.glob('*.dxil'))
    done = failed = 0
    for name in names:
        original = (args.shaders / (name + '.dxil')).read_bytes()
        try:
            container, kind, count = build(original)
        except Skip as e:
            if args.only:
                print('SKIP %s: %s' % (name, e))
            continue
        except Exception as e:
            failed += 1
            print('FAIL %s: %s' % (name, str(e)[-600:]))
            continue
        key = ham.fnv1a(original)
        if '%016x' % key != name.lower():
            print('note: %s hashes to %016x' % (name, key))
        (args.out / ('%016x.dxil' % key)).write_bytes(container)
        done += 1
        print('OK %s %s: %d forward load(s), %d -> %d bytes' % (name, kind, count, len(original), len(container)))
    print('built %d, failed %d' % (done, failed))


if __name__ == '__main__':
    main()
