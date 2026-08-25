# -*- coding: utf-8 -*-
"""Compare two textures band by band, and say who writes them in the frame.

Built for the interior-lighting hunt: RenderFogOverlay reads a DIFFERENT 160x160 R16G16B16A16 LUT in
each view (the atmosphere / aerial-perspective table), and a mean over the whole texture said they
matched. A mean is the wrong statistic for a LUT whose axes are screen position and DISTANCE: the far
end can be empty while the average looks fine, which is exactly the reported symptom (no haze in the
distance). So this prints per-band statistics, plus the usage list, which distinguishes "this view
accumulates its own copy" from "nobody fills it".

usage:
    rdc_lutcmp.py <capture.rdc> <resA> <resB> [bands]
    where resA/resB are numeric resource ids, e.g. 280569 28848
"""
import os, sys, struct, collections

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
IDA = sys.argv[2]
IDB = sys.argv[3]
BANDS = int(sys.argv[4]) if len(sys.argv) > 4 else 8

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402


def half_to_float(h):
    s = (h >> 15) & 0x1
    e = (h >> 10) & 0x1F
    m = h & 0x3FF
    if e == 0:
        if m == 0:
            return -0.0 if s else 0.0
        val = m * (2.0 ** -24)
    elif e == 31:
        return float("nan") if m else (float("-inf") if s else float("inf"))
    else:
        val = (1.0 + m / 1024.0) * (2.0 ** (e - 15))
    return -val if s else val


rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed")
    sys.exit(1)
status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if status != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", status)
    sys.exit(1)

texs = {}
for t in ctrl.GetTextures():
    texs[str(t.resourceId)] = t


def find(num):
    for k, t in texs.items():
        if k.endswith("::" + num):
            return t
    return None


USAGE = {13: "VS_Resource", 32: "CS_Resource", 33: "CS_RWResource", 35: "PS_RWResource",
         36: "VS_RWResource", 40: "ResolveDst", 41: "Copy", 42: "CopySrc", 43: "CopyDst",
         44: "Barrier", 18: "PS_Resource", 27: "ColourTarget"}


def report(num, label):
    t = find(num)
    if t is None:
        print("%s  ResourceId::%s NOT FOUND" % (label, num))
        return None
    print("%s  %s  %dx%d a%d %s" % (label, str(t.resourceId), t.width, t.height, t.arraysize,
                                    t.format.Name()))
    us = ctrl.GetUsage(t.resourceId)
    by = collections.OrderedDict()
    for u in us:
        by.setdefault(int(u.usage), []).append(u.eventId)
    for k, evs in by.items():
        evs.sort()
        print("      usage %-3d %-14s x%-5d first %s" % (k, USAGE.get(k, "?"), len(evs), evs[:6]))
    return t


def bands(t):
    """Per-band min/max/mean of the RGB channels, banded along Y."""
    try:
        sub = rd.Subresource(0, 0, 0)
        data = ctrl.GetTextureData(t.resourceId, sub)
    except Exception as exc:
        print("      GetTextureData failed: %s" % exc)
        return None
    px = 8  # 4 channels x 2 bytes
    need = t.width * t.height * px
    if len(data) < need:
        print("      short read: %d bytes for %dx%d (need %d)" % (len(data), t.width, t.height, need))
        return None
    rows_per = max(1, t.height // BANDS)
    out = []
    for b in range(BANDS):
        y0 = b * rows_per
        y1 = min(t.height, y0 + rows_per)
        acc = [0.0, 0.0, 0.0]
        mx = [-1e30, -1e30, -1e30]
        n = 0
        for y in range(y0, y1, 2):
            base = y * t.width * px
            for x in range(0, t.width, 2):
                o = base + x * px
                h = struct.unpack_from("<4H", data, o)
                for c in range(3):
                    v = half_to_float(h[c])
                    acc[c] += v
                    if v > mx[c]:
                        mx[c] = v
                n += 1
        if n:
            out.append((y0, y1, [a / n for a in acc], mx))
    return out


print("=== who touches each LUT ===")
ta = report(IDA, "A")
print()
tb = report(IDB, "B")

print()
print("=== contents, banded along Y (A = first id, B = second) ===")
ba, bb = (bands(ta) if ta else None), (bands(tb) if tb else None)
if ba and bb:
    print("  band            A mean rgb                       B mean rgb                      ratio r")
    for (a, b) in zip(ba, bb):
        ar, ag, ab_ = a[2]
        br, bg, bb_ = b[2]
        ratio = (ar / br) if abs(br) > 1e-12 else float("inf")
        print("  y %3d..%-3d   %10.4g %10.4g %10.4g   %10.4g %10.4g %10.4g   %8.3f"
              % (a[0], a[1], ar, ag, ab_, br, bg, bb_, ratio))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
