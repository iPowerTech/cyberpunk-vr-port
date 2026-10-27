"""The game image and CDPR's address library, read once and kept as arrays.

A patch moves almost every function, so a fixed RVA is only worth what can find it again. Two things
can: the address library the game ships beside the exe (bin/x64/cyberpunk2077_addresses.json -- a hash
that stays with a symbol from build to build, the same thing RED4ext resolves against), and the code
itself, compared with the bytes that hold an address masked out (.reloc absolutes, rel32 and RIP
displacements). This module provides both raw materials; inventory.py and remap.py do the matching.
"""
import bisect
import json
import os
import pickle
import struct

import numpy as np

IMAGE_BASE = 0x140000000
FNV_OFFSET = 14695981039346656037
FNV_PRIME = 1099511628211
U64 = (1 << 64) - 1


def fnv1a64(data):
    """native_recording::detail::Fingerprint, byte for byte."""
    h = FNV_OFFSET
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & U64
    return h


class Section:
    __slots__ = ('index', 'name', 'va', 'vsize', 'raw', 'rsize', 'flags')

    def __init__(self, index, name, va, vsize, raw, rsize, flags):
        self.index, self.name, self.va, self.vsize, self.raw, self.rsize, self.flags = (
            index, name, va, vsize, raw, rsize, flags)

    @property
    def code(self):
        return bool(self.flags & 0x20000000)

    def __contains__(self, rva):
        return self.va <= rva < self.va + self.vsize


class Image:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        b = self.data
        pe = struct.unpack_from('<I', b, 0x3C)[0]
        if b[pe:pe + 4] != b'PE\0\0':
            raise ValueError(f'{path}: not a PE image')
        nsec, = struct.unpack_from('<H', b, pe + 6)
        self.timestamp, = struct.unpack_from('<I', b, pe + 8)
        optsz, = struct.unpack_from('<H', b, pe + 20)
        opt = pe + 24
        self.image_base, = struct.unpack_from('<Q', b, opt + 24)
        self.size_of_image, = struct.unpack_from('<I', b, opt + 56)
        self.dirs = [struct.unpack_from('<II', b, opt + 112 + 8 * i) for i in range(16)]
        self.sections = []
        for i in range(nsec):
            o = opt + optsz + 40 * i
            name = b[o:o + 8].rstrip(b'\0').decode('latin1')
            vs, va, rs, rp = struct.unpack_from('<IIII', b, o + 8)
            flags, = struct.unpack_from('<I', b, o + 36)
            self.sections.append(Section(i + 1, name, va, vs, rp, rs, flags))
        self.text = next(s for s in self.sections if s.name == '.text')
        self._pdata = None
        self._relocs = None

    # ---- addressing -------------------------------------------------------------------------
    def section_of(self, rva):
        for s in self.sections:
            if rva in s:
                return s
        return None

    def read(self, rva, n):
        s = self.section_of(rva)
        if s is None:
            return None
        off = rva - s.va
        if off + n > s.rsize:
            # Past the raw data the section is zero-filled (.data's bss tail).
            avail = max(0, min(n, s.rsize - off))
            return self.data[s.raw + off:s.raw + off + avail] + b'\0' * (n - avail)
        return self.data[s.raw + off:s.raw + off + n]

    def text_bytes(self):
        t = self.text
        return self.data[t.raw:t.raw + min(t.vsize, t.rsize)]

    def cstr(self, rva):
        s = self.section_of(rva)
        off = s.raw + rva - s.va
        return self.data[off:self.data.index(b'\0', off)].decode('latin1')

    def imports(self):
        """{dll (lower case): set of names} of the import directory -- what an IAT hook can patch.
        Delay-loaded imports are not in it (they bind through their own table)."""
        out = {}
        rva, _ = self.dirs[1]
        while rva:
            ilt, _, _, name, iat = struct.unpack('<IIIII', self.read(rva, 20))
            if not name:
                break
            names = out.setdefault(self.cstr(name).lower(), set())
            thunk = ilt or iat
            while True:
                v, = struct.unpack('<Q', self.read(thunk, 8))
                if not v:
                    break
                if not v >> 63:
                    names.add(self.cstr((v & 0x7FFFFFFF) + 2))
                thunk += 8
            rva += 20
        return out

    # ---- functions (.pdata) -----------------------------------------------------------------
    def _load_pdata(self):
        rva, size = self.dirs[3]
        s = self.section_of(rva)
        raw = self.data[s.raw + rva - s.va:s.raw + rva - s.va + size]
        a = np.frombuffer(raw, dtype='<u4').reshape(-1, 3)
        order = np.argsort(a[:, 0], kind='stable')
        a = a[order]
        self._pdata = (a[:, 0].copy(), a[:, 1].copy(), a[:, 2].copy())

    @property
    def pdata(self):
        if self._pdata is None:
            self._load_pdata()
        return self._pdata

    def function_at(self, rva):
        """(begin, end, root_begin) of the .pdata entry that holds rva, or None.

        root_begin follows UNW_FLAG_CHAININFO back to the entry the fragment belongs to: a cold
        block split off a function carries its parent's unwind info, and only the parent has a
        symbol in the address library."""
        begins, ends, unwind = self.pdata
        i = int(np.searchsorted(begins, rva, side='right')) - 1
        if i < 0 or not (begins[i] <= rva < ends[i]):
            return None
        root = int(begins[i])
        info = int(unwind[i])
        for _ in range(8):
            hdr = self.read(info & ~1, 4)
            if hdr is None:
                break
            flags = hdr[0] >> 3
            if not flags & 0x4:
                break
            count = hdr[2]
            chained = (info & ~1) + 4 + ((count + 1) & ~1) * 2
            parent = self.read(chained, 12)
            if parent is None:
                break
            pb, _, pu = struct.unpack('<III', parent)
            root, info = pb, pu
        return int(begins[i]), int(ends[i]), root

    # ---- absolute pointers (.reloc) ---------------------------------------------------------
    def _load_relocs(self):
        rva, size = self.dirs[5]
        out = []
        pos = 0
        s = self.section_of(rva)
        blob = self.data[s.raw + rva - s.va:s.raw + rva - s.va + size]
        while pos + 8 <= len(blob):
            page, bsize = struct.unpack_from('<II', blob, pos)
            if bsize < 8:
                break
            ent = np.frombuffer(blob, dtype='<u2', count=(bsize - 8) // 2, offset=pos + 8)
            typ = ent >> 12
            out.append(np.uint32(page) + (ent[typ == 10] & 0xFFF).astype(np.uint32))
            pos += bsize
        self._relocs = np.sort(np.concatenate(out)) if out else np.zeros(0, np.uint32)

    @property
    def relocs(self):
        if self._relocs is None:
            self._load_relocs()
        return self._relocs

    def relocs_in(self, begin, end):
        r = self.relocs
        i = int(np.searchsorted(r, begin, side='left'))
        j = int(np.searchsorted(r, end, side='left'))
        return r[i:j]


class AddressLibrary:
    """bin/x64/cyberpunk2077_addresses.json: {hash, secondary hash, symbol?, offset "SSSS:OOOOOOOO"}.

    'offset' is relative to the section it names (0001 = .text at 0x1000), so the RVA is that
    section's VA plus the offset. The primary hash is what RED4ext keys on and what we match by;
    it is NOT unique (see by_hash), so every anchor records how many entries its hash names."""

    def __init__(self, path, image):
        self.path = path
        cache = path + '.cvr.pickle'
        st = os.stat(path)
        key = (st.st_size, int(st.st_mtime))
        data = None
        if os.path.exists(cache):
            try:
                with open(cache, 'rb') as f:
                    data = pickle.load(f)
                if data.get('key') != key:
                    data = None
            except Exception:
                data = None
        if data is None:
            with open(path, 'r', encoding='utf-8') as f:
                j = json.load(f)
            va = {s.index: s.va for s in image.sections}
            rvas, hashes, symbols = [], [], {}
            for e in j['Addresses']:
                sec, off = e['offset'].split(':')
                r = va[int(sec, 16)] + int(off, 16)
                h = int(e['hash'])
                rvas.append(r)
                hashes.append(h)
                if 'symbol' in e:
                    symbols[h] = e['symbol']
            data = dict(key=key, rvas=np.array(rvas, np.uint32), hashes=np.array(hashes, np.uint32),
                        symbols=symbols, timestamp=j.get('Linker map timestamp', ''))
            try:
                with open(cache, 'wb') as f:
                    pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)
            except OSError:
                pass
        self.timestamp = data['timestamp']
        self.symbols = data['symbols']
        order = np.argsort(data['rvas'], kind='stable')
        self.rvas = data['rvas'][order]
        self.hashes = data['hashes'][order]
        # NOT unique: ~65k hashes name more than one entry (folded identical code, data the
        # linker merged). An anchor on one of those is ambiguous and is never trusted alone.
        self.by_hash = {}
        for h, r in zip(self.hashes.tolist(), self.rvas.tolist()):
            self.by_hash.setdefault(h, []).append(r)

    def exact(self, rva):
        """Every hash with an entry exactly at rva (several when entries coincide)."""
        i = int(np.searchsorted(self.rvas, rva, side='left'))
        out = []
        while i < len(self.rvas) and self.rvas[i] == rva:
            out.append(int(self.hashes[i]))
            i += 1
        return out

    def unique(self, h):
        r = self.by_hash.get(h, ())
        return r[0] if len(r) == 1 else None

    def preceding(self, rva):
        """(hash, entry_rva) of the last entry at or below rva."""
        i = int(np.searchsorted(self.rvas, rva, side='right')) - 1
        if i < 0:
            return None
        return int(self.hashes[i]), int(self.rvas[i])

    def symbol(self, h):
        return self.symbols.get(h)

    def __len__(self):
        return len(self.rvas)


def find_game_files(game_dir):
    exe = os.path.join(game_dir, 'bin', 'x64', 'Cyberpunk2077.exe')
    lib = os.path.join(game_dir, 'bin', 'x64', 'cyberpunk2077_addresses.json')
    return exe, lib
