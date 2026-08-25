# -*- coding: utf-8 -*-
"""For one pixel, read every INPUT TEXTURE of two events and report which inputs carry different data.

This is the tool for the case where two events bind the SAME resources and still produce different
output. In this port that case is normal rather than exotic: the frame graph pools its targets, so both
views legitimately read the same texture objects, one after the other -- and then "21 of 21 bindings
identical" says nothing at all about what was IN them at the moment each view sampled.

So this samples the actual contents. One pixel per bound texture, at the same world feature in both
views (pass the pixel for A and, if you want to cancel the eye parallax, a shifted pixel for B).

usage:
    rdc_inputpix.py <capture.rdc> <eventA> <eventB> <xA> <yA> [xB] [yB]
"""
import os, sys

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
EA = int(sys.argv[2])
EB = int(sys.argv[3])
XA = int(sys.argv[4])
YA = int(sys.argv[5])
XB = int(sys.argv[6]) if len(sys.argv) > 6 else XA
YB = int(sys.argv[7]) if len(sys.argv) > 7 else YA

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402

NULL = rd.ResourceId.Null()


def res_of(d):
    for n in ("resource", "resourceId"):
        if hasattr(d, n):
            return getattr(d, n)
    return None


rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed")
    sys.exit(1)
status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if status != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", status)
    sys.exit(1)

texs = {t.resourceId: t for t in ctrl.GetTextures()}


def bound_textures(eid):
    ctrl.SetFrameEvent(eid, True)
    ps = ctrl.GetPipelineState()
    out = {}
    try:
        for u in ps.GetAllUsedDescriptors():
            d = u.descriptor if hasattr(u, "descriptor") else None
            acc = u.access if hasattr(u, "access") else None
            rid = res_of(d) if d is not None else None
            if rid is None or rid == NULL or rid not in texs:
                continue
            out[(str(getattr(acc, "stage", "?")).split(".")[-1], int(getattr(acc, "index", -1)))] = rid
    except Exception as exc:
        print("descriptor enumeration failed at %d: %s" % (eid, exc))
    return out


def sample(eid, rid, x, y):
    """One pixel, scaled from the 2560-wide frame to this texture size."""
    t = texs[rid]
    sx = int(x * t.width / 2560.0)
    sy = int(y * t.height / 2560.0)
    sx = max(0, min(t.width - 1, sx))
    sy = max(0, min(t.height - 1, sy))
    ctrl.SetFrameEvent(eid, True)
    try:
        sub = rd.Subresource(0, 0, 0)
        val = ctrl.PickPixel(rid, sx, sy, sub, rd.CompType.Typeless)
        return list(val.floatValue)[0:4], (sx, sy)
    except Exception as exc:
        return None, str(exc)


A = bound_textures(EA)
B = bound_textures(EB)
print("event %d binds %d textures, event %d binds %d" % (EA, len(A), EB, len(B)))
print("sampling A at (%d,%d) and B at (%d,%d)" % (XA, YA, XB, YB))
print()
print("  slot            resource            size        A rgba                              B rgba                              verdict")

for k in sorted(set(A) | set(B)):
    ra, rb = A.get(k), B.get(k)
    if ra is None or rb is None:
        print("  %-8s idx %-3d  bound in only one event" % (k[0], k[1]))
        continue
    va, pa = sample(EA, ra, XA, YA)
    vb, pb = sample(EB, rb, XB, YB)
    t = texs[ra]
    if va is None or vb is None:
        print("  %-8s idx %-3d  %-16s  %4dx%-4d  read failed: %s"
              % (k[0], k[1], str(ra), t.width, t.height, pa if va is None else pb))
        continue
    worst = 0.0
    for i in range(4):
        m = max(abs(va[i]), abs(vb[i]))
        if m > 1e-6:
            worst = max(worst, abs(va[i] - vb[i]) / m)
    verdict = "SAME" if worst < 0.02 else ("differs %.0f%%" % (worst * 100.0))
    same_res = "" if ra == rb else "  [different resource]"
    print("  %-8s idx %-3d  %-16s  %4dx%-4d  %-34s  %-34s  %s%s"
          % (k[0], k[1], str(ra), t.width, t.height,
             " ".join("%8.4g" % v for v in va),
             " ".join("%8.4g" % v for v in vb),
             verdict, same_res))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
