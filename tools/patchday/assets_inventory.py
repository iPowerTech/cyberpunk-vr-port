"""Every file a patch added, one row each: where WolvenKit opened it and what it holds.

    py -3.13 -B tools/patchday/assets_inventory.py [--out build/patchday/assets-2.5] [--jobs 12] [--voice] [--retry]

assets_diff.py extracted the added files (named/<path>, unnamed/<root class>/<hash>.<ext>, unnamed/_raw/,
added/) and WolvenKit converted the CR2W ones to JSON (json/, json_named/, json_unnamed/). This pass ties the
three together for all of lists/added.tsv:

  - a CR2W file with a WolvenKit JSON: the JSON is parsed in full (WolvenKit read every chunk and buffer of it) and
    summarised -- root class, object count, the most common inner classes, the lengths of the root's lists, and
    for textures size and format;
  - a CR2W file without one: converted now with `cp77tools convert serialize` (an unnamed <hash>.cr2w is given the
    extension of its root class first, cp77tools skips unknown extensions); if WolvenKit fails -- 2.5 world sectors
    with the new worldCooked* node classes no WolvenKit release knows -- the file is read with cr2w_read.py and
    WolvenKit's error kept;
  - anything else: identified by its header (Bink 2 video, Wwise RIFF audio, opus pack, plain text);
  - with --voice: the voice-over archives' added files (left out by assets_diff, 5 GB) are unbundled with
    WolvenKit into a scratch folder a batch at a time, their RIFF headers read (codec, channels, rate, length)
    and the audio deleted again.

Writes <out>/inventory/added_inventory.tsv (one row per added file) and <out>/inventory/INVENTORY.md.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GAME = r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'
TYPE = re.compile(r'"\$type": "([\w:]+)"')
DEPOT = re.compile(r'"DepotPath": \{\s*"\$type": "ResourcePath",\s*"\$storage": "string",\s*"\$value": "([^"]+)"')


def kind_of(head):
    if head[:4] == b'CR2W':
        return 'cr2w'
    if head[:3] in (b'KB2', b'BIK'):
        return 'bink2'
    if head[:4] == b'RIFF':
        return 'riff'
    if head[:4] == b'OggS':
        return 'ogg'
    if head[:4] == b'RDHS':
        return 'shader-cache'
    if head[:4] == b'BKHD':
        return 'wwise-bank'
    s = head.lstrip()
    if s[:1] in (b'{', b'['):
        return 'json-text'
    if head and all(32 <= c < 127 or c in (9, 10, 13) for c in head[:64]):
        return 'text'
    return 'binary:' + head[:4].hex()


def riff_info(data):
    """Wwise RIFF/RIFX: codec tag, channels, rate, seconds (from the data chunk and the average byte rate)."""
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return 'not WAVE'
    at, fmt, size_data = 12, None, None
    while at + 8 <= len(data):
        cid, n = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHII', data, at + 8)
        elif cid == b'data':
            size_data = n
        at += 8 + n + (n & 1)
    if not fmt:
        return 'no fmt chunk'
    tag, channels, rate, avg = fmt
    codec = {0xFFFF: 'wwise-vorbis', 0xFFFE: 'pcm-ext', 0x0002: 'adpcm', 0x3041: 'opus', 0x3040: 'opus-ww', 1: 'pcm'}.get(tag, hex(tag))
    secs = (size_data / avg) if (size_data and avg) else 0.0
    return '%s %dch %dHz %.1fs' % (codec, channels, rate, secs)


def summarise_json(path):
    """(status, root class, objects, summary) of a WolvenKit JSON, parsed in full."""
    try:
        text = open(path, encoding='utf-8').read()
        doc = json.loads(text)
    except Exception as e:
        return 'json-broken', '', 0, str(e)[:120]
    if doc.get('Header', {}).get('DataType') != 'CR2W':
        return 'json-not-cr2w', '', 0, str(doc.get('Header', {}).get('DataType'))
    root = doc.get('Data', {}).get('RootChunk')
    if not isinstance(root, dict):
        return 'json-no-root', '', 0, ''
    cls = root.get('$type', '?')
    types = TYPE.findall(text)
    inner = collections.Counter(t for t in types if t not in (cls, 'ResourcePath', 'CName', 'handle:', 'rRef:', 'raRef:'))
    lists = sorted(((k, len(v)) for k, v in root.items() if isinstance(v, list) and v), key=lambda kv: -kv[1])[:4]
    bits = ['%s=%d' % kv for kv in lists]
    if cls == 'CBitmapTexture':
        setup = root.get('setup', {}) or {}
        bits.insert(0, '%sx%s %s/%s' % (root.get('width'), root.get('height'), setup.get('compression'), setup.get('rawFormat')))
    depots = len(set(DEPOT.findall(text)))
    if depots:
        bits.append('refs=%d' % depots)
    top = ','.join('%s:%d' % kv for kv in inner.most_common(3))
    return 'wolvenkit-ok', cls, len(types), ' '.join(bits) + (' | ' + top if top else '')


def json_index(out):
    """WolvenKit JSON -> the file it was converted from (its header's ArchiveFileName)."""
    index = {}
    for sub in ('json', 'json_named', 'json_unnamed'):
        d = os.path.join(out, sub)
        for root, _, names in os.walk(d):
            for n in names:
                if not n.endswith('.json'):
                    continue
                p = os.path.join(root, n)
                with open(p, encoding='utf-8', errors='replace') as f:
                    head = f.read(2048)
                m = re.search(r'"ArchiveFileName": "((?:[^"\\]|\\.)*)"', head)
                if m:
                    index[os.path.normcase(json.loads('"%s"' % m.group(1)))] = p
    return index


def source_index(out):
    """hash (hex) -> extracted file, from named/ (path hash), unnamed/<class>/<hex>.<ext>, unnamed/_raw, added/."""
    sys.path.insert(0, HERE)
    from archives import path_hash
    found = {}
    named = os.path.join(out, 'named')
    for root, _, names in os.walk(named):
        for n in names:
            p = os.path.join(root, n)
            found['%016x' % path_hash(os.path.relpath(p, named))] = p
    added = os.path.join(out, 'added')
    for root, _, names in os.walk(added):
        for n in names:
            p = os.path.join(root, n)
            if root == added and n.endswith('.bin') and n[:-4].isdigit():
                found.setdefault('%016x' % int(n[:-4]), p)
            elif root != added:
                found.setdefault('%016x' % path_hash(os.path.relpath(p, added)), p)
    unnamed = os.path.join(out, 'unnamed')
    for root, _, names in os.walk(unnamed):
        for n in names:
            h = n.split('.')[0]
            if re.fullmatch(r'[0-9a-f]{16}', h):
                found[h] = os.path.join(root, n)
    return found


# cp77tools picks files by extension; an unnamed file extracted as <hash>.cr2w gets the one of its root class.
CLASS_EXT = {'worldStreamingSectorInplaceContent': 'streamingsector_inplace', 'CReflectionProbeDataResource': 'envprobe',
             'communityCommunityTemplate': 'community', 'inkCreditsResource': 'credits',
             'gameSmartObjectsCompiledResource': 'smartobjects', 'worldCookedPrefabData': 'cookedprefab',
             'gsmGameDefinition': 'gamedef', 'CGIDataResource': 'gidata', 'worldStreamingSector': 'streamingsector',
             'animAnimSet': 'anims'}


def convert_now(files, scratch):
    """cp77tools convert serialize for CR2W files that have no JSON yet, in one call over a folder of copies (each
    launch costs seconds); path -> (json or None, error). The batch log does not name the file an error belongs to,
    so files left without a JSON are converted once more alone for their error."""
    sys.path.insert(0, HERE)
    import cr2w_read
    d = tempfile.mkdtemp(dir=scratch)
    src_dir, out_dir = os.path.join(d, 'in'), os.path.join(d, 'json')
    os.makedirs(src_dir)
    os.makedirs(out_dir)   # cp77tools writes nothing for a folder input when -o does not exist yet
    copies = {}
    for i, p in enumerate(files):
        name = '%04d_%s' % (i, os.path.basename(p))
        if p.endswith('.cr2w'):
            root = cr2w_read.Cr2w(open(p, 'rb').read())
            ext = CLASS_EXT.get(root.names[root.chunks[0][0]]) if root.chunks else None
            if ext:
                name = name[:-4] + ext
        shutil.copyfile(p, os.path.join(src_dir, name))
        copies[name] = p
    subprocess.run(['cp77tools', 'convert', 'serialize', src_dir, '-o', out_dir], capture_output=True, timeout=4 * 3600)
    result = {}
    for name, p in copies.items():
        j = os.path.join(out_dir, name + '.json')
        if os.path.exists(j) and os.path.getsize(j) > 0:
            result[p] = (j, '')
            continue
        alone = os.path.join(d, 'alone')
        os.makedirs(alone, exist_ok=True)
        r = subprocess.run(['cp77tools', 'convert', 'serialize', os.path.join(src_dir, name), '-o', alone],
                           capture_output=True, text=True, timeout=1800)
        lines = (r.stdout + r.stderr).strip().splitlines()
        errors = [l.split('] - ', 1)[-1] for l in lines if 'Error' in l and 'ConvertToText' not in l]
        result[p] = (None, (errors or ['no JSON written'])[0][:160])
    return result


def own_reader(path, wk_error):
    """(status, root class, chunks, summary) from cr2w_read for a CR2W file WolvenKit could not convert."""
    sys.path.insert(0, HERE)
    import cr2w_read
    try:
        cls, n, text = cr2w_read.summary(path)
    except Exception as e:
        return 'unreadable', '', 0, 'WolvenKit: %s; own reader: %s' % (wk_error, e)
    c = cr2w_read.Cr2w(open(path, 'rb').read())
    new = sorted({c.names[ch[0]] for ch in c.chunks if c.names[ch[0]].startswith('worldCooked')})
    return 'own-reader-ok', cls, n, '%s | WolvenKit: %s%s' % (text, wk_error, ' | new 2.5 classes: ' + ','.join(new) if new else '')


def voice_pass(rows, scratch, game):
    """Unbundle the voice archives' added files a batch at a time: RIFF headers read, CR2W ones (lipsync data)
    converted by WolvenKit in one call per batch and parsed. hash -> (status, kind, class, objects, detail)."""
    sys.path.insert(0, HERE)
    from archives import path_hash
    import cr2w_read
    info = {}
    by_archive = collections.defaultdict(list)
    for r in rows:
        if '_voice' in r['archive']:
            by_archive[r['archive']].append(r['hash'])
    for rel, hashes in sorted(by_archive.items()):
        for i in range(0, len(hashes), 400):
            batch = hashes[i:i + 400]
            d = tempfile.mkdtemp(dir=scratch)
            listing = os.path.join(d, '_hashes.txt')
            with open(listing, 'w', encoding='utf-8') as f:
                f.write('\n'.join(str(int(h, 16)) for h in batch) + '\n')
            out = os.path.join(d, 'files')
            subprocess.run(['cp77tools', 'unbundle', os.path.join(game, rel.replace('/', os.sep)), '-o', out, '--hash', listing],
                           capture_output=True, timeout=7200)
            cr2w = {}
            for root, _, names in os.walk(out):
                for n in names:
                    p = os.path.join(root, n)
                    key = n[:-4] if n.endswith('.bin') and n[:-4].isdigit() else None
                    h = '%016x' % (int(key) if key else path_hash(os.path.relpath(p, out)))
                    data = open(p, 'rb').read()
                    kind = kind_of(data[:64])
                    if kind == 'cr2w':
                        if key:   # <hash>.bin: cp77tools skips it, give it its root class's extension
                            root_cr2w = cr2w_read.Cr2w(data)
                            ext = CLASS_EXT.get(root_cr2w.names[root_cr2w.chunks[0][0]]) if root_cr2w.chunks else None
                            if ext:
                                os.replace(p, p[:-3] + ext)
                                p = p[:-3] + ext
                        cr2w[os.path.normcase(os.path.realpath(p))] = h   # long names, as WolvenKit writes them
                    else:
                        info[h] = ('wolvenkit-unbundled', kind, '', 0, riff_info(data) if kind == 'riff' else '%d bytes' % len(data))
            if cr2w:
                jdir = os.path.join(d, 'json')
                os.makedirs(jdir)
                subprocess.run(['cp77tools', 'convert', 'serialize', out, '-o', jdir], capture_output=True, timeout=7200)
                jsons = {}
                for root, _, names in os.walk(jdir):
                    for n in names:
                        with open(os.path.join(root, n), encoding='utf-8', errors='replace') as f:
                            m = re.search(r'"ArchiveFileName": "((?:[^"\\]|\\.)*)"', f.read(2048))
                        if m:
                            jsons[os.path.normcase(os.path.realpath(json.loads('"%s"' % m.group(1))))] = os.path.join(root, n)
                for p, h in cr2w.items():
                    j = jsons.get(p)
                    got = summarise_json(j) if j else None
                    if not got or got[0] != 'wolvenkit-ok':
                        got = own_reader(p, 'no JSON' if not j else 'empty JSON')
                    info[h] = (got[0], 'cr2w') + tuple(got[1:])
            shutil.rmtree(d, ignore_errors=True)
            print('voice %s: %d / %d' % (rel, min(i + 400, len(hashes)), len(hashes)), flush=True)
    return info


def ogg_info(path):
    """(channels, rate, seconds) of an Ogg Vorbis file: identification header of the first page, granule of the last."""
    d = open(path, 'rb').read()
    if d[:4] != b'OggS':
        return None
    seg = d[26]
    body = 27 + seg
    if d[body:body + 7] != b'\x01vorbis':
        return None
    channels, rate = d[body + 11], struct.unpack_from('<I', d, body + 12)[0]
    last = d.rfind(b'OggS')
    granule = struct.unpack_from('<q', d, last + 6)[0]
    return channels, rate, granule / rate if rate else 0.0


def bink_info(d):
    """Bink 2 header: frames, width, height, fps."""
    frames, width, height, num, den = struct.unpack_from('<IIIII', d, 8)[0], *struct.unpack_from('<IIII', d, 20)
    return 'Bink 2 %s %dx%d %d frames %.2f fps' % (d[:4].decode('ascii', 'replace'), width, height, frames, num / den if den else 0)


def bank_sections(d):
    """Wwise bank chunks -> {tag: (offset, size)}; DIDX entries are (id, offset, size) of the embedded WEMs."""
    at, out = 0, {}
    while at + 8 <= len(d):
        tag, n = d[at:at + 4].decode('ascii', 'replace'), struct.unpack_from('<I', d, at + 4)[0]
        out[tag] = (at + 8, n)
        at += 8 + n
    return out


def wk_export(files, scratch, game):
    """WolvenKit export (WEM -> Ogg Vorbis via its ww2ogg) of {name: bytes}; name -> ogg path or None."""
    d = tempfile.mkdtemp(dir=scratch)
    src, dst = os.path.join(d, 'in'), os.path.join(d, 'out')
    os.makedirs(src)
    os.makedirs(dst)
    for name, data in files.items():
        open(os.path.join(src, name + '.wem'), 'wb').write(data)
    subprocess.run(['cp77tools', 'export', src, '-o', dst, '-gp', game], capture_output=True, timeout=7200)
    made = {os.path.splitext(n)[0].lower(): os.path.join(dst, n) for n in os.listdir(dst)}
    return d, {name: made.get(name.lower()) for name in files}


def decode_result(name, data, ogg):
    riff = riff_info(data)
    if ogg:
        info = ogg_info(ogg)
        if info:
            return 'wolvenkit-decoded', 'ogg vorbis %dch %dHz %.1fs (RIFF: %s)' % (info[0], info[1], info[2], riff)
        return 'wolvenkit-decode-failed', 'WolvenKit wrote a file that is not Ogg Vorbis (RIFF: %s)' % riff
    if riff.startswith(('pcm', 'pcm-ext')):
        fmt_ok = 'data' in [c.decode('ascii', 'replace') for c in re.findall(rb'(?s)(fmt |data)', data[:4096])]
        return 'pcm-checked', 'PCM, which WolvenKit\'s ww2ogg does not take; RIFF/fmt/data consistent: %s (%s)' % (fmt_ok, riff)
    return 'wolvenkit-decode-failed', 'no output (RIFF: %s)' % riff


def audio_pass(rows, a, scratch):
    """Decode every added WEM with WolvenKit (export -> Ogg Vorbis) and check the result; read Bink 2 headers;
    split Wwise banks (BKHD/DIDX/DATA/HIRC) and decode their embedded WEMs the same way."""
    sys.path.insert(0, HERE)
    from archives import path_hash
    done = 0
    local = [r for r in rows if r['kind'] in ('riff', 'bink2', 'wwise-bank') and r['source'] and '_voice' not in r['archive']]
    files = {r['hash']: open(os.path.join(a.out, r['source']), 'rb').read() for r in local if r['kind'] == 'riff'}
    if files:
        d, made = wk_export(files, scratch, a.game)
        for r in local:
            if r['kind'] == 'riff':
                r['status'], r['detail'] = decode_result(r['hash'], files[r['hash']], made.get(r['hash']))
        shutil.rmtree(d, ignore_errors=True)
    for r in local:
        data = open(os.path.join(a.out, r['source']), 'rb').read()
        if r['kind'] == 'bink2':
            r['status'], r['detail'] = 'header-checked', bink_info(data) + ' (WolvenKit has no Bink decoder)'
        elif r['kind'] == 'wwise-bank':
            sec = bank_sections(data)
            didx = sec.get('DIDX', (0, 0))
            base = sec.get('DATA', (0, 0))[0]
            wems = {}
            for i in range(didx[1] // 12):
                wid, off, size = struct.unpack_from('<III', data, didx[0] + 12 * i)
                wems['%s_%d' % (r['hash'], wid)] = data[base + off: base + off + size]
            hirc = struct.unpack_from('<I', data, sec['HIRC'][0])[0] if 'HIRC' in sec else 0
            # DATA can hold plugin media ('PLUG', e.g. convolution impulse responses) instead of WEM audio
            plug = {n: w for n, w in wems.items() if w[:4] == b'PLUG'}
            audio = {n: w for n, w in wems.items() if n not in plug}
            ok = 0
            if audio:
                d, made = wk_export(audio, scratch, a.game)
                ok = sum(1 for n in audio if decode_result(n, audio[n], made.get(n))[0] in ('wolvenkit-decoded', 'pcm-checked'))
                shutil.rmtree(d, ignore_errors=True)
            version = struct.unpack_from('<I', data, sec['BKHD'][0])[0] if 'BKHD' in sec else 0
            if audio:
                r['status'] = 'wolvenkit-decoded' if ok == len(audio) else 'wolvenkit-decode-failed'
            else:
                r['status'] = 'header-checked'
            r['detail'] = ('Wwise bank v%d, chunks %s, %d HIRC objects, %d embedded WEMs (%d decoded by WolvenKit), '
                           '%d plugin media blobs (PLUG, %s bytes)') % (
                version, ','.join(sec), hirc, len(audio), ok, len(plug), '+'.join(str(len(w)) for w in plug.values()))
    by_archive = collections.defaultdict(list)
    for r in rows:
        if '_voice' in r['archive'] and r['kind'] == 'riff':
            by_archive[r['archive']].append(r)
    for rel, items in sorted(by_archive.items()):
        for i in range(0, len(items), 400):
            batch = items[i:i + 400]
            d = tempfile.mkdtemp(dir=scratch)
            listing = os.path.join(d, '_hashes.txt')
            open(listing, 'w').write('\n'.join(str(int(r['hash'], 16)) for r in batch) + '\n')
            out = os.path.join(d, 'files')
            subprocess.run(['cp77tools', 'unbundle', os.path.join(a.game, rel.replace('/', os.sep)), '-o', out, '--hash', listing],
                           capture_output=True, timeout=7200)
            files = {}
            for root, _, names in os.walk(out):
                for n in names:
                    p = os.path.join(root, n)
                    key = n[:-4] if n.endswith('.bin') and n[:-4].isdigit() else None
                    files['%016x' % (int(key) if key else path_hash(os.path.relpath(p, out)))] = open(p, 'rb').read()
            shutil.rmtree(out, ignore_errors=True)
            d2, made = wk_export(files, scratch, a.game)
            for r in batch:
                if r['hash'] in files:
                    r['status'], r['detail'] = decode_result(r['hash'], files[r['hash']], made.get(r['hash']))
                else:
                    r['status'], r['detail'] = 'wolvenkit-decode-failed', 'not unbundled again'
            shutil.rmtree(d2, ignore_errors=True)
            shutil.rmtree(d, ignore_errors=True)
            done += len(batch)
            print('audio %s: %d / %d' % (rel, min(i + 400, len(items)), len(items)), flush=True)
    return rows


OPENED = ('wolvenkit-ok', 'wolvenkit-unbundled', 'own-reader-ok', 'wolvenkit-decoded', 'pcm-checked', 'header-checked')
COLS = ['hash', 'archive', 'size', 'path', 'source', 'kind', 'status', 'class', 'objects', 'detail']


def fall_back(rows, out):
    """Rows WolvenKit did not open (no JSON, or an empty one) -> cr2w_read."""
    for r in rows:
        if r.get('status') in ('wolvenkit-failed', 'json-broken') and r['kind'] == 'cr2w':
            err = r.get('detail', '') if r['status'] == 'wolvenkit-failed' else 'empty JSON'
            r['status'], r['class'], r['objects'], r['detail'] = own_reader(os.path.join(out, r['source']), err)


def retry(a, inv, scratch):
    """Redo only the rows of an existing added_inventory.tsv that were not opened."""
    with open(os.path.join(inv, 'added_inventory.tsv'), encoding='utf-8') as f:
        cols = f.readline().rstrip('\n').split('\t')
        rows = [dict(zip(cols, l.rstrip('\n').split('\t'))) for l in f]
    for r in rows:
        r['size'] = int(r['size'])
    todo = [r for r in rows if r['status'] not in OPENED and r['kind'] == 'cr2w']
    print('retry %d of %d' % (len(todo), len(rows)), flush=True)
    srcs = {os.path.join(a.out, r['source']): r for r in todo}
    for k, (src, (j, err)) in enumerate(convert_now(list(srcs), scratch).items()):
        r = srcs[src]
        if j:
            r['status'], r['class'], r['objects'], r['detail'] = summarise_json(j)
            os.remove(j)
            if r['status'] == 'json-broken':
                r['detail'] = 'empty JSON'
        else:
            r['status'], r['detail'] = 'wolvenkit-failed', err
        if k % 50 == 0:
            print('converted %d / %d' % (k, len(srcs)), flush=True)
    fall_back(todo, a.out)
    voice_cr2w = [r for r in rows if '_voice' in r['archive'] and r['kind'] == 'cr2w' and r['status'] != 'wolvenkit-ok']
    if voice_cr2w:
        print('voice CR2W %d' % len(voice_cr2w), flush=True)
        voice = voice_pass(voice_cr2w, scratch, a.game)
        for r in voice_cr2w:
            if r['hash'] in voice:
                r['status'], r['kind'], r['class'], r['objects'], r['detail'] = voice[r['hash']]
    for r in rows:
        if r['kind'] == 'binary:424b4844':
            r['kind'] = 'wwise-bank'
    return rows


def write_reports(rows, inv):
    with open(os.path.join(inv, 'added_inventory.tsv'), 'w', encoding='utf-8') as f:
        f.write('\t'.join(COLS) + '\n')
        for r in rows:
            f.write('\t'.join(str(r.get(c, '')).replace('\t', ' ') for c in COLS) + '\n')
    status = collections.Counter(r.get('status', '') for r in rows)
    classes = collections.defaultdict(lambda: [0, 0])
    for r in rows:
        k = r.get('class') or r.get('kind', '')
        classes[k][0] += 1
        classes[k][1] += r['size']
    md = ['# Added files, one by one (%d)' % len(rows), '',
          'Every row of lists/added.tsv in inventory/added_inventory.tsv: archive, size, name, the extracted file, how',
          'it was opened and a summary of what it holds. wolvenkit-ok = WolvenKit converted it to JSON and the JSON',
          'parsed in full; wolvenkit-unbundled = audio/video/raw unpacked by WolvenKit and its header read;',
          'own-reader-ok = WolvenKit failed (reason in detail), read by tools/patchday/cr2w_read.py instead.', '',
          '## status', '', '| status | files |', '|---|---|']
    md += ['| %s | %d |' % kv for kv in status.most_common()]
    own = [r for r in rows if r.get('status') == 'own-reader-ok']
    if own:
        why = collections.Counter(r['detail'].split(' | WolvenKit: ', 1)[-1].split(' | ')[0] for r in own)
        md += ['', '## why WolvenKit failed (own-reader-ok rows)', '', '| WolvenKit error | files |', '|---|---|']
        md += ['| %s | %d |' % kv for kv in why.most_common()]
    md += ['', '## by root class / kind', '', '| class | files | bytes |', '|---|---|---|']
    md += ['| %s | %d | %d |' % (k, v[0], v[1]) for k, v in sorted(classes.items(), key=lambda kv: -kv[1][0])]
    failed = [r for r in rows if r.get('status') not in OPENED]
    md += ['', '## not opened (%d)' % len(failed), '']
    md += ['- %s %s %s: %s %s' % (r['hash'], r['path'] or '(unnamed)', r['archive'], r.get('status'), r.get('detail', ''))
           for r in failed[:200]]
    open(os.path.join(inv, 'INVENTORY.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print('status:', dict(status))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=os.path.join(REPO, 'build', 'patchday', 'assets-2.5'))
    ap.add_argument('--game', default=GAME)
    ap.add_argument('--jobs', type=int, default=12)
    ap.add_argument('--voice', action='store_true', help='also unbundle and read the voice-over files')
    ap.add_argument('--retry', action='store_true', help='redo only the rows of the last inventory that were not opened')
    ap.add_argument('--audio', action='store_true', help='on the last inventory: decode every WEM with WolvenKit, '
                                                         'read Bink headers, split Wwise banks')
    a = ap.parse_args()
    inv = os.path.join(a.out, 'inventory')
    os.makedirs(inv, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix='cvr_assets_')
    if a.retry or a.audio:
        rows = retry(a, inv, scratch)
        if a.audio:
            audio_pass(rows, a, scratch)
        shutil.rmtree(scratch, ignore_errors=True)
        write_reports(rows, inv)
        return
    rows = []
    for line in open(os.path.join(a.out, 'lists', 'added.tsv'), encoding='utf-8'):
        h, archive, size, *path = line.rstrip('\n').split('\t')
        rows.append(dict(hash=h, archive=archive, size=int(size), path=path[0] if path else ''))
    sources = source_index(a.out)
    jsons = json_index(a.out)
    print('rows %d, extracted %d, wolvenkit json %d' % (len(rows), len(sources), len(jsons)), flush=True)
    todo_parse, todo_convert = {}, []
    for r in rows:
        src = sources.get(r['hash'])
        r['source'] = os.path.relpath(src, a.out) if src else ''
        if not src:
            r['kind'] = 'voice' if '_voice' in r['archive'] else 'not-extracted'
            continue
        with open(src, 'rb') as f:
            head = f.read(64)
        r['kind'] = kind_of(head)
        j = jsons.get(os.path.normcase(src))
        if j:
            todo_parse[r['hash']] = j
        elif r['kind'] == 'cr2w':
            todo_convert.append(src)
    print('to parse %d, to convert now %d' % (len(todo_parse), len(todo_convert)), flush=True)
    converted = convert_now(todo_convert, scratch) if todo_convert else {}
    for r in rows:
        src = sources.get(r['hash'])
        if src and src in converted:
            j, err = converted[src]
            if j:
                todo_parse[r['hash']] = j
            else:
                r['status'], r['detail'] = 'wolvenkit-failed', err
    results = {}
    with concurrent.futures.ProcessPoolExecutor(a.jobs) as pool:
        futs = {pool.submit(summarise_json, p): h for h, p in todo_parse.items()}
        for k, fut in enumerate(concurrent.futures.as_completed(futs)):
            results[futs[fut]] = fut.result()
            if k % 1000 == 0:
                print('parsed %d / %d' % (k, len(futs)), flush=True)
    voice = voice_pass(rows, scratch, a.game) if a.voice else {}
    for r in rows:
        if r['hash'] in results:
            r['status'], r['class'], r['objects'], r['detail'] = results[r['hash']]
        elif r['hash'] in voice:
            r['status'], r['kind'], r['class'], r['objects'], r['detail'] = voice[r['hash']]
        elif 'status' not in r:
            if r['kind'] in ('bink2', 'riff', 'ogg', 'text', 'json-text') or r['kind'].startswith('binary:'):
                src = os.path.join(a.out, r['source'])
                data = open(src, 'rb').read()
                r['status'] = 'wolvenkit-unbundled'
                r['detail'] = riff_info(data) if r['kind'] == 'riff' else ('%d bytes' % len(data))
            else:
                r['status'] = r['kind']
    for r in rows:
        if r.get('status') == 'json-broken':
            r['detail'] = 'empty JSON'
    fall_back(rows, a.out)
    shutil.rmtree(scratch, ignore_errors=True)
    write_reports(rows, inv)


if __name__ == '__main__':
    main()
