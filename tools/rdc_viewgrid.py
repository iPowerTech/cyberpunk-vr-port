# -*- coding: utf-8 -*-
"""Ratio grid between the two views at the same named stage, finding everything itself.

The predecessor (rdc_stagemap.py) needed the event ids and the target resource typed in by hand, which
meant one replay to list the passes and another to read them. On a 3.6 GB capture that is minutes of
loading and gigabytes of RAM twice over, and the machine has already been pushed into swap once. This
takes a NODE NAME, finds each view's last action under that marker, takes the render target from the
action itself, and prints the grid -- one replay, nothing typed twice.

Read it as: ~1.00 everywhere means the views agree; a block well above or below 1 is a real difference;
the frame edges always disagree because of parallax and that is not a defect.

usage:
    rdc_viewgrid.py <capture.rdc> [node-name] [grid] [step]
    default node is RenderFogOverlay -- the last pass before the per-view post chain diverges
"""
import os, sys, struct, collections

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
NODE = sys.argv[2] if len(sys.argv) > 2 else "RenderFogOverlay"
GRID = int(sys.argv[3]) if len(sys.argv) > 3 else 8
STEP = int(sys.argv[4]) if len(sys.argv) > 4 else 8

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402

NULL = rd.ResourceId.Null()


def res_of(d):
    for n in ("resource", "resourceId"):
        if hasattr(d, n):
            return getattr(d, n)
    return None


def walk(actions, out):
    for a in actions:
        out.append(a)
        if len(a.children):
            walk(a.children, out)
    return out


def r11g11b10(u):
    out = []
    for shift, mbits in ((0, 6), (11, 6), (22, 5)):
        v = (u >> shift) & ((1 << (mbits + 5)) - 1)
        e = v >> mbits
        m = v & ((1 << mbits) - 1)
        if e == 0:
            f = m / float(1 << mbits) * (2.0 ** -14)
        elif e == 31:
            f = 0.0
        else:
            f = (1.0 + m / float(1 << mbits)) * (2.0 ** (e - 15))
        out.append(f)
    return out


def half(h):
    e, m, s = (h >> 10) & 0x1F, h & 0x3FF, (h >> 15) & 1
    if e == 0:
        val = m * (2.0 ** -24)
    elif e == 31:
        return 0.0
    else:
        val = (1.0 + m / 1024.0) * (2.0 ** (e - 15))
    return -val if s else val


rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed")
    sys.exit(1)
st, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if st != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", st)
    sys.exit(1)

sdf = ctrl.GetStructuredFile()
texs = {t.resourceId: t for t in ctrl.GetTextures()}
acts = walk(ctrl.GetRootActions(), [])
WORK = int(rd.ActionFlags.Drawcall) | int(rd.ActionFlags.Dispatch)
work = [a for a in acts if int(a.flags) & WORK]

marks = []
for a in acts:
    if int(a.flags) & int(rd.ActionFlags.SetMarker):
        n = str(getattr(a, "customName", "") or (a.GetName(sdf) if hasattr(a, "GetName") else ""))
        if "|" in n:
            marks.append((a.eventId, n))
marks.sort()
print("markers %d, work actions %d" % (len(marks), len(work)))
if not marks:
    print("NO MARKERS -- tick DEBUG in the launcher; see the cascmark INSTALLED line in the log")
    sys.exit(0)

# last work action under the wanted node, per view
chosen = {}
for i, (eid, name) in enumerate(marks):
    view, _, node = [x.strip() for x in name.partition("|")]
    if node.split("[")[0].strip().lower() != NODE.lower():
        continue
    hi = marks[i + 1][0] - 1 if i + 1 < len(marks) else 10 ** 9
    inside = [a for a in work if eid <= a.eventId <= hi]
    if inside:
        chosen[view] = inside[-1]

if "VRCAM" not in chosen or "MAIN" not in chosen:
    print("node %r not present in both views (found: %s)" % (NODE, sorted(chosen)))
    print("nodes that ARE marked:")
    seen = []
    for _, n in marks:
        nd = n.partition("|")[2].strip().split("[")[0].strip()
        if nd not in seen:
            seen.append(nd)
    print("  " + ", ".join(seen))
    sys.exit(0)

A, B = chosen["VRCAM"], chosen["MAIN"]
tgt = None
for o in A.outputs:
    if o != NULL and o in texs:
        tgt = o
        break
if tgt is None:
    print("that action writes no colour target")
    sys.exit(1)
t = texs[tgt]
print("stage %r: VRCAM event %d, MAIN event %d" % (NODE, A.eventId, B.eventId))
print("target %s  %dx%d  %s" % (str(tgt), t.width, t.height, t.format.Name()))

fmt = t.format.Name()
if "R11G11B10" in fmt:
    px, mode = 4, "r11"
elif "R16G16B16A16" in fmt:
    px, mode = 8, "half4"
elif "R8G8B8A8" in fmt:
    px, mode = 4, "rgba8"
else:
    print("unhandled format")
    sys.exit(1)


def grid(eid):
    ctrl.SetFrameEvent(eid, True)
    data = ctrl.GetTextureData(tgt, rd.Subresource(0, 0, 0))
    if len(data) < t.width * t.height * px:
        print("  short read at %d: %d bytes" % (eid, len(data)))
        return None
    cw, ch = t.width // GRID, t.height // GRID
    rows = []
    for gy in range(GRID):
        row = []
        for gx in range(GRID):
            acc, n = 0.0, 0
            for y in range(gy * ch, (gy + 1) * ch, STEP):
                base = y * t.width * px
                for x in range(gx * cw, (gx + 1) * cw, STEP):
                    o = base + x * px
                    if mode == "r11":
                        rgb = r11g11b10(struct.unpack_from("<I", data, o)[0])
                    elif mode == "half4":
                        h = struct.unpack_from("<4H", data, o)
                        rgb = [half(h[0]), half(h[1]), half(h[2])]
                    else:
                        b0, b1, b2, _ = struct.unpack_from("<4B", data, o)
                        rgb = [b0 / 255.0, b1 / 255.0, b2 / 255.0]
                    acc += rgb[0] + rgb[1] + rgb[2]
                    n += 1
            row.append(acc / (3.0 * n) if n else 0.0)
        rows.append(row)
    return rows


ga, gb = grid(A.eventId), grid(B.eventId)
if not ga or not gb:
    sys.exit(1)

print()
print("=== VRCAM / MAIN per cell ===")
worst, wpos = 1.0, None
flat = []
for gy in range(GRID):
    line = []
    for gx in range(GRID):
        b = gb[gy][gx]
        r = (ga[gy][gx] / b) if abs(b) > 1e-9 else float("inf")
        line.append("%7.2f" % r)
        if r != float("inf"):
            flat.append(r)
            if abs(r - 1.0) > abs(worst - 1.0):
                worst, wpos = r, (gx, gy)
    print("  " + " ".join(line))

flat.sort()
mid = flat[len(flat) // 2] if flat else 0.0
over = sum(1 for r in flat if r > 1.15 or r < 0.87)
print()
print("median ratio %.3f   cells off by more than 15%%: %d of %d" % (mid, over, len(flat)))
if wpos:
    cw, ch = t.width // GRID, t.height // GRID
    print("worst cell %s = %.2fx  pixels x %d..%d  y %d..%d"
          % (wpos, worst, wpos[0] * cw, (wpos[0] + 1) * cw, wpos[1] * ch, (wpos[1] + 1) * ch))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
