# Headless: what gates RenderSkyScattering, and what it does.
#
# Run:  idat.exe -A -S"tools\ida_sky_probe.py" "...\Cyberpunk2077.exe.i64"
# Writes tools/ida_sky_out.txt. IDA base = 0x140000000.
#
# The question: the second view never runs this node (markers: MAIN 1 pass / 2 draws, VRCAM none) and the
# interior looks lit by the sky instead of the room. The feature-bit test the frame graph asks per pass is
# sub_14023AF5C, so every call to it inside this function, with its constant bit, is the gate.
import ida_funcs, ida_name, ida_bytes, idautils, idc, idaapi, ida_lines
try:
    import ida_hexrays
    HX = ida_hexrays.init_hexrays_plugin()
except Exception:
    HX = False

OUT = "C:/Users/dariulone/Desktop/CyberpunkVRPort/tools/ida_sky_out.txt"

BASE = 0x140000000
TARGETS = [("RenderSkyScattering", 0x7818B0), ("SkyWorker", 0x7818F8),
           ("ViewPredicate_1E4B60", 0x1E4B60), ("FeatureBitTest", 0x23AF5C)]
DECOMP = {"RenderSkyScattering", "SkyWorker", "ViewPredicate_1E4B60"}

lines = []
def w(s):
    lines.append(str(s))

w("hexrays: %s   imagebase: %#x" % (HX, idaapi.get_imagebase()))
for name, rva in TARGETS:
    ea = BASE + rva
    f = ida_funcs.get_func(ea)
    w("")
    w("=== %s  ea=%#x  func=%s" % (name, ea, ("%#x..%#x" % (f.start_ea, f.end_ea)) if f else "NOT A FUNCTION"))
    if not f:
        continue
    # who calls it
    callers = sorted({x.frm for x in idautils.XrefsTo(f.start_ea, 0)})
    w("callers (%d): %s" % (len(callers), ", ".join("%#x" % c for c in callers[:12])))
    if name not in DECOMP:
        continue
    # every call this function makes, with the immediate constants seen just before it
    w("--- calls made, with any immediate operand on the call site's basic block ---")
    for head in idautils.Heads(f.start_ea, f.end_ea):
        mnem = idc.print_insn_mnem(head)
        if mnem != "call":
            continue
        tgt = idc.get_operand_value(head, 0)
        tname = ida_name.get_ea_name(tgt) or ("%#x" % tgt)
        imms = []
        p = head
        for _ in range(10):
            p = idc.prev_head(p, f.start_ea)
            if p == idc.BADADDR or p < f.start_ea:
                break
            for op in (0, 1):
                if idc.get_operand_type(p, op) == idc.o_imm:
                    imms.append(idc.get_operand_value(p, op))
        w("  %#x  call %-40s  immediates before: %s" % (head, tname, imms[:6]))
    if HX:
        try:
            cf = ida_hexrays.decompile(f.start_ea)
            if cf:
                w("--- pseudocode ---")
                for sl in cf.get_pseudocode():
                    w("  " + ida_lines.tag_remove(sl.line))
        except Exception as exc:
            w("decompile failed: %s" % exc)

open(OUT, "w", encoding="utf-8", errors="replace").write("\n".join(lines))
idc.qexit(0)
