# -*- coding: utf-8 -*-
"""Which view and which frame-graph node does an event belong to?

The cheapest question to ask of a capture and the one that used to be unanswerable: a D3D12 capture is
a flat list, and this port runs the graph twice. The port marks its own work ("VIEW | Node"), so the
answer is just the nearest preceding marker -- but doing that by hand across thousands of events is how
four wrong readings got made earlier in this hunt.

usage:
    rdc_whichnode.py <capture.rdc> <event> [event ...]
"""
import os, sys

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
EVENTS = [int(x) for x in sys.argv[2:]]

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

sdf = ctrl.GetStructuredFile()


def walk(actions, out):
    for a in actions:
        out.append(a)
        if len(a.children):
            walk(a.children, out)
    return out


acts = walk(ctrl.GetRootActions(), [])
byid = {}
marks = []
for a in acts:
    byid[a.eventId] = a
    if int(a.flags) & int(rd.ActionFlags.SetMarker):
        n = str(getattr(a, "customName", "") or (a.GetName(sdf) if hasattr(a, "GetName") else ""))
        if "|" in n:
            marks.append((a.eventId, n))
marks.sort()
print("markers: %d" % len(marks))

for eid in EVENTS:
    owner = None
    for meid, n in marks:
        if meid <= eid:
            owner = (meid, n)
        else:
            break
    a = byid.get(eid)
    kind = "?"
    if a is not None:
        f = int(a.flags)
        kind = ("dispatch" if f & int(rd.ActionFlags.Dispatch) else
                "draw" if f & int(rd.ActionFlags.Drawcall) else
                "copy" if f & int(rd.ActionFlags.Copy) else
                "clear" if f & int(rd.ActionFlags.Clear) else "other")
    if owner:
        print("  %6d  %-9s  under marker @%-6d  %s" % (eid, kind, owner[0], owner[1]))
    else:
        print("  %6d  %-9s  no marker before it" % (eid, kind))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
