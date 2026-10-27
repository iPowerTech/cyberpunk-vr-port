"""Masked code: the bytes of a function with every byte that holds an address replaced by a wildcard.

What a patch changes in an unchanged function is exactly those bytes -- rel32 call/jump targets, RIP
displacements, the 64-bit absolutes listed in .reloc. With them masked, "same masked bytes" means
"same code", and the offset of a site inside the function carries over. What stays (struct
offsets, immediates, register choice) is the meaning; when it differs the function was changed and
the site needs a human.
"""
import hashlib
import re

import capstone
from capstone import x86

_md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
_md.detail = True
_md.skipdata = True

_REL_GROUPS = (capstone.CS_GRP_JUMP, capstone.CS_GRP_CALL, capstone.CS_GRP_BRANCH_RELATIVE)


class Insn:
    __slots__ = ('rva', 'size', 'mask', 'mnemonic', 'op_str', 'rip_target', 'rel_target')

    def __init__(self, rva, size, mask, mnemonic, op_str, rip_target, rel_target):
        self.rva, self.size, self.mask = rva, size, mask
        self.mnemonic, self.op_str = mnemonic, op_str
        self.rip_target, self.rel_target = rip_target, rel_target


def decode(image, begin, end):
    """Linear decode of [begin, end) with a per-byte mask (True = wildcard)."""
    code = image.read(begin, end - begin)
    if code is None:
        return []
    relocs = set(int(r) for r in image.relocs_in(begin, end))
    out = []
    for i in _md.disasm(code, begin):
        mask = [False] * i.size
        rip_target = rel_target = None
        if i.id != 0:
            try:
                is_rel = any(i.group(g) for g in _REL_GROUPS)
            except capstone.CsError:
                is_rel = False
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP:
                    rip_target = i.address + i.size + op.mem.disp
                    off, sz = i.disp_offset, i.disp_size
                    for k in range(off, min(off + sz, i.size)):
                        mask[k] = True
                elif op.type == x86.X86_OP_IMM and is_rel:
                    rel_target = op.imm
                    off, sz = i.imm_offset, i.imm_size
                    for k in range(off, min(off + sz, i.size)):
                        mask[k] = True
        for k in range(i.size):
            if (i.address + k) in relocs:
                # An 8-byte absolute starts here; it may straddle into the next instruction's
                # bytes only in data mis-decoded as code, which the mask then covers too.
                for q in range(k, min(k + 8, i.size)):
                    mask[q] = True
        out.append(Insn(i.address, i.size, mask, i.mnemonic, i.op_str, rip_target, rel_target))
    return out


def masked_bytes(image, insns):
    """bytes with wildcards as 0x00 plus the mask as a parallel bytes object."""
    data = bytearray()
    mask = bytearray()
    for ins in insns:
        raw = image.read(ins.rva, ins.size)
        for k in range(ins.size):
            data.append(0 if ins.mask[k] else raw[k])
            mask.append(1 if ins.mask[k] else 0)
    return bytes(data), bytes(mask)


def masked_hash(image, insns):
    data, mask = masked_bytes(image, insns)
    h = hashlib.sha1()
    h.update(len(data).to_bytes(4, 'little'))
    h.update(data)
    h.update(mask)
    return h.hexdigest()


def range_masked_hash(image, begin, length, func_insns=None):
    """Masked hash of an arbitrary byte range, masked by the decode of the function holding it."""
    if func_insns is None:
        f = image.function_at(begin)
        func_insns = decode(image, f[0], f[1]) if f else decode(image, begin, begin + length)
    raw = image.read(begin, length)
    if raw is None:
        return None
    mask = bytearray(length)
    for ins in func_insns:
        if ins.rva + ins.size <= begin or ins.rva >= begin + length:
            continue
        for k in range(ins.size):
            a = ins.rva + k - begin
            if 0 <= a < length and ins.mask[k]:
                mask[a] = 1
    relocs = image.relocs_in(begin - 7, begin + length)
    for r in relocs:
        for q in range(8):
            a = int(r) + q - begin
            if 0 <= a < length:
                mask[a] = 1
    data = bytes(0 if mask[k] else raw[k] for k in range(length))
    h = hashlib.sha1()
    h.update(length.to_bytes(4, 'little'))
    h.update(data)
    h.update(bytes(mask))
    return h.hexdigest()


def window(image, insns, site, before=6, after=10):
    """Masked pattern of the instructions around site: (pattern hex with '??', offset of site)."""
    idx = None
    for n, ins in enumerate(insns):
        if ins.rva <= site < ins.rva + ins.size:
            idx = n
            break
    if idx is None:
        return None, None
    lo = max(0, idx - before)
    hi = min(len(insns), idx + after + 1)
    parts = []
    for ins in insns[lo:hi]:
        raw = image.read(ins.rva, ins.size)
        parts.extend('??' if ins.mask[k] else f'{raw[k]:02X}' for k in range(ins.size))
    return ' '.join(parts), site - insns[lo].rva


def pattern_regex(pattern):
    out = []
    for tok in pattern.split():
        out.append(b'.' if tok == '??' else re.escape(bytes([int(tok, 16)])))
    return re.compile(b''.join(out), re.DOTALL)


def find_pattern(image, pattern, begin=None, end=None, limit=4):
    """RVAs where the masked pattern matches inside [begin, end) of .text (all of it by default)."""
    t = image.text
    lo = t.va if begin is None else max(begin, t.va)
    hi = t.va + min(t.vsize, t.rsize) if end is None else min(end, t.va + min(t.vsize, t.rsize))
    blob = image.data[t.raw + lo - t.va:t.raw + hi - t.va]
    rx = pattern_regex(pattern)
    hits = []
    pos = 0
    while len(hits) < limit:
        m = rx.search(blob, pos)
        if not m:
            break
        hits.append(lo + m.start())
        pos = m.start() + 1
    return hits


_TERMINATORS = {'ret', 'retn', 'int3', 'jmp', 'ud2'}


def decode_leaf(image, begin, limit=512):
    """A function with no .pdata entry (a leaf that never touches the stack): decode from its
    start to the first ret / unconditional jmp / int3, at most limit bytes."""
    insns = decode(image, begin, begin + limit)
    out = []
    for ins in insns:
        out.append(ins)
        if ins.mnemonic in _TERMINATORS:
            break
    return out
