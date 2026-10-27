"""Generic CR2W reader: every chunk's properties by the names and types the file itself carries.

    py -3.13 -B tools/patchday/cr2w_read.py <file> [--json out.json] [--schema]

WolvenKit reads a class it has no definition for as a dynamic class, but it still has to assign the chunk to the typed
property that points at it; when a patch adds a node class (2.5: worldCookedStaticMeshNode and friends inside
worldStreamingSector.nodes) the cast fails and the whole file is lost. This reader needs no type library: a chunk is

    u8 0, { u16 name, u16 type, u32 size (with itself), value }*, u16 0

with name and type indexes into the file's name table, so every property is found and its size known. Values are
decoded where the type string says how (primitives, CName/enums, handles, resource refs, strings, buffers, arrays,
nested structs in the same form); anything else is kept as its byte size.
"""
import argparse
import collections
import json
import struct
import sys

PRIM = {'Bool': '<?', 'Int8': '<b', 'Uint8': '<B', 'Int16': '<h', 'Uint16': '<H', 'Int32': '<i', 'Uint32': '<I',
        'Float': '<f', 'Int64': '<q', 'Uint64': '<Q', 'Double': '<d', 'TweakDBID': '<Q', 'CRUID': '<Q',
        'CDateTime': '<Q', 'EngineTime': '<d', 'CGUID': '<16s', 'Color': '<4B'}
BUFFERS = ('DataBuffer', 'SerializationDeferredDataBuffer', 'SharedDataBuffer', 'serializationDeferredDataBuffer')


class Raw(Exception):
    pass


class Cr2w:
    def __init__(self, data):
        if data[:4] != b'CR2W':
            raise ValueError('not CR2W')
        self.d = data
        self.version, self.flags = struct.unpack_from('<II', data, 4)
        self.tables = [struct.unpack_from('<III', data, 40 + 12 * i) for i in range(10)]
        s_off = self.tables[0][0]
        self.names = []
        for i in range(self.tables[1][1]):
            o, _ = struct.unpack_from('<II', data, self.tables[1][0] + 8 * i)
            self.names.append(data[s_off + o:data.index(b'\0', s_off + o)].decode('utf-8', 'replace'))
        self.imports = []
        for i in range(self.tables[2][1]):
            o, cls, fl = struct.unpack_from('<IHH', data, self.tables[2][0] + 8 * i)
            self.imports.append(data[s_off + o:data.index(b'\0', s_off + o)].decode('utf-8', 'replace'))
        self.chunks = [struct.unpack_from('<HHIIIII', data, self.tables[4][0] + 24 * i) for i in range(self.tables[4][1])]
        self.buffers = [struct.unpack_from('<IIIIII', data, self.tables[5][0] + 24 * i) for i in range(self.tables[5][1])]

    def chunk(self, i):
        cls, _flags, parent, size, off, _tpl, _crc = self.chunks[i]
        try:
            props, end = self.props(off, off + size)
        except Raw:
            props, end = [], off   # natively serialised class: no property list
        return {'class': self.names[cls], 'parent': parent - 1 if parent else None, 'props': props, 'unread': off + size - end}

    def props(self, at, end):
        if self.d[at] != 0:
            raise Raw('no leading zero')
        at += 1
        out = []
        while at + 2 <= end:
            name = struct.unpack_from('<H', self.d, at)[0]
            if name == 0:
                return out, at + 2
            typ, size = struct.unpack_from('<HI', self.d, at + 2)
            tname = self.names[typ]
            vat, vend = at + 8, at + 4 + size
            try:
                v, used = self.value(tname, vat, vend)
                if used != vend:
                    v = {'$raw': vend - vat, '$decoded_until': used - vat}
            except Raw:
                v = {'$raw': vend - vat}
            out.append((self.names[name], tname, v))
            at = vend
        return out, at

    def value(self, t, at, end):
        """(decoded value, end position) of a value of type t starting at `at`; `end` bounds it."""
        d = self.d
        if t in PRIM:
            fmt = PRIM[t]
            n = struct.calcsize(fmt)
            v = struct.unpack_from(fmt, d, at)
            v = v[0] if len(v) == 1 else list(v)
            if isinstance(v, bytes):
                v = v.hex()
            return v, at + n
        if t == 'CName' or t.startswith('E') and end - at == 2 and t not in ('EulerAngles',):
            return self.names[struct.unpack_from('<H', d, at)[0]], at + 2
        if t.startswith('handle:') or t.startswith('whandle:'):
            h = struct.unpack_from('<i', d, at)[0]
            return {'$chunk': h - 1 if h > 0 else None}, at + 4
        if t.startswith('rRef:') or t.startswith('raRef:') or t.startswith('cname:'):
            i = struct.unpack_from('<H', d, at)[0]
            return {'$ref': self.imports[i - 1] if 0 < i <= len(self.imports) else None}, at + 2
        if t in ('String', 'NodeRef'):
            return self.string(at)
        if t in BUFFERS:
            b = struct.unpack_from('<I', d, at)[0]
            i = (b & 0xFFFF) - 1
            info = self.buffers[i] if 0 <= i < len(self.buffers) else None
            return {'$buffer': i, '$mem': info[4] if info else None}, at + 4
        if t == 'LocalizationString':
            u = struct.unpack_from('<Q', d, at)[0]
            s, at = self.string(at + 8)
            return {'unk': u, 'value': s}, at
        if t.startswith('array:') or t.startswith('static:'):
            inner = t.split(':', 1)[1]
            if t.startswith('static:'):
                inner = inner.split(',', 1)[1]
            n = struct.unpack_from('<I', d, at)[0]
            at += 4
            items = []
            for _ in range(n):
                v, at = self.value(inner, at, end)
                if at > end:
                    raise Raw('array overrun')
                items.append(v)
            return items, at
        if t.startswith('curveData:') or t.startswith('multiChannelCurve:') or t.startswith('script_ref:'):
            raise Raw('curve')
        if end - at >= 3 and d[at] == 0:
            props, e = self.props(at, end)
            return {'$struct': t, 'props': props}, e
        if t.startswith('E') or end - at == 2:
            return self.names[struct.unpack_from('<H', d, at)[0]], at + 2
        raise Raw(t)

    def string(self, at):
        """VLQ length prefix: bit 7 of the first byte = 8-bit text (else UTF-16), bit 6 = more length bytes."""
        b = self.d[at]
        narrow, n, at = b & 0x80, b & 0x3F, at + 1
        if b & 0x40:
            shift = 6
            while True:
                b = self.d[at]
                n |= (b & 0x7F) << shift
                shift, at = shift + 7, at + 1
                if not b & 0x80:
                    break
        if narrow:
            return self.d[at:at + n].decode('utf-8', 'replace'), at + n
        return self.d[at:at + 2 * n].decode('utf-16-le', 'replace'), at + 2 * n


def read(path):
    c = Cr2w(open(path, 'rb').read())
    return c, [c.chunk(i) for i in range(len(c.chunks))]


def plain(v):
    """Chunk tree -> JSON-able (properties as {name: value})."""
    if isinstance(v, list):
        return [plain(x) for x in v]
    if isinstance(v, dict) and '$struct' in v:
        return {'$type': v['$struct'], **{n: plain(x) for n, _, x in v['props']}}
    return v


def schema(chunks):
    """class -> {property: type} over all chunks (and nested structs)."""
    out = collections.defaultdict(dict)

    def walk(cls, props):
        for n, t, v in props:
            out[cls][n] = t
            for x in (v if isinstance(v, list) else [v]):
                if isinstance(x, dict) and '$struct' in x:
                    walk(x['$struct'], x['props'])
    for ch in chunks:
        walk(ch['class'], ch['props'])
    return out


def summary(path):
    """One line: root class, chunk classes, root lists, properties left undecoded."""
    c, chunks = read(path)
    classes = collections.Counter(ch['class'] for ch in chunks)
    raw = sum(1 for ch in chunks for _, _, v in ch['props'] if isinstance(v, dict) and '$raw' in v)
    lists = sorted(((n, len(v)) for n, _, v in chunks[0]['props'] if isinstance(v, list) and v), key=lambda kv: -kv[1])[:4]
    bits = ['%s=%d' % kv for kv in lists] + ['chunks=%d' % len(chunks), 'buffers=%d' % len(c.buffers), 'refs=%d' % len(c.imports)]
    if raw:
        bits.append('undecoded=%d' % raw)
    top = ','.join('%s:%d' % kv for kv in classes.most_common(4) if kv[0] != chunks[0]['class'])
    return chunks[0]['class'], len(chunks), ' '.join(bits) + (' | ' + top if top else '')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('file')
    ap.add_argument('--json')
    ap.add_argument('--schema', action='store_true')
    a = ap.parse_args()
    c, chunks = read(a.file)
    if a.json:
        json.dump([{'class': ch['class'], 'parent': ch['parent'], **{n: plain(v) for n, _, v in ch['props']}} for ch in chunks],
                  open(a.json, 'w', encoding='utf-8'), indent=1)
    if a.schema:
        for cls, props in sorted(schema(chunks).items()):
            print(cls)
            for n, t in props.items():
                print('    %-40s %s' % (n, t))
    print(*summary(a.file), sep='\t')


if __name__ == '__main__':
    sys.exit(main())
