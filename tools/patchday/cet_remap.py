"""Re-find the Cyber Engine Tweaks internals the port checks, in a new cyber_engine_tweaks.asi.

    py -3.13 -B tools/patchday/cet_remap.py [--old <asi>] [--new <asi>] [--apply]

Render/NativeTargetLifetime.hpp authenticates CET's own hook on the game's swapchain resize and
shutdown before it reads through it (ReadHookChain): CET's wrapper functions and its ResetState
(RVA + length + FNV), the CET singleton global, the two trampoline fields in it, and the module's
TimeDateStamp / SizeOfImage. All of it belongs to one CET build (1.37.1, 68D9142A). A new CET -- there
is one for nearly every game patch -- fails that check, and the lifetime tracking it guards stays off.

CET ships no address library, so this finds each function by its masked bytes (sigs.py), the global
by the RIP operand of the same instruction in the re-found function, and the fields by the
displacement of the same instruction. Only an exact masked match is rewritten; anything else is
reported for a human.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import pe  # noqa: E402
import sigs  # noqa: E402

OLD = os.path.join(REPO, 'build', 'patchday', 'base-2.31', 'frameworks', 'bin', 'x64', 'plugins',
                   'cyber_engine_tweaks.asi')
NEW = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\plugins\cyber_engine_tweaks.asi'
HEADER = os.path.join(REPO, 'include', 'Render', 'NativeTargetLifetime.hpp')


def masked_pattern(img, insns):
    parts = []
    for ins in insns:
        raw = img.read(ins.rva, ins.size)
        parts.extend('??' if ins.mask[k] else f'{raw[k]:02X}' for k in range(ins.size))
    return ' '.join(parts)


def read_constants(text):
    def num(name):
        m = re.search(name + r'\s*=\s*(0x[0-9A-Fa-f]+|\d+)', text)
        return int(m.group(1), 0)
    fns = {k: (num(f'Cet{k}Rva'), num(f'Cet{k}Bytes'), num(f'Cet{k}Hash')) for k in ('Resize', 'Reset', 'Shutdown')}
    single = int(re.search(r'Field\(read,user,cet,(0x[0-9A-Fa-f]+),value\.singleton\)', text).group(1), 16)
    ts = int(re.search(r'TimeDateStamp!=(0x[0-9A-Fa-f]+)', text).group(1), 16)
    size = int(re.search(r'SizeOfImage!=(0x[0-9A-Fa-f]+)', text).group(1), 16)
    # HookContract{..., CetResizeHash, <originalOffset>, ...}: the trampoline field in the singleton.
    offs = [int(m, 16) for m in re.findall(r'Cet(?:Resize|Shutdown)Hash,(0x[0-9A-Fa-f]+),', text)]
    return fns, single, ts, size, offs


def find_function(old, new, rva, length):
    ins = sigs.decode(old, rva, rva + length)
    pat = masked_pattern(old, ins)
    hits = sigs.find_pattern(new, pat, limit=2)
    if len(hits) == 1:
        return hits[0], 'same-code', ins
    # The body changed: its first instructions may still be unique.
    head = [i for i in ins if i.rva < rva + 24]
    hits = sigs.find_pattern(new, masked_pattern(old, head), limit=2)
    if len(hits) == 1:
        return hits[0], 'changed', ins
    return None, 'lost' if not hits else 'ambiguous', ins


def masked_equal(old, new, rva, hit, ins):
    """The old function's masked bytes, compared at one address of the new module."""
    for i in ins:
        a, b = old.read(i.rva, i.size), new.read(hit + i.rva - rva, i.size)
        if b is None or any(not i.mask[k] and a[k] != b[k] for k in range(i.size)):
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', default=OLD)
    ap.add_argument('--new', default=NEW)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--selftest', action='store_true', help='TEST: run the search against the same build')
    args = ap.parse_args()
    if args.selftest and args.apply:
        sys.exit('--selftest is a dry run')
    old, new = pe.Image(args.old), pe.Image(args.new)
    with open(HEADER, encoding='utf-8', newline='') as f:
        text = f.read()
    fns, single, ts, size, offs = read_constants(text)
    if old.timestamp != ts:
        sys.exit(f'--old is CET {old.timestamp:08X}, the header was made against {ts:08X}')
    print(f'CET {old.timestamp:08X}/{old.size_of_image:#x} -> {new.timestamp:08X}/{new.size_of_image:#x}')
    if new.timestamp == old.timestamp and not args.selftest:
        print('same CET build: nothing to do')
        return
    repl = {}
    ok = True
    new_single = set()
    new_offs = {}
    found = {name: find_function(old, new, rva, length) for name, (rva, length, _) in fns.items()}
    # A small function can match in several places; the one a re-found function CALLS is it
    # (CET's shutdown wrapper calls its ResetState). Taken only when its masked bytes match there.
    callees = {}
    for name, (hit, status, oins) in found.items():
        if status != 'same-code':
            continue
        rva = fns[name][0]
        nins = sigs.decode(new, hit, hit + fns[name][1])
        for a, b in zip(oins, nins):
            if a.mnemonic == 'call' and a.rel_target is not None and b.rel_target is not None:
                callees.setdefault(a.rel_target, set()).add(b.rel_target)
    for name, (hit, status, oins) in list(found.items()):
        rva, length, _ = fns[name]
        cand = callees.get(rva, set())
        if status != 'same-code' and len(cand) == 1:
            at = next(iter(cand))
            if masked_equal(old, new, rva, at, oins):
                found[name] = (at, 'same-code', oins)
                print(f'  {name:8s} {rva:#x}: {status} by its bytes, found as the callee of a re-found function')
            else:
                bounds = new.function_at(at)
                size = f', {bounds[1] - bounds[0]:#x} bytes (was {length:#x})' if bounds and bounds[0] == at else ''
                found[name] = (None, f'changed: the re-found caller calls {at:#x}{size}; its code differs', oins)
    for name, (rva, length, h) in fns.items():
        hit, status, oins = found[name]
        if hit is None or status != 'same-code':
            print(f'  {name:8s} {rva:#x}: {status.upper() if " " not in status else status} -- re-find by hand '
                  f'(IDA on the new asi)')
            ok = False
            continue
        raw = new.read(hit, length)
        nh = pe.fnv1a64(raw)
        print(f'  {name:8s} {rva:#x} -> {hit:#x}  FNV {h:#018x} -> {nh:#018x}')
        repl[f'Cet{name}Rva'] = (rva, hit)
        repl[f'Cet{name}Hash'] = (h, nh)
        nins = sigs.decode(new, hit, hit + length)
        for a, b in zip(oins, nins):
            if a.rip_target is not None and a.rip_target == single and b.rip_target is not None:
                new_single.add(b.rip_target)
            for off in offs:
                if f'+ {off:#x}]' in a.op_str:
                    m = re.search(r'\+ (0x[0-9a-f]+)\]', b.op_str)
                    if m:
                        new_offs[off] = int(m.group(1), 16)
    if len(new_single) != 1:
        print(f'  singleton {single:#x}: {"not re-found" if not new_single else "ambiguous " + str(new_single)}')
        ok = False
    else:
        print(f'  singleton {single:#x} -> {next(iter(new_single)):#x}')
    for off in offs:
        print(f'  trampoline field {off:#x} -> ' + (f'{new_offs[off]:#x}' if off in new_offs else 'NOT re-found'))
        ok = ok and off in new_offs
    if not args.apply:
        print('dry run' + ('' if ok else ' -- incomplete, --apply would refuse'))
        return
    if not ok:
        sys.exit('incomplete: nothing written')

    def sub_const(t, name, old_v, new_v, hexform):
        rx = re.compile(r'(' + name + r'\s*=\s*)(0x[0-9A-Fa-f]+|\d+)')
        return rx.sub(lambda m: m.group(1) + (hexform(new_v) if m.group(2).startswith('0x') else str(new_v)), t, 1)
    for name, (o, n) in repl.items():
        text = sub_const(text, name, o, n, (lambda v: f'0x{v:016X}') if name.endswith('Hash') else (lambda v: f'0x{v:X}'))
    text = text.replace(f'Field(read,user,cet,0x{single:X},', f'Field(read,user,cet,0x{next(iter(new_single)):X},')
    for off, n in new_offs.items():
        text = re.sub(r'(Cet(?:Resize|Shutdown)Hash,)0x' + f'{off:X}' + r',', lambda m: m.group(1) + f'0x{n:X},', text)
    text = text.replace(f'TimeDateStamp!=0x{ts:X}', f'TimeDateStamp!=0x{new.timestamp:X}')
    text = text.replace(f'SizeOfImage!=0x{size:X}', f'SizeOfImage!=0x{new.size_of_image:X}')
    text = text.replace(f'cet>UINT64_MAX-0x{size:X}', f'cet>UINT64_MAX-0x{new.size_of_image:X}')
    with open(HEADER, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    print('rewrote', HEADER)


if __name__ == '__main__':
    main()
