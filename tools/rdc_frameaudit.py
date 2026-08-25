# -*- coding: utf-8 -*-
"""WHOLE-FRAME audit: every pass both views run, compared by what it actually produced.

The per-stage grid answers "how much do the views differ HERE". This answers the other question --
"is there anywhere left in the frame where they differ at all" -- which is the one worth asking once
the visible defects are gone and the eye can no longer tell the views apart.

Method: for each (view, node) pair the port's markers identify, take the view's last action under that
marker, take that action's colour target, and ask the REPLAY for the target's min/max via GetMinMax.
That runs on the GPU, so a fifty-pass frame costs one replay and no host memory -- reading every
2560x2560 target into Python would be gigabytes and has already put this machine into swap once.

THE FIRST VERSION OF THIS USED min/max AND WAS WRONG. Those are one pixel each: the brightest point
in the frame is a lamp or a specular highlight, and which eye sees it brightest is decided by parallax,
so it differs by tens of percent in a frame that is otherwise identical. The darkest point is near zero,
where a relative comparison reports 100% out of nothing. That version flagged ten passes at 79-100% in
a frame whose per-cell grid came out at 0.995 -- pure noise dressed as findings.

What it uses instead: the MEDIAN of each target's distribution, taken from GetHistogram over a range
shared by both views. Still entirely on the GPU, but half the pixels have to move before it does, so
parallax on a highlight cannot shift it and a real per-view mismatch (a default value, a missing input,
a stale cache) still does. The 90th percentile is printed beside it, because a difference that shows in
the tail but not the median is exactly what a shifted highlight looks like.

usage:
    rdc_frameaudit.py <capture.rdc> [tolerance-percent]
"""
import os, sys, collections

RD_DIR = r"C:\Users\dariulone\Desktop\renderdoc-src\x64\Release"
CAP = sys.argv[1]
TOL = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0

sys.path.insert(0, os.path.join(RD_DIR, "pymodules"))
os.add_dll_directory(RD_DIR)
import renderdoc as rd  # noqa: E402

NULL = rd.ResourceId.Null()


def walk(actions, out):
    for a in actions:
        out.append(a)
        if len(a.children):
            walk(a.children, out)
    return out


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
print("markers %d   work actions %d" % (len(marks), len(work)))
if not marks:
    print("NO MARKERS -- tick DEBUG in the launcher; look for the cascmark INSTALLED line")
    sys.exit(0)

# last work action per (view, node), and how many actions the pass held
last = collections.OrderedDict()
count = collections.Counter()
for i, (eid, name) in enumerate(marks):
    view, _, node = [x.strip() for x in name.partition("|")]
    node = node.split("[")[0].strip()
    hi = marks[i + 1][0] - 1 if i + 1 < len(marks) else 10 ** 9
    inside = [a for a in work if eid <= a.eventId <= hi]
    if not inside:
        continue
    count[(view, node)] += len(inside)
    last[(view, node)] = inside[-1]

nodes = []
for (v, nd) in last:
    if nd not in nodes:
        nodes.append(nd)


BUCKETS = 256
TYPE_SAMPLES = set()

# The read-write descriptor types, taken FROM THE ENUM rather than matched as text. str() on these
# yields a bare number in this build ("7", "8"), so an earlier version that searched for the substring
# "ReadWrite" matched nothing and quietly left every compute pass out of the audit -- reporting a clean
# frame while the pass that actually held the defect was never looked at.
RW_TYPES = set()
for _nm in dir(rd.DescriptorType):
    if "ReadWrite" in _nm:
        try:
            RW_TYPES.add(int(getattr(rd.DescriptorType, _nm)))
        except Exception:
            pass
print("read-write descriptor type values: %s" % sorted(RW_TYPES))


def _tyval(obj):
    try:
        return int(getattr(obj, "type", -1))
    except Exception:
        return -1


def target_of(action):
    """The texture this action writes: a colour target, or failing that a read-write (UAV) image.

    THE COLOUR-TARGET-ONLY VERSION HAD A HOLE THAT MATTERED. Twenty-one passes in this frame write
    through UAVs and bind no render target at all -- ScreenSpaceReflections, VolumetricFog,
    ReflectionProbes, DrawConeAO, ClusteredLightsCull among them -- so the audit skipped them and
    reported the frame as clean. SSR is exactly where the interior defect lived: the mask it writes
    read 1.0 in one view against 0.0157 in the other. An audit blind to compute output would have
    called that frame identical.
    """
    for o in action.outputs:
        if o != NULL and o in texs:
            return o
    ctrl.SetFrameEvent(action.eventId, True)
    ps = ctrl.GetPipelineState()
    try:
        used = ps.GetAllUsedDescriptors()
    except Exception:
        return None
    # The type lives on the ACCESS in this RenderDoc build, not on the descriptor -- checking only
    # descriptor.type silently matched nothing and left every compute pass unaudited, which is the
    # same shape of mistake as the census that read the first dword of a run. Check both, and record
    # what the type strings actually look like so a third round is not needed.
    best = None
    for u in used:
        d = u.descriptor if hasattr(u, "descriptor") else None
        acc = u.access if hasattr(u, "access") else None
        if d is None:
            continue
        TYPE_SAMPLES.add("%s %s" % (_tyval(d), _tyval(acc)))
        if _tyval(d) not in RW_TYPES and _tyval(acc) not in RW_TYPES:
            continue
        rid = None
        for n in ("resource", "resourceId"):
            if hasattr(d, n):
                rid = getattr(d, n)
                break
        if rid is None or rid == NULL or rid not in texs:
            continue
        t = texs[rid]
        # Prefer the biggest one: a pass often writes a full-screen result plus small side buffers.
        if best is None or (t.width * t.height) > (texs[best].width * texs[best].height):
            best = rid
    return best


def range_of(action, tgt):
    ctrl.SetFrameEvent(action.eventId, True)
    try:
        lo, hi = ctrl.GetMinMax(tgt, rd.Subresource(0, 0, 0), rd.CompType.Typeless)
        return min(list(lo.floatValue)[0:3]), max(list(hi.floatValue)[0:3])
    except Exception:
        return None, None


def percentiles(action, tgt, lo, hi):
    """(median, p90) of the luminance-ish distribution, from a GPU histogram."""
    if hi <= lo:
        hi = lo + 1e-6
    ctrl.SetFrameEvent(action.eventId, True)
    try:
        buckets = ctrl.GetHistogram(tgt, rd.Subresource(0, 0, 0), rd.CompType.Typeless,
                                    lo, hi, (True, True, True, False))
    except Exception as exc:
        return None, str(exc)
    total = sum(buckets)
    if not total:
        return None, "empty histogram"
    want_med, want_p90 = total * 0.5, total * 0.9
    acc, med, p90 = 0, None, None
    for i, c in enumerate(buckets):
        acc += c
        v = lo + (hi - lo) * (i + 0.5) / float(len(buckets))
        if med is None and acc >= want_med:
            med = v
        if p90 is None and acc >= want_p90:
            p90 = v
            break
    return med, p90


rows = []
skipped = []
for nd in nodes:
    a, b = last.get(("VRCAM", nd)), last.get(("MAIN", nd))
    if a is None or b is None:
        skipped.append((nd, "VRCAM only" if b is None else "MAIN only"))
        continue
    ta, tb = target_of(a), target_of(b)
    if ta is None or tb is None:
        skipped.append((nd, "writes no texture this tool can read"))
        continue
    la, ha = range_of(a, ta)
    lb, hb = range_of(b, tb)
    if la is None or lb is None:
        skipped.append((nd, "GetMinMax failed"))
        continue
    # ONE shared range for both views, or the two histograms would not be comparable at all.
    lo, hi = min(la, lb), max(ha, hb)
    ma, pa = percentiles(a, ta, lo, hi)
    mb, pb = percentiles(b, tb, lo, hi)
    if ma is None or mb is None:
        skipped.append((nd, "histogram failed: %s" % (pa if ma is None else pb)))
        continue
    scale = max(abs(ma), abs(mb), (hi - lo) * 0.01)
    worst = abs(ma - mb) / scale * 100.0
    tail = abs(pa - pb) / scale * 100.0
    rows.append((worst, nd, str(ta), str(tb), (ma, pa, tail), (mb, pb, 0.0),
                 count[("VRCAM", nd)], count[("MAIN", nd)]))

rows.sort(reverse=True)
print()
print("=== passes where the two views produced DIFFERENT extremes (worst first) ===")
print("  median%  node                                     actions V/M    VRCAM med / p90        MAIN med / p90       tail%")
shown = 0
for worst, nd, ta, tb, av, bv, ca, cb in rows:
    if worst < TOL:
        continue
    shown += 1
    same = "" if ta == tb else "  [different target]"
    print("  %6.1f  %-40s %5d/%-5d  %9.4g /%9.4g  %9.4g /%9.4g  %6.1f%s"
          % (worst, nd, ca, cb, av[0], av[1], bv[0], bv[1], av[2], same))
if not shown:
    print("  none above %.1f%% -- every matched pass produced the same extremes in both views" % TOL)

print()
print("=== summary ===")
print("  matched passes compared: %d" % len(rows))
print("  within %.1f%%: %d" % (TOL, sum(1 for r in rows if r[0] < TOL)))
print("  above  %.1f%%: %d" % (TOL, sum(1 for r in rows if r[0] >= TOL)))
if TYPE_SAMPLES:
    print()
    print("=== descriptor type strings seen (for diagnosing an empty UAV match) ===")
    for t in sorted(TYPE_SAMPLES)[:14]:
        print("  %r" % t)

if skipped:
    print()
    print("=== not comparable ===")
    for nd, why in skipped:
        print("  %-40s %s" % (nd, why))

ctrl.Shutdown()
cap.Shutdown()
rd.ShutdownReplay()
