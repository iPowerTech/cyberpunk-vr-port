"""Camera-facing particle sprites turned to face the eye, not the screen (docs: memory cpvr-particle-facing).

The engine's billboard particles (vertex factory MVF_ParticleBilboard and its kin) expand every sprite in the
plane of the screen: the quad's corner, already turned by the particle's own rotation, is laid out along the
camera's right and up axes from the camera block (b1 rows 40 and 42), and the sprite's normal is minus the
camera's forward axis (row 41):

    corner = centre + right * a + up * b            normal = -forward

On a monitor that is invisible. In a headset the camera is the head, so every sprite tilts with a tilted head
and swings with a turned or raised one -- smoke and steam "turn with the head".

Each recipe builds the sprite's axes from the eye and the particle instead -- what a sprite looks like from
where the eye stands, whatever way the head points:

    f  = normalize(centre - eye)                    eye = b1 row 36.xyz (camera position, world)
    up = normalize(Z - f * f.z)                     world up projected off f (camera up near the poles)
    right = s * cross(up, f)                        s = sign(dot(cross(right0, up0), forward0)): the camera
                                                    basis' own handedness, so the quad is not mirrored
    normal = -f

For a sprite straight ahead of an unrolled camera these are exactly the camera's axes, so the picture at rest
is the engine's. The centre is found as the term the right-axis offset is added to; every later use of the
camera's right and up (a tangent frame for lighting) moves onto the new axes with the corner.

Sprites that already face the eye (vertical, sphere-aligned, beams) or lie in the world (trails, ribbons) still
take minus the forward axis as their normal and as their push towards the viewer; those negations are made from
f = normalize(X - eye) instead, X the point the shader itself measures from the eye (its earliest one: the
centre before the corners) or, in a shader that never reads the eye, the world position it projects. The
forward axis used as itself (the behind-the-camera test) is left alone. Shaders that write clip space directly
(screen particles) and world-oriented ones without any camera axis are not touched.

Shaders come from the game's engine\\shader_final.cache (extract_shader_cache.py --kind vs); output:
<FNV-1a>.dxil signed containers for `asym_particles\\` (Render/HiddenAreaShaders.cpp, CyberpunkVR_ParticleFacing).

  py -3.13 -B particle_facing_shaders.py --shaders DIR_OF_VS --out OUT_DIR [--only HASH ...]

Without --only, only particle vertex shaders are tried (input signature: COLOR, no NORMAL/TANGENT/INSTANCE/
BLEND/EXTRA)."""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

CAMERA = 1                         # b1, the camera block (Render/StereoShaderCamera.hpp)
ROW_EYE, ROW_RIGHT, ROW_FORWARD, ROW_UP = 36, 40, 41, 42
DEF = re.compile(r'(?m)^  (%\d+) = (.*)$')
HANDLE = re.compile(r'(?m)^  (%\d+) = call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')


def lit(v):
    v, = struct.unpack('<f', struct.pack('<f', v))
    return '0x%016X' % struct.unpack('<Q', struct.pack('<d', v))[0]


class Skip(Exception):
    pass


def axis_components(defs, handle, row):
    loads = [k for k, v in defs.items() if re.fullmatch(r'call %dx\.types\.CBufRet\.f32 @dx\.op\.cbufferLoadLegacy\.f32\(i32 59, %dx\.types\.Handle '
                                                        + re.escape(handle) + r', i32 %d\)\s*(;.*)?' % row, v)]
    comps = {}
    for k, v in defs.items():
        m = re.fullmatch(r'extractvalue %dx\.types\.CBufRet\.f32 (%\d+), ([0-3])', v)
        if m and m.group(1) in loads:
            comps.setdefault(int(m.group(2)), []).append(k)
    return comps


def operands(v):
    m = re.fullmatch(r'(fadd|fmul|fsub) fast float (%\d+|[-0-9.e+x]+), (%\d+|[-0-9.e+x]+)', v)
    return (m.group(1), m.group(2), m.group(3)) if m else (None, None, None)


def use_map(defs):
    users = {}
    for k, v in defs.items():
        for o in re.findall(r'%\d+', v.split(';')[0]):
            users.setdefault(o, []).append(k)
    return users


def token(v):
    return re.compile(re.escape(v) + r'(?![\w.])')


def turn_forward(ir, defs, users, order, F, point, q):
    """-forward -> -f: every negation of the camera's forward axis (the sprite's normal, the push towards the
    viewer) is made from f instead. One that comes before `point` (f does not exist there yet) gets a copy
    made from f just after f's block when nothing uses it before then (stores included); the later uses move
    to the copy and the original is left dead (unnamed values must stay numbered in order). The forward axis
    used as itself (the behind-the-camera test) is left alone. Returns (ir, lines for after the block,
    renames for the text after the block, count)."""
    tail, renames, count = [], {}, 0
    point_end = re.search(r'(?m)^  ' + re.escape(point) + r' = .*\n', ir).end()
    for c, ax in enumerate('xyz'):
        for g in F.get(c, []):
            for k in users.get(g, []):
                if not re.fullmatch(r'fsub fast float -0\.000000e\+00, ' + re.escape(g), defs[k]):
                    continue
                line = re.compile(r'(?m)^  ' + re.escape(k) + r' = fsub fast float -0\.000000e\+00, ' + re.escape(g) + r'\n')
                if order[k] > order[point]:
                    ir, n = line.subn(lambda _m: f'  {k} = fsub fast float -0.000000e+00, {q}f{ax}\n', ir, count=1)
                    count += n
                    continue
                m = line.search(ir)
                if m and not token(k).search(ir, m.end(), point_end):
                    copy = q + 'm' + k[1:]
                    tail.append(f'  {copy} = fsub fast float -0.000000e+00, {q}f{ax}\n')
                    renames[k] = copy
                    count += 1
    return ir, ''.join(tail), renames, count


def insert(ir, point, block, tail, renames):
    """The block and the copies right after `point`; the renamed values swapped in everywhere after it."""
    m = re.search(r'(?m)^  ' + re.escape(point) + r' = .*\n', ir)
    after = ir[m.end():]
    for old, new in renames.items():
        after = token(old).sub(new, after)
    return ir[:m.end()] + block + tail + after


FMAD = re.compile(r'call float @dx\.op\.tertiary\.f32\(i32 46, float (%\d+), float (%\d+), float (%\d+)\)\s*(;.*)?')


def eye_point(defs, users, order, E):
    """X such that the shader measures X from the eye on every axis (fsub eye, X or fsub X, eye); the earliest
    when there are several (a sprite's centre, then its corner)."""
    X = {}
    for c in range(3):
        found = set()
        for e in E[c]:
            for k in users.get(e, []):
                o, a, b = operands(defs[k])
                if o == 'fsub' and a == e and b.startswith('%'):
                    found.add(b)
                elif o == 'fsub' and b == e and a.startswith('%'):
                    found.add(a)
        found = [k for k in found if k in order]
        if not found:
            raise Skip('axis %d: no point measured from the eye' % c)
        X[c] = min(found, key=lambda k: order[k])     # the sprite's own point comes before its corners
    return X


def projected_point(defs, users, V):
    """The world position the shader projects: x*m0 (fmul), then fmad(y, m1, .), fmad(z, m2, .) with m the
    components of b1 row 0."""
    def step(prev, row):
        out = []
        for u in users.get(prev, []):
            m = FMAD.fullmatch(defs[u])
            if m and m.group(3) == prev:
                a, b = m.group(1), m.group(2)
                if b in V[row]:
                    out.append((u, a))
                elif a in V[row]:
                    out.append((u, b))
        return out
    found = set()
    for m0 in V[0]:
        for k in users.get(m0, []):
            o, a, b = operands(defs[k])
            if o != 'fmul':
                continue
            x = b if a == m0 else a
            for u1, y in step(k, 1):
                for _, z in step(u1, 2):
                    found.add((x, y, z))
    if len(found) != 1:
        raise Skip('no eye; %d projected positions' % len(found))
    return dict(enumerate(found.pop()))


def patch_facing(ir, b1, defs, order, F):
    """Sprites that already face the eye (vertical, sphere-aligned, beams) or lie in the world (trails, ribbons)
    but take their normal or their push towards the viewer from the camera's forward axis: that becomes
    f = normalize(X - eye), X the point the shader itself measures from the eye (fsub eye, X on every axis)
    or, in a shader that never reads the eye, the world position it projects."""
    if not all(c in F for c in range(3)):
        raise Skip('no forward')
    users = use_map(defs)
    E = axis_components(defs, b1, ROW_EYE)
    if all(c in E for c in range(3)):
        X = eye_point(defs, users, order, E)
    else:
        V = axis_components(defs, b1, 0)
        if not all(c in V for c in range(3)):
            raise Skip('no eye and no projection')
        X = projected_point(defs, users, V)
    if not all(X[c] in order for c in range(3)):
        raise Skip('point is not a computed value')
    point = max((X[c] for c in range(3)), key=lambda k: order[k])
    q = '%pf.'
    L = [f'  {q}le = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b1}, i32 {ROW_EYE})']
    for i, ax in enumerate('xyz'):
        L.append(f'  {q}e{ax} = extractvalue %dx.types.CBufRet.f32 {q}le, {i}')
        L.append(f'  {q}d{ax} = fsub fast float {X[i]}, {q}e{ax}')
    L.append(f'  {q}dd = call float @dx.op.dot3.f32(i32 55, float {q}dx, float {q}dy, float {q}dz, float {q}dx, float {q}dy, float {q}dz)')
    L.append(f'  {q}ddm = fadd fast float {q}dd, {lit(1e-12)}')
    L.append(f'  {q}di = call float @dx.op.unary.f32(i32 25, float {q}ddm)')
    for ax in 'xyz':
        L.append(f'  {q}f{ax} = fmul fast float {q}d{ax}, {q}di')
    ir, tail, renames, count = turn_forward(ir, defs, users, order, F, point, q)
    if not count:
        raise Skip('forward axis not negated')
    ir = insert(ir, point, '\n'.join(L) + '\n', tail, renames)
    return ir, count


def patch(ir):
    handles = [h for h, r in HANDLE.findall(ir) if int(r) == CAMERA]
    if len(handles) != 1:
        raise Skip('no single b1 handle')
    b1 = handles[0]
    defs = dict(DEF.findall(ir))
    order = {k: i for i, k in enumerate(DEF.findall(ir) and [m.group(1) for m in DEF.finditer(ir)])}
    R, U, F = (axis_components(defs, b1, r) for r in (ROW_RIGHT, ROW_UP, ROW_FORWARD))
    if not any(c in R for c in range(3)) and not any(c in U for c in range(3)):
        return patch_facing(ir, b1, defs, order, F)
    if not all(c in R for c in range(3)) or not all(c in U for c in range(3)):
        raise Skip('partial right/up axes')
    users = use_map(defs)
    # the expansion: per axis c, fmul(R[c], a) and fmul(U[c], b) summed onto the centre. The axes may be
    # used more often (a tangent frame for lighting); the corner is the pair that lands on one point.
    def other(sum_, term):
        op, a, b = operands(defs[sum_])
        return b if a == term else a
    centre = {}
    for c in range(3):
        rm = [k for r in R[c] for k in users.get(r, []) if operands(defs[k])[0] == 'fmul']
        um = [k for u in U[c] for k in users.get(u, []) if operands(defs[k])[0] == 'fmul']
        found = set()
        for rt in rm:
            for ut in um:
                for a in (k for k in users.get(rt, []) if operands(defs[k])[0] == 'fadd'):
                    for b in (k for k in users.get(ut, []) if operands(defs[k])[0] == 'fadd'):
                        if other(b, ut) == a:          # (centre + rt) + ut
                            found.add(other(a, rt))
                        elif other(a, rt) == b:        # (centre + ut) + rt
                            found.add(other(b, ut))
        if len(found) != 1:
            raise Skip('axis %d: %d centres (%d right / %d up products)' % (c, len(found), len(rm), len(um)))
        centre[c] = found.pop()
    # the new axes go right after the centre is complete; every use of the old axes after that is moved
    # onto them, and none may come earlier
    for c in range(3):
        if centre[c] not in order:
            raise Skip('centre is not a computed value')
    last_centre = max((centre[c] for c in range(3)), key=lambda k: order[k])
    axis_uses = [k for c in range(3) for v in R[c] + U[c] for k in users.get(v, [])]
    if min(order[k] for k in axis_uses) < order[last_centre]:
        raise Skip('axes used before the centre')
    q = '%pf.'
    L = []
    ld = lambda name, row: L.append(f'  {q}{name} = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle {b1}, i32 {row})')
    ex = lambda name, src, c: L.append(f'  {q}{name} = extractvalue %dx.types.CBufRet.f32 {q}{src}, {c}')
    op = lambda name, o, a, b: L.append(f'  {q}{name} = {o} fast float {a}, {b}')
    ld('le', ROW_EYE); ld('lr', ROW_RIGHT); ld('lu', ROW_UP); ld('lf', ROW_FORWARD)
    for i, ax in enumerate('xyz'):
        ex('e' + ax, 'le', i); ex('r' + ax, 'lr', i); ex('u' + ax, 'lu', i); ex('g' + ax, 'lf', i)
    # f = normalize(centre - eye)
    for i, ax in enumerate('xyz'):
        op('d' + ax, 'fsub', centre[i], q + 'e' + ax)
    L.append(f'  {q}dd = call float @dx.op.dot3.f32(i32 55, float {q}dx, float {q}dy, float {q}dz, float {q}dx, float {q}dy, float {q}dz)')
    op('ddm', 'fadd', q + 'dd', lit(1e-12))
    L.append(f'  {q}di = call float @dx.op.unary.f32(i32 25, float {q}ddm)')
    for ax in 'xyz':
        op('f' + ax, 'fmul', q + 'd' + ax, q + 'di')
    # up = Z - f * f.z, or camera up off f near the poles
    op('zx', 'fmul', q + 'fx', q + 'fz'); op('zy', 'fmul', q + 'fy', q + 'fz'); op('zz', 'fmul', q + 'fz', q + 'fz')
    op('wx', 'fsub', '-0.000000e+00', q + 'zx'); op('wy', 'fsub', '-0.000000e+00', q + 'zy'); op('wz', 'fsub', '1.000000e+00', q + 'zz')
    L.append(f'  {q}cu = call float @dx.op.dot3.f32(i32 55, float {q}fx, float {q}fy, float {q}fz, float {q}ux, float {q}uy, float {q}uz)')
    for ax in 'xyz':
        op('k' + ax, 'fmul', q + 'f' + ax, q + 'cu'); op('v' + ax, 'fsub', q + 'u' + ax, q + 'k' + ax)
    L.append(f'  {q}ww = call float @dx.op.dot3.f32(i32 55, float {q}wx, float {q}wy, float {q}wz, float {q}wx, float {q}wy, float {q}wz)')
    L.append(f'  {q}pole = fcmp fast olt float {q}ww, {lit(1e-4)}')
    for ax in 'xyz':
        L.append(f'  {q}h{ax} = select i1 {q}pole, float {q}v{ax}, float {q}w{ax}')
    L.append(f'  {q}hh = call float @dx.op.dot3.f32(i32 55, float {q}hx, float {q}hy, float {q}hz, float {q}hx, float {q}hy, float {q}hz)')
    op('hhm', 'fadd', q + 'hh', lit(1e-12))
    L.append(f'  {q}hi = call float @dx.op.unary.f32(i32 25, float {q}hhm)')
    for ax in 'xyz':
        op('n' + ax, 'fmul', q + 'h' + ax, q + 'hi')          # the new up
    # handedness of the camera's own basis: s = sign(dot(cross(right, up), forward))
    op('c1', 'fmul', q + 'ry', q + 'uz'); op('c2', 'fmul', q + 'rz', q + 'uy'); op('cx', 'fsub', q + 'c1', q + 'c2')
    op('c3', 'fmul', q + 'rz', q + 'ux'); op('c4', 'fmul', q + 'rx', q + 'uz'); op('cy', 'fsub', q + 'c3', q + 'c4')
    op('c5', 'fmul', q + 'rx', q + 'uy'); op('c6', 'fmul', q + 'ry', q + 'ux'); op('cz', 'fsub', q + 'c5', q + 'c6')
    L.append(f'  {q}hs = call float @dx.op.dot3.f32(i32 55, float {q}cx, float {q}cy, float {q}cz, float {q}gx, float {q}gy, float {q}gz)')
    L.append(f'  {q}neg = fcmp fast olt float {q}hs, 0.000000e+00')
    L.append(f'  {q}s = select i1 {q}neg, float -1.000000e+00, float 1.000000e+00')
    # right = s * cross(up, f)
    op('a1', 'fmul', q + 'ny', q + 'fz'); op('a2', 'fmul', q + 'nz', q + 'fy'); op('ax0', 'fsub', q + 'a1', q + 'a2')
    op('a3', 'fmul', q + 'nz', q + 'fx'); op('a4', 'fmul', q + 'nx', q + 'fz'); op('ay0', 'fsub', q + 'a3', q + 'a4')
    op('a5', 'fmul', q + 'nx', q + 'fy'); op('a6', 'fmul', q + 'ny', q + 'fx'); op('az0', 'fsub', q + 'a5', q + 'a6')
    for ax in 'xyz':
        op('t' + ax, 'fmul', q + 'a' + ax + '0', q + 's')         # the new right
    block = '\n'.join(L) + '\n'
    # every use of the old axes onto the new ones
    swap = {}
    for c, ax in enumerate('xyz'):
        for v in R[c]:
            swap[v] = q + 't' + ax
        for v in U[c]:
            swap[v] = q + 'n' + ax
    for k in sorted(set(axis_uses), key=lambda k: order[k]):
        line = re.search(r'(?m)^  ' + re.escape(k) + r' = (.*)$', ir)
        body, _, comment = line.group(1).partition(';')
        new = re.sub(r'%\d+(?!\d)', lambda m: swap.get(m.group(0), m.group(0)), body)
        ir = ir[:line.start(1)] + new + (';' + comment if comment else '') + ir[line.end(1):]
    ir, tail, renames, normals = turn_forward(ir, defs, users, order, F, last_centre, q)
    ir = insert(ir, last_centre, block, tail, renames)
    return ir, normals


DECLS = {
    '@dx.op.unary.f32': 'declare float @dx.op.unary.f32(i32, float)',
    '@dx.op.dot3.f32': 'declare float @dx.op.dot3.f32(i32, float, float, float, float, float, float)',
}


def declare(ir):
    """Declarations for the dx.op functions the recipe calls that the module did not use itself."""
    missing = [n for n in DECLS if ('declare float ' + n + '(') not in ir]
    if not missing:
        return ir
    groups = re.findall(r'(?m)^attributes #(\d+) = \{ nounwind readnone \}$', ir)
    if groups:
        group, extra = groups[0], ''
    else:
        group = str(max([int(g) for g in re.findall(r'(?m)^attributes #(\d+) =', ir)] or [-1]) + 1)
        extra = '\nattributes #%s = { nounwind readnone }\n' % group
    text = '\n'.join('%s #%s' % (DECLS[n], group) for n in missing) + '\n'
    m = re.search(r'(?m)^declare ', ir)
    ir = ir[:m.start()] + text + ir[m.start():]
    if extra:
        ir += extra
    return ir


def build(original):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        ir, normals = patch(ham.disassemble(original, work))
        ir = declare(ir)
        container = ham.assemble(ir, work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container, normals


def input_semantics(container):
    """The vertex shader's input signature (ISG1) as SEMANTICn names."""
    count, = struct.unpack_from('<I', container, 28)
    for i in range(count):
        off, = struct.unpack_from('<I', container, 32 + 4 * i)
        if container[off:off + 4] == b'ISG1':
            base = off + 8
            n, = struct.unpack_from('<I', container, base)
            out = []
            for e in range(n):
                _, name, index = struct.unpack_from('<III', container, base + 8 + e * 32)
                out.append(container[base + name:container.index(b'\0', base + name)].decode() + str(index))
            return out
    return []


def particle_inputs(container):
    """Particle vertex factories feed COLOR and none of a mesh's NORMAL/TANGENT, skinning or instance streams."""
    sig = input_semantics(container)
    return any(s.startswith('COLOR') for s in sig) and not any(s.startswith(('NORMAL', 'TANGENT', 'INSTANCE', 'BLEND', 'EXTRA')) for s in sig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--shaders', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--only', nargs='*')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    files = [args.shaders / (h + '.dxil') for h in args.only] if args.only else sorted(args.shaders.glob('*.dxil'))
    ok = skipped = failed = 0
    for p in files:
        original = p.read_bytes()
        if ham.fnv1a(original) != int(p.stem, 16):
            raise RuntimeError('%s does not hash to its name' % p)
        if not args.only and not particle_inputs(original):
            continue
        try:
            container, normals = build(original)
        except Skip as why:
            skipped += 1
            print('SKIP', p.stem, why)
            continue
        except Exception as error:
            failed += 1
            print('FAILED', p.stem, str(error)[:300])
            continue
        (args.out / p.name).write_bytes(container)
        ok += 1
        print('OK', p.stem, 'normal components', normals, len(original), '->', len(container))
    print('twins', ok, 'skipped', skipped, 'failed', failed)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
