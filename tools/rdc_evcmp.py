# -*- coding: utf-8 -*-
"""Compare TWO SPECIFIC events: shaders, bindings, render target, constants.

The companion to rdc_viewdiff.py, which compares the FIRST action of each matched pass. That is not
always the interesting one: RenderLightsIntegrate has seven actions, its first pair matched exactly,
and the seventh is where the interior defect actually lives (the ceiling pixel comes out 2.7x brighter
in red and 10x in green in the second view). Pixel history names the guilty event; this says how the
two differ.

usage:
    rdc_evcmp.py <capture.rdc> <eventA> <eventB> [labelA] [labelB]
"""
import os, sys

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
EA = int(sys.argv[2])
EB = int(sys.argv[3])
LA = sys.argv[4] if len(sys.argv) > 4 else "A"
LB = sys.argv[5] if len(sys.argv) > 5 else "B"

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
bufs = {b.resourceId: b for b in ctrl.GetBuffers()}


def dsc(rid):
    t = texs.get(rid)
    if t:
        return "%-16s %5dx%-5d a%-2d %s" % (str(rid), t.width, t.height, t.arraysize, t.format.Name())
    b = bufs.get(rid)
    return "%-16s buffer %d B" % (str(rid), b.length) if b else str(rid)


def snapshot(eid):
    ctrl.SetFrameEvent(eid, True)
    ps = ctrl.GetPipelineState()
    d = {"shaders": {}, "reads": {}, "outs": [], "blocks": [], "vp": "?"}
    for nm, stage in (("vs", rd.ShaderStage.Vertex), ("ps", rd.ShaderStage.Pixel),
                      ("cs", rd.ShaderStage.Compute)):
        refl = ps.GetShaderReflection(stage)
        d["shaders"][nm] = str(refl.resourceId) if refl is not None else "-"
    try:
        vp = ps.GetViewport(0)
        d["vp"] = "%.0fx%.0f" % (vp.width, vp.height)
    except Exception:
        pass
    try:
        for u in ps.GetAllUsedDescriptors():
            desc = u.descriptor if hasattr(u, "descriptor") else None
            acc = u.access if hasattr(u, "access") else None
            rid = res_of(desc) if desc is not None else None
            if rid is None or rid == NULL:
                continue
            d["reads"][(str(getattr(acc, "stage", "?")).split(".")[-1],
                        int(getattr(acc, "index", -1)))] = rid
    except Exception as exc:
        print("  descriptor enumeration failed at %d: %s" % (eid, exc))
    outs = [res_of(o) for o in ps.GetOutputTargets()]
    d["outs"] = [o for o in outs if o and o != NULL]
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
            d["blocks"].append({"stage": nm, "slot": i, "size": blk.byteSize,
                                "buf": str(rid), "vals": vals})
    return d


A, B = snapshot(EA), snapshot(EB)

print("=== %s event %d   vs   %s event %d ===" % (LA, EA, LB, EB))
for k in ("vs", "ps", "cs"):
    if A["shaders"][k] != B["shaders"][k]:
        print("  SHADER %s DIFFERS: %s %s   %s %s" % (k, LA, A["shaders"][k], LB, B["shaders"][k]))
    else:
        print("  shader %s same: %s" % (k, A["shaders"][k]))
print("  viewport %s %s / %s %s" % (LA, A["vp"], LB, B["vp"]))
print("  target %s %s" % (LA, [dsc(o) for o in A["outs"]] or "-"))
print("  target %s %s" % (LB, [dsc(o) for o in B["outs"]] or "-"))

diff = [k for k in sorted(set(A["reads"]) | set(B["reads"])) if A["reads"].get(k) != B["reads"].get(k)]
print()
print("  bindings: %d in %s, %d in %s, %d differ" % (len(A["reads"]), LA, len(B["reads"]), LB, len(diff)))
for k in diff:
    print("     %-8s idx %-3d %s %s" % (k[0], k[1], LA, dsc(A["reads"][k]) if k in A["reads"] else "(none)"))
    print("     %-8s     %-3s %s %s" % ("", "", LB, dsc(B["reads"][k]) if k in B["reads"] else "(none)"))

print()
byb = {(x["stage"], x["slot"]): x for x in B["blocks"]}
for xa in A["blocks"]:
    xb = byb.get((xa["stage"], xa["slot"]))
    if not xb:
        print("  %-3s slot%-2d %5dB  only in %s" % (xa["stage"], xa["slot"], xa["size"], LA))
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
    for d in big[:20]:
        print("        [%3d].%s  %s %-15.6g %s %-15.6g" % (d[0], d[1], LA, d[2], LB, d[3]))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
