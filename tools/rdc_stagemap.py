# -*- coding: utf-8 -*-
"""Where in the frame do the two views disagree? A grid of ratios over one SHARED render target,
sampled at the equivalent stage of each view.

The point is to stop guessing which pass to inspect. Both views write the same scene HDR target, so
reading it right after the same named pass in each view gives a like-for-like comparison, and the grid
says WHERE the difference lives -- ceiling, windows, distance -- instead of collapsing it into one mean
that hides everything. Ratios near 1.0 are parallax; a block of 1.5-2.0 is the defect.

usage:
    rdc_stagemap.py <capture.rdc> <resource-number> <eventA> <eventB> [grid] [step]
    e.g. rdc_stagemap.py frame.rdc 29850 17730 41834 8 4
         (eventA = after VRCAM RenderFogOverlay, eventB = after MAIN RenderFogOverlay)
"""
import os, sys, struct

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
RES = sys.argv[2]
EA = int(sys.argv[3])
EB = int(sys.argv[4])
GRID = int(sys.argv[5]) if len(sys.argv) > 5 else 8
STEP = int(sys.argv[6]) if len(sys.argv) > 6 else 4

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402


def half_to_float(h):
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    s = (h >> 15) & 0x1
    if e == 0:
        val = m * (2.0 ** -24)
    elif e == 31:
        return 0.0
    else:
        val = (1.0 + m / 1024.0) * (2.0 ** (e - 15))
    return -val if s else val


def r11g11b10_to_float(u):
    """R11G11B10_FLOAT: 5-bit exponent, no sign, 6/6/5 mantissa bits."""
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


rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed")
    sys.exit(1)
status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if status != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", status)
    sys.exit(1)

tex = None
for t in ctrl.GetTextures():
    if str(t.resourceId).endswith("::" + RES):
        tex = t
        break
if tex is None:
    print("resource not found:", RES)
    sys.exit(1)
print("target %s  %dx%d  %s" % (str(tex.resourceId), tex.width, tex.height, tex.format.Name()))

fmt = tex.format.Name()
if "R11G11B10" in fmt:
    px, decode = 4, "r11"
elif "R16G16B16A16" in fmt:
    px, decode = 8, "half4"
else:
    print("unhandled format:", fmt)
    sys.exit(1)


def grid_means(eid):
    ctrl.SetFrameEvent(eid, True)
    sub = rd.Subresource(0, 0, 0)
    data = ctrl.GetTextureData(tex.resourceId, sub)
    need = tex.width * tex.height * px
    if len(data) < need:
        print("  short read at %d: %d of %d" % (eid, len(data), need))
        return None
    cw, ch = tex.width // GRID, tex.height // GRID
    cells = []
    for gy in range(GRID):
        row = []
        for gx in range(GRID):
            acc, n = 0.0, 0
            for y in range(gy * ch, (gy + 1) * ch, STEP):
                base = y * tex.width * px
                for x in range(gx * cw, (gx + 1) * cw, STEP):
                    o = base + x * px
                    if decode == "r11":
                        u = struct.unpack_from("<I", data, o)[0]
                        rgb = r11g11b10_to_float(u)
                    else:
                        h = struct.unpack_from("<4H", data, o)
                        rgb = [half_to_float(h[0]), half_to_float(h[1]), half_to_float(h[2])]
                    acc += rgb[0] + rgb[1] + rgb[2]
                    n += 1
            row.append(acc / (3.0 * n) if n else 0.0)
        cells.append(row)
    return cells


A = grid_means(EA)
B = grid_means(EB)
if not A or not B:
    sys.exit(1)

print()
print("=== luminance-ish mean per cell: A (event %d) ===" % EA)
for row in A:
    print("  " + " ".join("%8.4f" % v for v in row))
print()
print("=== B (event %d) ===" % EB)
for row in B:
    print("  " + " ".join("%8.4f" % v for v in row))
print()
print("=== A / B  (1.00 = agree; a block well above 1 is the defect) ===")
worst, wpos = 0.0, None
for gy in range(GRID):
    line = []
    for gx in range(GRID):
        b = B[gy][gx]
        r = (A[gy][gx] / b) if abs(b) > 1e-9 else float("inf")
        line.append("%8.2f" % r)
        if r != float("inf") and r > worst:
            worst, wpos = r, (gx, gy)
    print("  " + " ".join(line))
if wpos:
    cw, ch = tex.width // GRID, tex.height // GRID
    print()
    print("worst cell %s = %.2fx, pixels x %d..%d  y %d..%d"
          % (wpos, worst, wpos[0] * cw, (wpos[0] + 1) * cw, wpos[1] * ch, (wpos[1] + 1) * ch))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
