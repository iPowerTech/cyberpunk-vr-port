"""Audit every 2.5 pixel / compute shader for the assumptions that break under engine stereo with per-eye
asymmetric windows (side-by-side frame, each eye drawn into its half with its own camera block).

Per shader it reports:
  frame_eye   a pixel position scaled by the WHOLE frame (b0 rows 2-4: render size / reciprocals) that then meets
              the eye's camera block (b1 matrix rows 0-35 or vector rows 39-50) -- the right eye's pixels land in
              x = 1..2 of an "ndc" made for one view (the bug the view_dir twins had, carsLighterInOneEye.rdc)
  view_size   reads b1 row 47 (the view's own pixel size)
  stereo      reads b1 row 53 (camera kind 0 centre / 1 left / 2 right) or b0 row 32 (two-halves flag)
  tangent     ndc * a lone constant (ndc_tangent_scan.scan): rebuilds view rays without the window's shift
  forward     b1 row 41 (camera forward) without row 36 (eye position): forward used as the view direction
  centre      a frame uv minus 0.5 (or ndc) that is squared / dotted: distance from the FRAME centre (vignette,
              lens effects) -- under side by side that centre is the seam between the eyes

  py -3.13 -B sbs_audit_scan.py <ll_all dir> <out.json>"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ndc_tangent_scan as nts

DEF = re.compile(r'^\s+(%[\w.]+) = (.*)$')
HANDLE = re.compile(r'call %dx\.types\.Handle @dx\.op\.createHandle\(i32 57, i8 2, i32 \d+, i32 (\d+), i1 false\)')
CBLOAD = re.compile(r'call %dx\.types\.CBufRet\.(f32|i32) @dx\.op\.cbufferLoadLegacy\.(?:f32|i32)\(i32 59, '
                    r'%dx\.types\.Handle (%\d+), i32 (\d+)\)')
EXTRACT = re.compile(r'^extractvalue %dx\.types\.CBufRet\.(?:f32|i32) (%\d+), (\d)')
TOKEN = re.compile(r'%[\w.]+')
POS_CS = ('@dx.op.threadId.', '@dx.op.groupId.', '@dx.op.threadIdInGroup.', '@dx.op.flattenedThreadIdInGroup.')
ARITH = re.compile(r'^(f?add|f?sub|f?mul|f?div|uitofp|sitofp|fptoui|fptosi|bitcast|zext|sext|trunc|shl|lshr|ashr|'
                   r'and|or|xor|select|phi|insertelement|extractelement|fneg|icmp|fcmp)\b')
DXOP_MATH = re.compile(r'@dx\.op\.(unary|binary|tertiary|dot2|dot3|dot4)\.')
FRAME_ROWS = {2, 3, 4}
EYE_ROWS = set(range(0, 36)) | set(range(39, 51))


def sv_position_id(ir):
    """Signature element id of SV_Position in a pixel shader's input signature table, or None."""
    m = re.search(r'; Input signature:\n;\n; Name[^\n]*\n; -+[^\n]*\n((?:; [^\n]+\n)+)', ir)
    if not m:
        return None
    for i, row in enumerate(m.group(1).splitlines()):
        if row.split()[1:2] == ['SV_Position']:
            return i
    return None


def scan(ir, kind):
    defs, order = {}, []
    for line in ir.splitlines():
        m = DEF.match(line)
        if m:
            defs[m.group(1)] = m.group(2).split(';')[0].strip()
            order.append(m.group(1))
    regs = {h: int(r) for h, v in defs.items() for r in [HANDLE.search(v)] if r for r in [r.group(1)]}
    loads = {}
    for k, v in defs.items():
        m = CBLOAD.search(v)
        if m and m.group(2) in regs:
            loads[k] = (regs[m.group(2)], int(m.group(3)))
    comps = {}
    for k, v in defs.items():
        m = EXTRACT.match(v)
        if m and m.group(1) in loads:
            comps[k] = (*loads[m.group(1)], int(m.group(2)))
    rows = {}
    for reg, row in loads.values():
        rows.setdefault(reg, set()).add(row)

    sv = sv_position_id(ir) if kind == 'ps' else None
    pos = set()
    for k in order:
        v = defs[k]
        if kind == 'ps' and sv is not None and re.search(r'@dx\.op\.loadInput\.f32\(i32 4, i32 %d, ' % sv, v):
            pos.add(k)
        elif kind == 'cs' and any(s in v for s in POS_CS):
            pos.add(k)
    # forward taint: P = from the pixel position, F = scaled by the whole frame, V = scaled by the view size
    P, F, V = set(pos), set(), set()
    frame_eye = set()
    centre = 0
    half = set()     # values that are (uv - 0.5) or an ndc made from a frame-scaled uv
    for k in order:
        v = defs[k]
        if k in pos:
            continue
        if not (ARITH.match(v) or DXOP_MATH.search(v)):
            continue
        ops = [o for o in TOKEN.findall(v) if o in defs and o != k]
        cb = [comps[o] for o in ops if o in comps]
        if any(o in P for o in ops):
            P.add(k)
            if any(r == 0 and row in FRAME_ROWS for r, row, _ in cb):
                F.add(k)
            if any(r == 1 and row == 47 for r, row, _ in cb):
                V.add(k)
        if any(o in F for o in ops):
            F.add(k)
            hit = [(row, c) for r, row, c in cb if r == 1 and row in EYE_ROWS]
            if hit and not any(o in V for o in ops):
                frame_eye |= set(hit)
            if re.match(r'^f(add|sub) (fast )?float', v) and ('-5.000000e-01' in v or ' 5.000000e-01' in v
                                                              or '-1.000000e+00' in v or ' 1.000000e+00' in v):
                half.add(k)
        if any(o in V for o in ops):
            V.add(k)
        if any(o in half for o in ops) and k not in half:
            if re.match(r'^fmul (fast )?float (%[\w.]+), (%[\w.]+)$', v):
                a, b = re.match(r'^fmul (fast )?float (%[\w.]+), (%[\w.]+)$', v).groups()[1:]
                if a == b:
                    centre += 1
            elif '@dx.op.dot2.' in v or '@dx.op.dot3.' in v:
                args = TOKEN.findall(v)
                if len(args) >= 2 and len(set(args)) < len(args):
                    centre += 1
    tangent = []
    try:
        hits, _ = nts.scan(ir)
        tangent = sorted({f'b{r}[{row}].{"xyzw"[c]}' for _, _, (r, row, c) in hits})
    except Exception:
        pass
    b1 = rows.get(1, set())
    b0 = rows.get(0, set())
    return {
        'pos': bool(pos),
        'frame_eye': sorted(f'{row}.{"xyzw"[c]}' for row, c in frame_eye),
        'view_size': 47 in b1,
        'stereo': sorted((['kind53'] if 53 in b1 else []) + (['halves32'] if 32 in b0 else [])),
        'tangent': tangent,
        'forward': 41 in b1 and 36 not in b1,
        'centre': centre,
        'b1_rows': sorted(b1),
    }


def main():
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    result = {}
    # ps / cs: engine\shader_final.cache, sps / scs: engine\staticshader_final.cache
    for kind in ('ps', 'cs', 'sps', 'scs'):
        for ll in sorted((root / kind).glob('*.ll')):
            try:
                result[f'{kind}/{ll.stem}'] = scan(ll.read_text(errors='replace'), kind[-2:])
            except Exception as e:  # keep going: one odd shader must not stop the census
                result[f'{kind}/{ll.stem}'] = {'error': repr(e)}
    out.write_text(json.dumps(result, indent=0), encoding='utf-8')
    count = lambda f: sum(1 for r in result.values() if f(r))
    print('shaders', len(result))
    print('frame_eye (no stereo signal)', count(lambda r: r.get('frame_eye') and not r['stereo'] and not r['view_size']))
    print('frame_eye (stereo aware)', count(lambda r: r.get('frame_eye') and (r['stereo'] or r['view_size'])))
    print('tangent', count(lambda r: r.get('tangent')))
    print('forward', count(lambda r: r.get('forward')))
    print('centre', count(lambda r: r.get('centre')))
    print('stereo aware', count(lambda r: r.get('stereo')))


if __name__ == '__main__':
    main()
