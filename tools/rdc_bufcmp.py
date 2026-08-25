# -*- coding: utf-8 -*-
"""Compare two BUFFERS byte for byte, reported as float4 rows and as raw dwords.

Textures were the easy half. The interior defect turned out to hinge on a 25600-byte structured
buffer that both RenderLightsIntegrate and VolumetricFog read and that is a DIFFERENT resource in
each view -- never compared, because every earlier tool here only looked at textures and constant
blocks. 25600 = 400 x 64, the shape of a per-view list (lights, clusters, probes).

Rows are printed both ways on purpose: a light list holds floats, an index or count list holds
integers, and reading one as the other is how a real difference gets dismissed as noise.

usage:
    rdc_bufcmp.py <capture.rdc> <bufA-number> <bufB-number> [max-rows-to-print]
"""
import os, sys, struct

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
IDA = sys.argv[2]
IDB = sys.argv[3]
MAXROWS = int(sys.argv[4]) if len(sys.argv) > 4 else 24

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402

rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed")
    sys.exit(1)
status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if status != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", status)
    sys.exit(1)

bufs = {}
for b in ctrl.GetBuffers():
    bufs[str(b.resourceId)] = b


def find(num):
    for k, b in bufs.items():
        if k.endswith("::" + num):
            return b
    return None


ba, bb = find(IDA), find(IDB)
if ba is None or bb is None:
    print("buffer not found:", IDA if ba is None else IDB)
    sys.exit(1)
print("A %s  %d bytes" % (str(ba.resourceId), ba.length))
print("B %s  %d bytes" % (str(bb.resourceId), bb.length))

da = ctrl.GetBufferData(ba.resourceId, 0, 0)
db = ctrl.GetBufferData(bb.resourceId, 0, 0)
print("read A %d bytes, B %d bytes" % (len(da), len(db)))
n = min(len(da), len(db))
n -= n % 16

difrows = []
for off in range(0, n, 16):
    if da[off:off + 16] != db[off:off + 16]:
        difrows.append(off)

print()
print("=== %d of %d 16-byte rows differ ===" % (len(difrows), n // 16))
if not difrows:
    print("  the two buffers are byte-identical -- this resource is not the difference")
else:
    print("  first differing row at byte %d (row %d), last at byte %d (row %d)"
          % (difrows[0], difrows[0] // 16, difrows[-1], difrows[-1] // 16))
    print()
    print("  row   byte     A as float4                                      B as float4")
    for off in difrows[:MAXROWS]:
        fa = struct.unpack_from("<4f", da, off)
        fb = struct.unpack_from("<4f", db, off)
        print("  %4d  %6d   %-46s   %s"
              % (off // 16, off,
                 " ".join("%10.4g" % v for v in fa),
                 " ".join("%10.4g" % v for v in fb)))
    print()
    print("  same rows as dwords (for index/count fields)")
    for off in difrows[:MAXROWS]:
        ua = struct.unpack_from("<4I", da, off)
        ub = struct.unpack_from("<4I", db, off)
        print("  %4d  %6d   %-46s   %s"
              % (off // 16, off,
                 " ".join("%10d" % v for v in ua),
                 " ".join("%10d" % v for v in ub)))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
