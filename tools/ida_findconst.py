# Headless: find every place a given float constant is written or referenced, and name the function.
#
# Run against the LOCAL database (the game folder has none since the reinstall, and D: is going away):
#   idat.exe -A -Ltools/ida_const_log.txt -Stools/ida_findconst.py C:/Users/dariulone/Desktop/ida_headless/cp2077.i64
#
# WHY. The second view's screen-space-reflection constants hold 4000 and 5000 where MAIN holds 1 and 8.
# Neither number appears in the SSR node's own code, so they arrive as DATA -- meaning some initialiser
# or default writes them into a settings structure. A round constant like 5000.0f is rare enough that
# finding its writers names that structure directly, which beats guessing which viewData field feeds it.
#
# Prints, for each constant: immediate operands in code, dwords in data with their xrefs, and the
# owning function for each hit. Edit FLOATS for other hunts -- this is a general tool.
import ida_bytes, ida_funcs, ida_name, ida_segment, idautils, idc, idaapi
import struct

OUT = "C:/Users/dariulone/Desktop/CyberpunkVRPort/tools/ida_const_out.txt"
FLOATS = (4000.0, 5000.0)
MAX_PER_CONST = 40

lines = []


def w(s):
    lines.append(str(s))


def fname(ea):
    f = ida_funcs.get_func(ea)
    if not f:
        return "(no function)"
    n = ida_name.get_ea_name(f.start_ea)
    return n if n else "sub_%X" % f.start_ea


w("imagebase %#x" % idaapi.get_imagebase())

segs = []
for s in idautils.Segments():
    seg = ida_segment.getseg(s)
    segs.append((ida_segment.get_segm_name(seg), seg.start_ea, seg.end_ea))
w("segments: %s" % ", ".join("%s %#x-%#x" % t for t in segs))

for fv in FLOATS:
    bits = struct.unpack("<I", struct.pack("<f", fv))[0]
    w("")
    w("==== %g  (bit pattern %#010x) ====" % (fv, bits))

    # 1. immediates in code
    code_hits = 0
    for segname, lo, hi in segs:
        if segname not in (".text",):
            continue
        ea = lo
        while ea < hi and code_hits < MAX_PER_CONST:
            ea = ida_bytes.next_head(ea, hi)
            if ea == idc.BADADDR or ea >= hi:
                break
            for op in (0, 1, 2):
                if idc.get_operand_type(ea, op) == idc.o_imm:
                    if (idc.get_operand_value(ea, op) & 0xFFFFFFFF) == bits:
                        w("  code   %#x  %-44s  in %s" % (ea, idc.generate_disasm_line(ea, 0), fname(ea)))
                        code_hits += 1
    if not code_hits:
        w("  no immediate operands in .text")

    # 2. dwords in data, plus who references them
    data_hits = 0
    for segname, lo, hi in segs:
        if segname in (".text",):
            continue
        ea = lo
        while ea + 4 <= hi and data_hits < MAX_PER_CONST:
            found = ida_bytes.find_bytes(struct.pack("<I", bits), ea, hi)
            if found == idc.BADADDR or found is None:
                break
            refs = list(idautils.DataRefsTo(found))
            xr = [x for x in idautils.XrefsTo(found, 0)]
            names = []
            for r in refs[:6]:
                names.append("%#x in %s" % (r, fname(r)))
            if not names:
                for x in xr[:6]:
                    names.append("%#x in %s" % (x.frm, fname(x.frm)))
            w("  data   %#x  (%s)  refs: %s" % (found, segname, "; ".join(names) if names else "none"))
            data_hits += 1
            ea = found + 4
    if not data_hits:
        w("  no matching dword in data segments")

open(OUT, "w", encoding="utf-8", errors="replace").write("\n".join(lines))
idc.qexit(0)
