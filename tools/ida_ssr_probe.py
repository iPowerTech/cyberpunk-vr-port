# Headless: where do the screen-space-reflection parameters come from?
#
# Run against the LOCAL database, never one on a scratch drive:
#   idat.exe -A -Ltools/ida_ssr_log.txt -Stools/ida_ssr_probe.py C:/Users/dariulone/Desktop/ida_headless/cp2077.i64
# The game folder does NOT hold a database any more (a Windows reinstall took it with the game),
# and the rescue copy on D: is going away -- the Desktop one is the copy that stays.
# Writes tools/ida_ssr_out.txt. IDA base = 0x140000000.
#
# THE QUESTION. The second view's SSR constant block holds 4000 and 5000 where MAIN holds 1 and 8
# (block of 372 bytes, float4 row 22, components y and z -- so bytes 356 and 360 of the block). Those
# are round numbers against worked ones, the same signature as the viewData fields this port already
# mirrors; 4000 in fact matches viewData+0x720 exactly. The 5000 has no known source, and guessing
# which field feeds it is what this script exists to avoid.
#
# WHAT IT PRINTS: the SSR node work function, its callees, every float immediate it references (4000
# and 5000 would show up as 0x457A0000 / 0x459C4000), and the Hex-Rays output so the field reads are
# visible as structure offsets rather than inferred.
import ida_funcs, ida_name, ida_bytes, idautils, idc, idaapi, ida_lines
import struct

try:
    import ida_hexrays
    HX = ida_hexrays.init_hexrays_plugin()
except Exception:
    HX = False

OUT = "C:/Users/dariulone/Desktop/CyberpunkVRPort/tools/ida_ssr_out.txt"
BASE = 0x140000000

# The SSR node work function, from the port's own node table (NodeNames.inc: 0x157B24).
TARGETS = [("ScreenSpaceReflections", 0x157B24)]

WANT_FLOATS = (4000.0, 5000.0, 1.0, 8.0)
WANT_BITS = set()
for f in WANT_FLOATS:
    WANT_BITS.add(struct.unpack("<I", struct.pack("<f", f))[0])

lines = []


def w(s):
    lines.append(str(s))


w("hexrays: %s   imagebase: %#x" % (HX, idaapi.get_imagebase()))
w("looking for these float bit patterns: %s" % sorted("%#x" % b for b in WANT_BITS))

seen = set()


def dump(name, ea, depth):
    if ea in seen or depth > 2:
        return
    seen.add(ea)
    f = ida_funcs.get_func(ea)
    w("")
    w("=== %s  ea=%#x  %s" % (name, ea, ("%#x..%#x" % (f.start_ea, f.end_ea)) if f else "NOT A FUNCTION"))
    if not f:
        return

    callers = sorted({x.frm for x in idautils.XrefsTo(f.start_ea, 0)})
    w("callers (%d): %s" % (len(callers), ", ".join("%#x" % c for c in callers[:10])))

    callees = []
    hits = []
    for head in idautils.Heads(f.start_ea, f.end_ea):
        mnem = idc.print_insn_mnem(head)
        if mnem == "call":
            tgt = idc.get_operand_value(head, 0)
            if tgt and ida_funcs.get_func(tgt):
                callees.append(tgt)
        for op in (0, 1, 2):
            if idc.get_operand_type(head, op) == idc.o_imm:
                v = idc.get_operand_value(head, op) & 0xFFFFFFFF
                if v in WANT_BITS:
                    hits.append((head, v))
        # constants also arrive as memory reads of a float pool
        if mnem in ("movss", "movaps", "movups", "mulss", "addss", "comiss", "cmpltss"):
            for op in (0, 1):
                if idc.get_operand_type(head, op) == idc.o_mem:
                    addr = idc.get_operand_value(head, op)
                    try:
                        raw = ida_bytes.get_dword(addr)
                    except Exception:
                        continue
                    if raw in WANT_BITS:
                        hits.append((head, raw))

    if hits:
        w("--- references to the wanted constants ---")
        for ea2, v in hits[:20]:
            fv = struct.unpack("<f", struct.pack("<I", v))[0]
            w("  %#x  %-40s  const %g" % (ea2, idc.generate_disasm_line(ea2, 0), fv))
    else:
        w("--- none of the wanted constants appear directly in this function ---")

    uniq = []
    for c in callees:
        if c not in uniq:
            uniq.append(c)
    w("callees (%d): %s" % (len(uniq), ", ".join((ida_name.get_ea_name(c) or "%#x" % c) for c in uniq[:12])))

    if HX:
        try:
            cf = ida_hexrays.decompile(f.start_ea)
            if cf:
                w("--- pseudocode ---")
                for sl in cf.get_pseudocode():
                    w("  " + ida_lines.tag_remove(sl.line))
        except Exception as exc:
            w("decompile failed: %s" % exc)

    for c in uniq[:4]:
        dump(ida_name.get_ea_name(c) or ("sub_%X" % c), c, depth + 1)


for name, rva in TARGETS:
    dump(name, BASE + rva, 0)

open(OUT, "w", encoding="utf-8", errors="replace").write("\n".join(lines))
idc.qexit(0)
