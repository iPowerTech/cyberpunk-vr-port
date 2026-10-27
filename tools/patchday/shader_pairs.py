"""Pair the port's orphaned shader twins with their recompiled originals in a new game build.

    py -3.13 -B tools/patchday/shader_pairs.py [--old build/patchday/base-2.31] [--new <game>] [--out build/patchday/shaders-2.5]

check_shaders.py says which originals a patch recompiled (their bytecode, so their FNV-1a, so the twin's file
name, changed). An entry of the engine's caches is key(16) size(4) DXBC; the key is NOT stable across builds.

  shader_final.cache      the footer's first table holds one record per compiled technique pass,
                          "<material> CompiledTechnique [Index, Pass, RenderStageContext, VF]" with the first 8 key
                          bytes of its vertex shader (field 0) and pixel shader (field 1). The name is stable, so an
                          original is found again as the same slot of the same-named record.
  staticshader_final.cache  no names. Matched by content: same program kind (and compute group size), then the
                          closest instruction stream of the dxc disassembly (histogram prefilter, sequence ratio),
                          reported with its margin over the runner-up.

Writes <out>/pairs.json {old fnv: {new fnv(s), folders, method, score}}, <out>/old/<fnv>.dxil and
<out>/new/<fnv>.dxil (the originals the hash-keyed generators take with --shaders).
"""
import argparse
import collections
import concurrent.futures
import difflib
import json
import math
import mmap
import os
import re
import struct
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
DXC = r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe'


def fnv1a(data):
    h = 1469598103934665603
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def entries(path):
    """[(key hex, offset, size)] of the cache's entries: key(16) size(4) DXBC..., found by the DXBC magic."""
    out = []
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        pos, n = 0, mm.size()
        while True:
            i = mm.find(b'DXBC', pos)
            if i < 0:
                break
            size, = struct.unpack_from('<I', mm, i + 24)
            if 32 < size < 16 << 20 and i + size <= n and i >= 20 and struct.unpack_from('<I', mm, i - 4)[0] == size:
                out.append((bytes(mm[i - 20:i - 4]).hex(), i, size))
                pos = i + size
                continue
            pos = i + 4
        mm.close()
    return out


def read(path, off, size):
    with open(path, 'rb') as f:
        f.seek(off)
        return f.read(size)


def _varint(b, i):
    c = b[i]
    v, shift, i = c & 0x3F, 6, i + 1
    if c & 0x40:
        while True:
            c = b[i]
            i += 1
            v |= (c & 0x7F) << shift
            shift += 7
            if not c & 0x80:
                break
    return v, i


def technique_records(path):
    """{record name: [vs key8 hex, ps key8 hex]} from shader_final.cache's footer table (RDHS v10)."""
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        n = mm.size()
        foot = struct.unpack('<10Q', mm[n - 80:n])
        assert foot[9] & 0xFFFFFFFF == 0x53484452, 'not an RDHS footer'
        b = mm[foot[0]:foot[6]]
        mm.close()
    out, i = {}, 0
    while i < len(b) - 16:
        rid = b[i:i + 8]
        length, j = _varint(b, i + 8)
        name = b[j:j + length].decode('latin-1')
        j += length
        assert b[j:j + 4] == rid[:4], 'record id (low) mismatch'
        fields = [b[j + 4 + 8 * k:j + 12 + 8 * k].hex() for k in range(5)]
        j += 48
        assert b[j - 4:j] == rid[4:], 'record id (high) mismatch'
        for _ in range(2):
            cnt, = struct.unpack_from('<I', b, j)
            assert cnt <= 64, 'record list too long'
            j += 4 + 8 * cnt
        out[name] = fields[:2]
        i = j
    return out


def parts(c):
    n, = struct.unpack_from('<I', c, 28)
    out = {}
    for k in range(n):
        o, = struct.unpack_from('<I', c, 32 + 4 * k)
        out[c[o:o + 4].decode('latin-1')] = c[o + 8:o + 8 + struct.unpack_from('<I', c, o + 4)[0]]
    return out


def kind(c):
    return struct.unpack_from('<I', parts(c)['DXIL'], 0)[0] >> 16


CALL = re.compile(r'@(dx\.op\.[A-Za-z0-9]+)')
OP = re.compile(r'^\s+(?:%[\w.]+ = )?([a-z]+)')
NUMTHREADS = re.compile(r'!\{i32 (\d+), i32 (\d+), i32 (\d+)\}')
LITERAL = re.compile(r'\b(?:i32|i8|i1|float|half) (-?[0-9][0-9.e+-]*|0x[0-9A-F]+|true|false)\b')


def literal_tokens(body):
    """The same stream with each instruction's literal operands (cbuffer rows, loop bounds, constants) attached:
    quality permutations of one shader differ there and nowhere else."""
    out = []
    for line in body.splitlines()[1:]:
        code = line.split(';', 1)[0]
        m = CALL.search(code) or OP.match(code)
        if m:
            out.append(m.group(1) + '|' + ','.join(LITERAL.findall(code)))
    return out


def tokens(container, work):
    """The instruction stream of the entry function (dx.op names and LLVM opcodes, no operands), the compute group
    size, the entry function's name and the literal-aware stream."""
    src = os.path.join(work, '%016x.dxil' % fnv1a(container))
    with open(src, 'wb') as f:
        f.write(container)
    out = src + '.ll'
    subprocess.run([DXC, '-dumpbin', src, '-Fc', out], check=True, capture_output=True)
    text = open(out, encoding='utf-8', errors='replace').read()
    m = re.search(r'(?m)^define void @([\w.$]+)\(\)[^\n]*\{$', text)
    name = m.group(1) if m else None
    body = text[m.start():] if m else ''
    body = body[:body.find('\n}\n')]
    seq = []
    for line in body.splitlines()[1:]:
        m = CALL.search(line)
        if m:
            seq.append(m.group(1))
            continue
        m = OP.match(line)
        if m:
            seq.append(m.group(1))
    groups = None
    m = re.search(r'!dx\.entryPoints = !\{!(\d+)\}', text)
    if m:
        entry = re.search(r'(?m)^!%s = !\{[^\n]*\}' % m.group(1), text)
        tags = entry and re.findall(r'!(\d+)\}$', entry.group(0))
        if tags:
            tag = re.search(r'(?m)^!%s = !\{([^\n]*)\}' % tags[0], text)
            nt = tag and re.search(r'i32 4, !(\d+)', tag.group(1))
            if nt:
                g = re.search(r'(?m)^!%s = (!\{i32 \d+, i32 \d+, i32 \d+\})' % nt.group(1), text)
                if g:
                    groups = tuple(int(x) for x in NUMTHREADS.match(g.group(1)).groups())
    return seq, groups, name, literal_tokens(body)


def cosine(a, b):
    num = sum(v * b.get(k, 0) for k, v in a.items())
    den = math.sqrt(sum(v * v for v in a.values())) * math.sqrt(sum(v * v for v in b.values()))
    return num / den if den else 0.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--old', default=os.path.join(REPO, 'build', 'patchday', 'base-2.31'))
    ap.add_argument('--new', default=GAME)
    ap.add_argument('--index', default=os.path.join(HERE, 'data', 'shader_twins-2.31.json'))
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'shaders-2.5'))
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args()
    idx = json.load(open(a.index, encoding='utf-8'))['twins']
    want = {int(h, 16): t for h, t in idx.items()}
    os.makedirs(os.path.join(a.out, 'old'), exist_ok=True)
    os.makedirs(os.path.join(a.out, 'new'), exist_ok=True)
    pairs = {}

    def keep(side, blob):
        h = fnv1a(blob)
        with open(os.path.join(a.out, side, '%016x.dxil' % h), 'wb') as f:
            f.write(blob)
        return h

    # -- shader_final: by technique record name and slot
    cache = 'shader_final.cache'
    po, pn = os.path.join(a.old, 'engine', cache), os.path.join(a.new, 'engine', cache)
    sizes = {t['size'] for t in want.values() if t['cache'] == cache}
    old_key = {}
    for key, off, size in entries(po):
        if size in sizes:
            blob = read(po, off, size)
            h = fnv1a(blob)
            if h in want:
                old_key[h] = (key[:16], blob)
    new_entries = {key[:16]: (off, size) for key, off, size in entries(pn)}
    rec_old, rec_new = technique_records(po), technique_records(pn)
    uses = collections.defaultdict(list)
    for name, fields in rec_old.items():
        for slot, k8 in enumerate(fields):
            uses[k8].append((name, slot))
    for h, (k8, blob) in sorted(old_key.items()):
        found = collections.Counter()
        missing = 0
        for name, slot in uses.get(k8, []):
            nf = rec_new.get(name)
            if nf and nf[slot] in new_entries:
                found[nf[slot]] += 1
            else:
                missing += 1
        keep('old', blob)
        news = []
        for k8n, _ in found.most_common():
            off, size = new_entries[k8n]
            news.append('%016x' % keep('new', read(pn, off, size)))
        pairs['%016x' % h] = dict(new=news, folders=want[h]['folders'], cache=cache, method='technique-record',
                                  records=len(uses.get(k8, [])), records_gone=missing)

    # -- staticshader_final: by content
    cache = 'staticshader_final.cache'
    po, pn = os.path.join(a.old, 'engine', cache), os.path.join(a.new, 'engine', cache)
    olds = []
    for key, off, size in entries(po):
        blob = read(po, off, size)
        h = fnv1a(blob)
        if h in want and want[h]['cache'] == cache:
            olds.append((h, blob))
    news = [read(pn, off, size) for _, off, size in entries(pn)]
    with tempfile.TemporaryDirectory() as work:
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as pool:
            old_tok = dict(zip([h for h, _ in olds], pool.map(lambda x: tokens(x[1], work), olds)))
            new_tok = list(pool.map(lambda c: tokens(c, work), news))
    new_kind = [kind(c) for c in news]
    new_hist = [collections.Counter(t[0]) for t in new_tok]
    for h, blob in olds:
        seq, groups, entry, lit = old_tok[h]
        if not seq:
            raise RuntimeError('no instructions read from %016x' % h)
        hist = collections.Counter(seq)
        k = kind(blob)
        pool_ = [i for i in range(len(news)) if new_kind[i] == k and new_tok[i][1:3] == (groups, entry)]
        pre = sorted(pool_, key=lambda i: -cosine(hist, new_hist[i]))[:12]
        scored = sorted(((difflib.SequenceMatcher(None, lit, new_tok[i][3], autojunk=False).ratio(), i) for i in pre),
                        reverse=True)
        keep('old', blob)
        best = scored[0] if scored else (0.0, None)
        # Quality permutations can tie: every candidate as close as the best is kept (the generators' own
        # structural checks then take the ones their recipe fits), the next lower score is the margin.
        tied = [i for r, i in scored if r >= best[0] - 0.002]
        second = next((r for r, _ in scored if r < best[0] - 0.002), 0.0)
        nhs = ['%016x' % keep('new', news[i]) for i in tied]
        pairs['%016x' % h] = dict(new=nhs, folders=want[h]['folders'],
                                  cache=cache, method='content', score=round(best[0], 4),
                                  runner_up=round(second, 4), groups=groups, entry=entry, candidates=len(pool_))
    json.dump(pairs, open(os.path.join(a.out, 'pairs.json'), 'w', encoding='utf-8'), indent=1)
    per = collections.Counter()
    for h, p in sorted(pairs.items(), key=lambda kv: (kv[1]['method'], kv[1]['folders'])):
        for folder in p['folders']:
            per[folder, bool(p['new'])] += 1
        if p['method'] == 'content':
            print(f"  {h} {','.join(p['folders']):20s} -> {'+'.join(p['new']) or '-'} score {p['score']} "
                  f"(next {p['runner_up']}, {p['candidates']} candidates, {p['entry']} {p['groups']})")
        elif len(p['new']) != 1 or p['records_gone']:
            print(f"  {h} {','.join(p['folders']):20s} -> {p['new']} ({p['records']} records, {p['records_gone']} gone)")
    for (folder, ok), n in sorted(per.items()):
        print(f"{folder:15s} {'paired' if ok else 'NOT PAIRED'} {n}")
    print('->', os.path.join(a.out, 'pairs.json'))


if __name__ == '__main__':
    main()
