# -*- coding: utf-8 -*-
"""One replay, every per-view question answered: which passes each view runs, and where a matched
pair differs in bindings, render target or constants.

Written as a TOOL and not a scratch file on purpose. The scratchpad versions of this were lost with a
Windows reinstall, and re-deriving them cost more than keeping them. It also does everything in ONE
replay: opening a 3.6 GB capture twice is what put the machine into swap.

Pass pairing comes from the port own PIX markers ("VIEW | Node"), which only land in the capture when
the launcher DEBUG box is ticked AND the wrapper-vtable hook installed -- check the log for
"[cascmark] ... marker hooks INSTALLED". Without them this prints nothing and says so.

usage:
    rdc_viewdiff.py <capture.rdc> [node,node,...]
"""
import os, sys, collections

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
WANT = [x.strip().lower() for x in sys.argv[2].split(",")] if len(sys.argv) > 2 else [
    "renderfogoverlay", "volumetricfog", "renderlightsintegrate", "renderbackground",
    "reflectionprobes", "globalillumination", "renderskyscattering"]

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


rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
if cap.OpenFile(CAP, "", None) != rd.ResultCode.Succeeded:
    print("OpenFile failed:", CAP)
    sys.exit(1)
status, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)
if status != rd.ResultCode.Succeeded:
    print("OpenCapture failed:", status)
    sys.exit(1)

sdf = ctrl.GetStructuredFile()
texs = {t.resourceId: t for t in ctrl.GetTextures()}
bufs = {b.resourceId: b for b in ctrl.GetBuffers()}


def dsc(rid):
    t = texs.get(rid)
    if t:
        return "%-16s %5dx%-5d a%-2d %s" % (str(rid), t.width, t.height, t.arraysize, t.format.Name())
    b = bufs.get(rid)
    return "%-16s buffer %d B" % (str(rid), b.length) if b else str(rid)


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
print("actions %d   work %d   port markers %d" % (len(acts), len(work), len(marks)))
if not marks:
    print("NO MARKERS -- tick DEBUG in the launcher and confirm the cascmark INSTALLED line")
    ctrl.Shutdown()
    cap.Shutdown()
    rd.ShutdownReplay()
    sys.exit(0)

passes = collections.OrderedDict()
for i, (eid, name) in enumerate(marks):
    hi = marks[i + 1][0] - 1 if i + 1 < len(marks) else 10 ** 9
    view, _, node = [x.strip() for x in name.partition("|")]
    node = node.split("[")[0].strip()
    lst = passes.setdefault((view, node), [])
    for a in work:
        if eid <= a.eventId <= hi:
            lst.append(a.eventId)

nodes = []
for (v, nd) in passes:
    if nd not in nodes:
        nodes.append(nd)

print()
print("=== passes only ONE view runs ===")
for nd in nodes:
    have = [v for v in ("VRCAM", "MAIN") if (v, nd) in passes]
    if len(have) == 1:
        print("  %-6s only   %-40s %d actions" % (have[0], nd, len(passes[(have[0], nd)])))

print()
print("=== matched passes with a different amount of work ===")
for nd in nodes:
    a, b = passes.get(("VRCAM", nd)), passes.get(("MAIN", nd))
    if not a or not b or len(a) == len(b):
        continue
    print("  %-40s VRCAM %4d   MAIN %4d" % (nd, len(a), len(b)))


def snapshot(eid):
    ctrl.SetFrameEvent(eid, True)
    ps = ctrl.GetPipelineState()
    reads = {}
    try:
        for u in ps.GetAllUsedDescriptors():
            d = u.descriptor if hasattr(u, "descriptor") else None
            acc = u.access if hasattr(u, "access") else None
            rid = res_of(d) if d is not None else None
            if rid is None or rid == NULL:
                continue
            reads[(str(getattr(acc, "stage", "?")).split(".")[-1], int(getattr(acc, "index", -1)))] = rid
    except Exception:
        pass
    outs = [res_of(o) for o in ps.GetOutputTargets()]
    outs = [o for o in outs if o and o != NULL]
    blocks = []
    for nm, stage in (("ps", rd.ShaderStage.Pixel), ("cs", rd.ShaderStage.Compute),
                      ("vs", rd.ShaderStage.Vertex)):
        refl = ps.GetShaderReflection(stage)
        if refl is None:
            continue
        pipe = (ps.GetComputePipelineObject() if stage == rd.ShaderStage.Compute
                else ps.GetGraphicsPipelineObject())
        sh, entry = ps.GetShader(stage), ps.GetShaderEntryPoint(stage)
        for i, blk in enumerate(refl.constantBlocks):
            ub = ps.GetConstantBlock(stage, i, 0)
            if hasattr(ub, "descriptor"):
                ub = ub.descriptor
            rid = res_of(ub)
            if rid is None or rid == NULL:
                continue
            try:
                cv = ctrl.GetCBufferVariableContents(pipe, sh, stage, entry, i, rid,
                                                     ub.byteOffset, ub.byteSize)
            except Exception:
                continue
            vals = []
            for v in cv:
                mem = list(v.members) if len(v.members) else [v]
                for m in mem:
                    vals.append(list(m.value.f32v[0:4]))
            blocks.append({"stage": nm, "slot": i, "size": blk.byteSize, "buf": str(rid), "vals": vals})
    return {"reads": reads, "outs": outs, "blocks": blocks}


for nd in nodes:
    if not any(w in nd.lower() for w in WANT):
        continue
    la, lb = passes.get(("VRCAM", nd), []), passes.get(("MAIN", nd), [])
    if not la or not lb:
        continue
    A, B = snapshot(la[0]), snapshot(lb[0])
    print()
    print("=== %s   VRCAM@%d (%d actions)   MAIN@%d (%d actions) ===" % (
        nd, la[0], len(la), lb[0], len(lb)))
    diff = [k for k in sorted(set(A["reads"]) | set(B["reads"])) if A["reads"].get(k) != B["reads"].get(k)]
    print("  bindings: %d identical, %d differ" % (len(set(A["reads"])) - len(diff), len(diff)))
    for k in diff[:8]:
        print("     %-8s idx %-3d VRCAM %s" % (k[0], k[1], dsc(A["reads"][k]) if k in A["reads"] else "(none)"))
        print("     %-8s     %-3s MAIN  %s" % ("", "", dsc(B["reads"][k]) if k in B["reads"] else "(none)"))
    print("  target VRCAM %s" % ([dsc(o) for o in A["outs"]] or "-"))
    print("  target MAIN  %s" % ([dsc(o) for o in B["outs"]] or "-"))
    byb = {(x["stage"], x["slot"]): x for x in B["blocks"]}
    for xa in A["blocks"]:
        xb = byb.get((xa["stage"], xa["slot"]))
        if not xb:
            print("  %-3s slot%-2d %5dB  only in VRCAM" % (xa["stage"], xa["slot"], xa["size"]))
            continue
        big = []
        for r in range(min(len(xa["vals"]), len(xb["vals"]))):
            for c in range(4):
                va, vb = xa["vals"][r][c], xb["vals"][r][c]
                m = max(abs(va), abs(vb))
                if m > 1e-7 and abs(va - vb) / m > 0.2:
                    big.append((r, "xyzw"[c], va, vb))
        print("  %-3s slot%-2d %5dB buf %s/%s : %d large diffs" % (
            xa["stage"], xa["slot"], xa["size"], xa["buf"], xb["buf"], len(big)))
        for d in big[:16]:
            print("        [%3d].%s  VRCAM %-15.6g MAIN %-15.6g" % d)

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
