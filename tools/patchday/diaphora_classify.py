"""Every function of the 2.31 -> 2.5 Diaphora diff, one row each: what kind of change it is and which part of the
engine it belongs to.

    py -3.13 -B tools/patchday/diaphora_classify.py [--diff build/patchday/diaphora-2.5]
        [--old build/patchday/ida/2.31/cp2077_2.31.sqlite] [--new build/patchday/ida/2.5/cp2077_2.5.sqlite]

Inputs: the Diaphora match lists (matched.tsv, unmatched_old.tsv, unmatched_new.tsv), both Diaphora exports
(instruction mnemonics, referenced names, constants, graph shape per function), and from IDA on 2.5
func_strings_2.5.json (the string literals each function references) and func_callees_2.5.json (its direct
callees).

Change kind of a matched pair, the first that fits:
  identical       Diaphora ratio 1.0 (same cleaned assembly / bytes)
  addresses-only  same mnemonic sequence and constants: only the addresses it refers to moved
  operands        same mnemonic sequence, other constants: immediates / struct offsets changed
  instructions    same control-flow graph (nodes, edges), other instructions
  control-flow    graph changed, the function is still mostly the same (ratio >= 0.5, size within 25 %)
  rewritten       anything bigger
Subsystem: keyword classes over the function's own strings; a function without strings takes the majority of its
direct callers' and callees' (one hop, then up to four); then its imported names, then tiny bodies (trivial). A
function still left is usually reached only indirectly, so refs_unattributed_<build>.json (IDA,
ida/scripts/refs_of.py) adds: the class of a vtable holding it (MSVC RTTI, or the REDengine class registered behind
the table's native-type slot, by module prefix), the constructor installing that vtable, the function taking its
address, then call-graph neighbours again. Removed functions use the strings in their 2.31 assembly and the same
indirect evidence in 2.31, with neighbours mapped through the match. The `context` column says which vtable slot or
referrer it was.

Writes <diff>/functions.tsv (all functions of both builds) and <diff>/CLASSIFIED.md.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import sqlite3

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PD = os.path.join(REPO, 'build', 'patchday')
BASE = 0x140000000
NOISE = re.compile(r'^(sub|loc|off|unk|qword|dword|byte|word|xmmword|ymmword|stru|asc|nullsub|j|jpt|def|locret|flt|dbl|algn|'
                   r'__imp_|\?\?_[0-9A-Z]|Concurrency)[_A-Za-z0-9@?$]*$')
SUBSYSTEMS = [
    # mod.io SDK REST endpoints are global descriptors built by static initializers: '/games/{game-id}/mods/...'
    ('mod.io / user mods', r'modio|mod\.io|GameModService|ModService|\bUGC\b|ugc_|modSupport|ModSupport|'
                           r'\{(?:game|mod|user|collection|comment|file|team)-id\}|'
                           r'^/(?:games|me|users|external|metrics|oauth|agreements|general|ping|report|authenticate)\b'),
    ('callback system', r'CallbackSystem|callbackSystem|ScriptableService|gameCallback|CallbackRun'),
    ('photo mode', r'[Pp]hoto ?[Mm]ode|photomode|PhotomodeCensorship'),
    ('upscalers / frame generation', r'fidelityfx|FidelityFX|\bFSR|ffx|DLSS|XeSS|[Ss]treamline|NGX|nvsdk|[Ff]rame ?[Gg]en|'
                                     r'[Aa]nti[Ll]ag'),
    ('weapons / dual wield', r'LeftWeapon|RightWeapon|[Dd]ual[Ww]ield|[Ww]eapon'),
    ('rendering', r'[Rr]ender|[Ss]hader|[Tt]exture|D3D12|DXGI|dxgi|\bGPU|\bgpu|[Mm]esh|[Ll]ight|[Rr]ay[Tt]rac|[Mm]aterial|'
                  r'[Ss]wap[Cc]hain|[Vv]iewport|[Pp]ostFX|[Ff]og|[Ss]hadow|[Dd]enois|ReBLUR|ReLAX|Odyssey3D|HistoryFix|'
                  r'SigmaScale|DownsampleThreshold|GpuTargetTime|TargetFrameTime|AdaptToCpu|TargetTimeScale'),
    ('physics', r'physx|PhysX|[Pp]hysics|[Cc]ollision|[Rr]agdoll|psarray'),
    ('animation', r'[Aa]nim'),
    # 2.5's Wwise is built at Demute; func_strings keeps 80 chars, which cuts its paths at "external\wwi"
    ('audio', r'[Ww]wise|external\\wwi|DEMUTE-PROG|\bAk[A-Z]|\bAK::|@AK@@|\bCAk[A-Z]|[Aa]udio|[Ss]ound|[Vv]oice|[Mm]usic'),
    ('UI / ink', r'\bink[A-Z]|[Ww]idget|\bHUD\b|\bhud|[Mm]enu|[Pp]opup|[Tt]ooltip|[Mm]appin'),
    ('script VM / RTTI', r'[Ss]cript|RTTI|IScriptable|rtti|[Oo]pcode|redscript'),
    ('resources / streaming / IO', r'[Rr]esource|[Dd]epot|[Aa]rchive|[Ss]tream|redIO|[Ll]oader|Kraken|zlib|[Cc]ompress|\.xbm|'
                                   r'\.mesh|\.ent\b|\.app\b|[Ss]ector'),
    ('quests / scenes / AI', r'[Qq]uest|[Ss]cene|[Jj]ournal|\bAI\b|[Bb]ehavio|[Cc]ommunity|[Ww]orkspot|[Dd]ialog|[Bb]raindance'),
    ('vehicles', r'[Vv]ehicle|[Cc]ar[A-Z]|[Dd]riv'),
    ('online / platform / telemetry', r'https?:|curl|[Tt]elemetry|[Aa]ccount|[Ss]team|GOG|[Gg]alaxy|EOS\b|[Oo]nline|[Aa]chievement|'
                                      r'[Cc]loud|[Pp]latform|[Ee]ntitlement'),
    ('input', r'[Ii]nput|[Gg]amepad|[Kk]eyboard|[Mm]ouse|[Cc]ontroller'),
    ('save / load', r'[Ss]ave[Gg]ame|[Ss]avegame|\bsave|\bSave|[Pp]ersisten'),
    # '!append-once' & co. are the YAML tweak operators 2.5 parses natively (TweakXL's syntax)
    ('config / settings / TweakDB', r'[Cc]onfig|[Ss]etting|[Tt]weak|[Oo]ption|^!(?:append|prepend|merge|remove)|'
                                    r'InheritanceMap|ExtraFlats'),
    ('memory / jobs / core', r'[Mm]emory|[Aa]lloc|\b[Jj]ob|[Tt]hread|[Mm]utex|[Ff]iber|[Ll]ock|[Pp]ool'),
    ('gameplay (other)', r'game[A-Z]|[Pp]layer|\bnpc|NPC|[Ii]tem|[Ii]nventory|[Ss]tat[A-Z]|[Vv]endor|[Cc]yberware|[Pp]erk|'
                         r'[Ff]action|[Pp]revention|[Pp]olice|[Cc]rafting|[Ee]ffector|GameplayRestriction'),
    # last: compiler runtime and third-party code linked in (CRT, STL, ConcRT, fmt, utf8cpp, EH unwind helpers)
    ('runtime / third-party libraries', r'__crt|__acrt|_LocaleUpdate|_Locinfo|_Locimp|\bstd::|@std@@|Concurrency::|'
                                        r'@Concurrency@@|\bfmt::|@fmt@@|utf8::|`eh vector|__std_|_invalid_parameter|'
                                        r'\bterminate\b|_wcsn?icmp|_mbtowc|wctype|strtox|_errno|_doserrno'),
]
SUBSYSTEMS = [(n, re.compile(p, re.M)) for n, p in SUBSYSTEMS]   # ^ = start of one string (tag_of joins by \n)
TRIVIAL = 32                     # bytes: below this a function without any string or caller evidence is a thunk/accessor
HUB = 50                         # callers: a utility this widely used passes on no subsystem it only inherited


def tag_of(strings):
    text = '\n'.join(strings)
    for name, rx in SUBSYSTEMS:
        if rx.search(text):
            return name
    return None


# REDengine native class names carry their module as a lowercase prefix (RTTI CName), longest first.
CLASS_PREFIX = [
    ('gameinteractions', 'gameplay (other)'), ('gameui', 'UI / ink'), ('gameaudio', 'audio'), ('gamevehicle', 'vehicles'),
    ('inkanim', 'UI / ink'), ('ink', 'UI / ink'), ('quest', 'quests / scenes / AI'), ('scn', 'quests / scenes / AI'),
    ('community', 'quests / scenes / AI'), ('work', 'quests / scenes / AI'), ('AI', 'quests / scenes / AI'),
    ('ai', 'quests / scenes / AI'), ('vehicle', 'vehicles'), ('anim', 'animation'), ('audio', 'audio'),
    ('physics', 'physics'), ('phys', 'physics'), ('Px', 'physics'), ('rend', 'rendering'), ('mesh', 'rendering'),
    ('world', 'resources / streaming / IO'), ('res', 'resources / streaming / IO'), ('mp', 'online / platform / telemetry'),
    ('net', 'online / platform / telemetry'), ('save', 'save / load'), ('user', 'config / settings / TweakDB'),
    ('tweak', 'config / settings / TweakDB'), ('input', 'input'), ('sense', 'gameplay (other)'), ('ent', 'gameplay (other)'),
    ('game', 'gameplay (other)'), ('red', 'memory / jobs / core'),
]


def class_tag(cls, names=()):
    """Subsystem of a vtable's class: MSVC names by their keywords (namespaces included), RED names by module prefix,
    then the registration function's other identifiers by keyword."""
    if not cls:
        return tag_of(list(names)) if names else None
    if cls.startswith('.?A'):
        return tag_of([cls.replace('@', ' ')])
    for prefix, sub in CLASS_PREFIX:
        if cls.startswith(prefix) and cls[len(prefix):len(prefix) + 1].isupper():
            return tag_of([cls]) if sub == 'gameplay (other)' and tag_of([cls]) not in (None, 'gameplay (other)') else sub
    return tag_of([cls] + list(names))


def indirect(tags, how, context, refs, neighbours, hubs=frozenset(), rounds=6):
    """Tag functions reached only indirectly: the class of a vtable holding them, the constructor that installs that
    vtable, the function taking their address, then their (possibly just tagged) call-graph neighbours."""
    for _ in range(rounds):
        add = {}
        for k, v in refs.items():
            r = int(k, 16)
            if r in tags:
                continue
            own = v.get('name', '')
            if own and not re.match(r'(sub|j|nullsub|unknown_libname)_', own) and tag_of([own]):
                add[r] = (tag_of([own]), 'own name', own[:100])
                continue
            parent = v.get('eh_parent')
            if parent and tags.get(int(parent, 16)):
                add[r] = (tags[int(parent, 16)], 'exception funclet', 'C++ unwind funclet of %s' % parent)
                continue
            for tb in v['tables']:
                t = class_tag(tb['class'], tb.get('names', ()))
                if t:
                    add[r] = (t, 'vtable class', 'slot %d of %s vtable %s' % (tb['slot'], tb['class'] or '?', tb['table']))
                    break
            if r in add:
                continue
            for tb in v['tables']:
                users = [tags.get(int(u, 16)) for u in tb['users']]
                users = [u for u in users if u]
                if users:
                    add[r] = (collections.Counter(users).most_common(1)[0][0], 'vtable constructor',
                              'slot %d of vtable %s installed by %s' % (tb['slot'], tb['table'], ','.join(tb['users'][:3])))
                    break
            if r in add:
                continue
            took = [tags.get(int(u, 16)) for u in v['address_taken_by']]
            took = [t for t in took if t]
            if took:
                add[r] = (collections.Counter(took).most_common(1)[0][0], 'address taken',
                          'address taken by %s' % ','.join(v['address_taken_by'][:3]))
                continue
            near = [tags.get(int(u, 16)) for u in v['callers'] + neighbours.get(r, []) if int(u, 16) not in hubs]
            near = [t for t in near if t]
            if near:
                add[r] = (collections.Counter(near).most_common(1)[0][0], 'call graph via indirect',
                          'called by %s' % ','.join(v['callers'][:3]) if v['callers'] else 'calls tagged functions')
        if not add:
            break
        for r, (t, ev, ctx) in add.items():
            tags[r], how[r], context[r] = t, ev, ctx


def by_init_table(tags, how, context, tables, reach=4):
    """Dynamic initializers of global constants (strings, CNames, config vars, REST endpoints) sit in the CRT
    initializer table in translation-unit order, so one without evidence of its own takes the subsystem its tagged
    table neighbours agree on (both sides within `reach` slots, or the one side when the other has nothing)."""
    for name, entries in tables.items():
        rvas = [int(e, 16) if e else None for e in entries]
        usable = lambda r: r is not None and r in tags and not tags[r].startswith('trivial')
        for i, r in enumerate(rvas):
            if r is None or r in tags:
                continue
            before = next((rvas[j] for j in range(i - 1, max(i - reach, 0) - 1, -1) if usable(rvas[j])), None)
            after = next((rvas[j] for j in range(i + 1, min(i + reach + 1, len(rvas))) if usable(rvas[j])), None)
            pick = None
            if before is not None and after is not None:
                pick = tags[before] if tags[before] == tags[after] else None
            elif before is not None or after is not None:
                pick = tags[before if before is not None else after]
            if pick:
                tags[r], how[r] = pick, 'initializer table'
                context[r] = 'dynamic initializer, CRT table %s slot %d, next to %s' % (
                    name, i, '%x' % (before if before is not None else after))


def by_address(tags, how, context, candidates, all_rvas, reach=6):
    """Last resort: the linker keeps an object file's functions together, so a function whose nearest tagged
    neighbours on both sides (within `reach` functions, trivial ones skipped) agree takes their subsystem."""
    order = sorted(all_rvas)
    pos = {r: i for i, r in enumerate(order)}
    usable = lambda r: r in tags and not tags[r].startswith('trivial')
    for r in candidates:
        if r in tags or r not in pos:
            continue
        i = pos[r]
        before = next((order[j] for j in range(i - 1, max(i - reach, 0) - 1, -1) if usable(order[j])), None)
        after = next((order[j] for j in range(i + 1, min(i + reach + 1, len(order))) if usable(order[j])), None)
        if before is not None and after is not None and tags[before] == tags[after]:
            tags[r], how[r] = tags[before], 'address neighbours'
            context[r] = 'between %x and %x (same object file)' % (before, after)


def load_json(path):
    return json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}


def clean_names(raw):
    try:
        names = json.loads(raw) if raw else []
    except ValueError:
        return frozenset()
    return frozenset(n for n in names if isinstance(n, str) and not NOISE.match(n) and not re.search(r'[0-9A-F]{8,}', n))


def features(db):
    """rva -> (id, nodes, edges, size, instructions, mnemonics digest, meaningful names)."""
    out = {}
    con = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
    for fid, addr, nodes, edges, size, insn, mnem, names in con.execute(
            'select id, address, nodes, edges, size, instructions, mnemonics, names from functions'):
        out[int(addr) - BASE] = (fid, nodes, edges, size, insn, hashlib.md5((mnem or '').encode()).digest(), clean_names(names))
    con.close()
    return out


def constants(db, ids):
    want = set(ids)
    out = collections.defaultdict(set)
    con = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
    for fid, value in con.execute('select func_id, constant from constants'):
        if fid in want:
            out[fid].add(value)
    con.close()
    return out


def old_strings(db, rvas):
    """String literals in the 2.31 assembly of the given functions (IDA's `; "..."` comments)."""
    want = {r + BASE for r in rvas}
    out = {}
    con = sqlite3.connect('file:%s?mode=ro' % db, uri=True)
    for addr, asm in con.execute('select address, assembly from functions'):
        a = int(addr)
        if a in want:
            out[a - BASE] = re.findall(r';\s*"([^"]{3,80})', asm or '')[:24]
    con.close()
    return out


def kind(pair, fo, fn, co, cn):
    if pair['ratio'] >= 0.99995:
        return 'identical', ''
    if fo is None or fn is None:
        return 'unknown', 'not in the exports'
    detail = []
    if fo[5] == fn[5]:
        a, b = co.get(fo[0], set()), cn.get(fn[0], set())
        if a == b:
            k = 'addresses-only'
        else:
            k = 'operands'
            detail.append('consts -%s +%s' % (','.join(sorted(a - b)[:4]), ','.join(sorted(b - a)[:4])))
    elif fo[1] == fn[1] and fo[2] == fn[2]:
        k = 'instructions'
        detail.append('insn %+d' % (fn[4] - fo[4]))
    else:
        grow = abs(fn[3] - fo[3]) / max(fo[3], fn[3], 1)
        k = 'control-flow' if pair['ratio'] >= 0.5 and grow <= 0.25 else 'rewritten'
        detail.append('bb %d->%d size %d->%d' % (fo[1], fn[1], fo[3], fn[3]))
    gone, new = fo[6] - fn[6], fn[6] - fo[6]
    if gone or new:
        detail.append('names -%s +%s' % (','.join(sorted(gone)[:3]), ','.join(sorted(new)[:3])))
    return k, '; '.join(detail)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--diff', default=os.path.join(PD, 'diaphora-2.5'))
    ap.add_argument('--old', default=os.path.join(PD, 'ida', '2.31', 'cp2077_2.31.sqlite'))
    ap.add_argument('--new', default=os.path.join(PD, 'ida', '2.5', 'cp2077_2.5.sqlite'))
    a = ap.parse_args()
    pairs = []
    with open(os.path.join(a.diff, 'matched.tsv'), encoding='utf-8') as f:
        head = f.readline().rstrip('\n').split('\t')
        for line in f:
            r = dict(zip(head, line.rstrip('\n').split('\t')))
            r['old'], r['new'], r['ratio'] = int(r['old'], 16), int(r['new'], 16), float(r['ratio'])
            pairs.append(r)
    def unmatched(name):
        out = []
        for line in open(os.path.join(a.diff, name), encoding='utf-8'):
            p = line.rstrip('\n').split('\t')
            if p and re.fullmatch(r'0x[0-9a-f]+', p[0]):
                out.append((int(p[0], 16), p[3] if len(p) > 3 else ''))
        return out
    added, removed = unmatched('unmatched_new.tsv'), unmatched('unmatched_old.tsv')
    print('pairs %d, added %d, removed %d' % (len(pairs), len(added), len(removed)), flush=True)
    fo, fn = features(a.old), features(a.new)
    print('features: old %d, new %d' % (len(fo), len(fn)), flush=True)
    changed = [p for p in pairs if p['ratio'] < 0.99995]
    co = constants(a.old, [fo[p['old']][0] for p in changed if p['old'] in fo])
    cn = constants(a.new, [fn[p['new']][0] for p in changed if p['new'] in fn])
    print('constants loaded for %d changed pairs' % len(changed), flush=True)
    strings = {int(k, 16): v for k, v in json.load(open(os.path.join(a.diff, 'func_strings_2.5.json'), encoding='utf-8')).items()}
    callees = {int(k, 16): [int(x, 16) for x in v] for k, v in json.load(open(os.path.join(a.diff, 'func_callees_2.5.json'))).items()}
    callers = collections.defaultdict(list)
    for f_, cs in callees.items():
        for c in cs:
            callers[c].append(f_)
    direct = {r: t for r, s in strings.items() if (t := tag_of(s))}
    tags, how = dict(direct), {r: 'strings' for r in direct}
    for hop in (1, 2, 3, 4):
        add = {}
        for r in fn:
            if r in tags:
                continue
            votes = collections.Counter(tags[x] for x in callees.get(r, []) + callers.get(r, [])
                                        if x in tags and (x in direct or len(callers.get(x, ())) <= HUB))
            if votes:
                add[r] = votes.most_common(1)[0][0]
        for r, t in add.items():
            tags[r], how[r] = t, 'call graph (%d hop%s)' % (hop, 's' if hop > 1 else '')
    # what is left: by the imported APIs it names, else tiny bodies (thunks, getters, setters, adjustors)
    for r, f_ in fn.items():
        if r in tags:
            continue
        t = tag_of(sorted(f_[6]))
        if t:
            tags[r], how[r] = t, 'names'
        elif f_[3] <= TRIVIAL:
            tags[r], how[r] = 'trivial (<= %d bytes)' % TRIVIAL, 'size'
    removed_strings = old_strings(a.old, [r for r, _ in removed])
    print('tagged %d of %d new functions; removed with strings %d' % (len(tags), len(fn),
          sum(1 for v in removed_strings.values() if v)), flush=True)
    # functions only reached through vtables / taken addresses (IDA: ida/scripts/refs_of.py over what is left)
    context = {}
    refs_new, refs_old, callees_old = (load_json(os.path.join(a.diff, n)) for n in (
        'refs_unattributed_2.5.json', 'refs_unattributed_2.31.json', 'callees_removed_2.31.json'))
    if refs_new:
        before = len(tags)
        hubs = frozenset(r for r in fn if len(callers.get(r, ())) > HUB and r not in direct)
        near = {int(k, 16): ['%x' % x for x in callees.get(int(k, 16), [])] for k in refs_new}
        indirect(tags, how, context, refs_new, near, hubs)
        for _ in range(3):   # table neighbours tagged in one pass make the next slot decidable
            by_init_table(tags, how, context, load_json(os.path.join(a.diff, 'init_table_2.5.json')).get('tables', {}))
        by_address(tags, how, context, [int(k, 16) for k in refs_new], fn)
        indirect(tags, how, context, refs_new, near, hubs)
        print('indirect evidence tagged %d more new functions' % (len(tags) - before), flush=True)
    removed_tag = {}
    for r, _ in removed:
        s = removed_strings.get(r, [])
        removed_tag[r] = (tag_of(s), 'strings') if tag_of(s) else ((tag_of(sorted(fo[r][6])), 'names') if r in fo and tag_of(sorted(fo[r][6]))
                                                                   else (('trivial (<= %d bytes)' % TRIVIAL, 'size') if r in fo and fo[r][3] <= TRIVIAL
                                                                         else ('unattributed', '')))
    old_tags = {p['old']: tags[p['new']] for p in pairs if p['new'] in tags}
    old_tags.update({r: t for r, (t, _) in removed_tag.items() if t != 'unattributed'})
    old_how, old_context = {}, {}
    if refs_old:
        indirect(old_tags, old_how, old_context, refs_old, {int(k, 16): v for k, v in callees_old.items()})
        by_address(old_tags, old_how, old_context, [r for r, (t, _) in removed_tag.items() if t == 'unattributed'], fo)
        indirect(old_tags, old_how, old_context, refs_old, {int(k, 16): v for k, v in callees_old.items()})
        for r, (t, ev) in removed_tag.items():
            if t == 'unattributed' and r in old_how:
                removed_tag[r] = (old_tags[r], old_how[r] + ' (2.31)')
    # what no rule reaches was read function by function (Hex-Rays) and judged by hand: review_unattributed.tsv
    review = os.path.join(a.diff, 'review_unattributed.tsv')
    if os.path.exists(review):
        for line in open(review, encoding='utf-8').read().splitlines()[1:]:
            rva, build, _, sub, verdict = line.split('\t')
            r = int(rva, 16)
            if build == '2.5' and r not in tags:
                tags[r], how[r], context[r] = sub, 'manual review', verdict
            elif build == '2.31' and removed_tag.get(r, ('', ''))[0] == 'unattributed':
                removed_tag[r] = (sub, 'manual review (2.31)')
                old_context[r] = verdict
    rows = []
    for p in pairs:
        k, detail = kind(p, fo.get(p['old']), fn.get(p['new']), co, cn)
        rows.append(dict(status='matched', type=p['type'], old='%x' % p['old'], new='%x' % p['new'], ratio='%.4f' % p['ratio'],
                         kind=k, detail=detail, subsystem=tags.get(p['new'], 'unattributed'), evidence=how.get(p['new'], ''),
                         strings=' | '.join(strings.get(p['new'], [])[:3]), used=p.get('used', ''), name=p.get('name', ''),
                         context=context.get(p['new'], '')))
    for r, name in added:
        rows.append(dict(status='added', type='', old='', new='%x' % r, ratio='', kind='added',
                         detail='size %d' % fn[r][3] if r in fn else '', subsystem=tags.get(r, 'unattributed'),
                         evidence=how.get(r, ''), strings=' | '.join(strings.get(r, [])[:3]), used='', name=name,
                         context=context.get(r, '')))
    for r, name in removed:
        s = removed_strings.get(r, [])
        sub, ev = removed_tag[r]
        rows.append(dict(status='removed', type='', old='%x' % r, new='', ratio='', kind='removed',
                         detail='size %d' % fo[r][3] if r in fo else '', subsystem=sub,
                         evidence=ev, strings=' | '.join(s[:3]), used='', name=name, context=old_context.get(r, '')))
    cols = ['status', 'type', 'old', 'new', 'ratio', 'kind', 'detail', 'subsystem', 'evidence', 'strings', 'used', 'name',
            'context']
    with open(os.path.join(a.diff, 'functions.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\t'.join(cols) + '\n')
        for r in rows:
            f.write('\t'.join(re.sub(r'[\t\r\n\x0b\x0c\x1c-\x1e\x85  ]', ' ', str(r[c])) for c in cols) + '\n')
    kinds = ['identical', 'addresses-only', 'operands', 'instructions', 'control-flow', 'rewritten', 'added', 'removed', 'unknown']
    by = collections.defaultdict(collections.Counter)
    for r in rows:
        by[r['subsystem']][r['kind']] += 1
    md = ['# 2.31 -> 2.5: every function classified (%d rows)' % len(rows), '',
          'functions.tsv has one row per function of both builds (Diaphora match, change kind, subsystem and the',
          'evidence for it). Kinds and subsystems are defined in tools/patchday/diaphora_classify.py.', '',
          '## change kinds', '', '| kind | functions |', '|---|---|']
    total = collections.Counter(r['kind'] for r in rows)
    md += ['| %s | %d |' % (k, total[k]) for k in kinds if total[k]]
    md += ['', '## by subsystem', '', '| subsystem | ' + ' | '.join(kinds[:8]) + ' |', '|---' * 9 + '|']
    for sub in sorted(by, key=lambda s: -sum(by[s].values())):
        md.append('| %s | ' % sub + ' | '.join(str(by[sub][k]) for k in kinds[:8]) + ' |')
    attributed = sum(1 for r in rows if r['subsystem'] != 'unattributed')
    ev = collections.Counter(r['evidence'].split(' (')[0] or 'none' for r in rows)
    md += ['', 'attributed: %d of %d (%s)' % (attributed, len(rows), ', '.join('%s %d' % kv for kv in ev.most_common()))]
    changed_un = [r for r in rows if r['subsystem'] == 'unattributed' and r['kind'] not in ('identical', 'addresses-only')]
    still = collections.Counter(r['kind'] for r in rows if r['subsystem'] == 'unattributed')
    md += ['', 'Changed (not identical / addresses-only) and still without a subsystem: %d. Without a subsystem at all: %d, '
           'by kind %s.' % (len(changed_un), sum(still.values()), dict(still)),
           'The functions no rule reached were read one by one from their Hex-Rays code: review_unattributed.md /',
           'review_unattributed.tsv (evidence "manual review").']
    used = [r for r in rows if r['used']]
    md += ['', '## functions the port or a framework uses (%d)' % len(used), '', '| kind | functions |', '|---|---|']
    md += ['| %s | %d |' % kv for kv in collections.Counter(r['kind'] for r in used).most_common()]
    md += ['', '### rewritten or control-flow changed among them', '', '| old | new | ratio | kind | used by |', '|---|---|---|---|---|']
    md += ['| %s | %s | %s | %s | %s |' % (r['old'], r['new'], r['ratio'], r['kind'], r['used'][:90]) for r in
           sorted(used, key=lambda r: float(r['ratio'] or 1)) if r['kind'] in ('rewritten', 'control-flow')]
    for sub in sorted(by, key=lambda s: -sum(by[s].values())):
        if sub == 'unattributed':
            continue
        new = sorted((r for r in rows if r['subsystem'] == sub and r['kind'] == 'added'),
                     key=lambda r: -int((r['detail'].split() or ['', '0'])[-1] or 0))[:12]
        big = sorted((r for r in rows if r['subsystem'] == sub and r['kind'] == 'rewritten'), key=lambda r: float(r['ratio']))[:8]
        md += ['', '## %s' % sub, '', 'added %d, removed %d, rewritten %d, control-flow %d, operands %d, addresses-only %d' % (
            by[sub]['added'], by[sub]['removed'], by[sub]['rewritten'], by[sub]['control-flow'], by[sub]['operands'],
            by[sub]['addresses-only'])]
        if new:
            md += ['', 'largest added:'] + ['- sub_%x (%s) %s' % (int(r['new'], 16) + BASE, r['detail'], r['strings'][:110]) for r in new]
        if big:
            md += ['', 'most rewritten:'] + ['- sub_%x -> sub_%x ratio %s %s %s' % (
                int(r['old'], 16) + BASE, int(r['new'], 16) + BASE, r['ratio'], r['detail'][:60], r['strings'][:80]) for r in big]
    open(os.path.join(a.diff, 'CLASSIFIED.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    print('kinds:', dict(total))


if __name__ == '__main__':
    main()
