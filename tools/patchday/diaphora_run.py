"""Diaphora on the whole game exe, in parallel: the export of 300k functions with the decompiler is hours in
one IDA, so the database is copied N times, each copy exports one address range (Diaphora's own
DIAPHORA_FROM_ADDRESS / DIAPHORA_TO_ADDRESS), and the parts are merged into one export database.

    py -3.13 -B tools/patchday/diaphora_run.py export --idb <x.i64> --exe <x.exe> --out <x.sqlite> [--parts 7]
    py -3.13 -B tools/patchday/diaphora_run.py merge --out <x.sqlite> <part.sqlite>...
    py -3.13 -B tools/patchday/diaphora_run.py diff --old <old.sqlite> --new <new.sqlite> --out <diff.diaphora>

The ranges are balanced by the bytes of the .pdata root functions in .text. Merging offsets every table's ids
per part (functions, instructions, basic blocks and everything that points at them); the program row's
call-graph primes are recomputed over all functions the way Diaphora does it; structures and type libraries
come from the first part only (every part exports the same ones).
"""
import argparse
import decimal
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import Image  # noqa: E402

IDAT = r'C:\Program Files\IDA Professional 9.2\idat.exe'
DIAPHORA = os.path.join(REPO, 'build', 'patchday', 'tools', 'diaphora-3.4.4')
IMAGE_BASE = 0x140000000

# Table -> (id column, {foreign key column: referenced table}).
TABLES = {
    'functions': {},
    'instructions': {'func_id': 'functions'},
    'basic_blocks': {},
    'bb_relations': {'parent_id': 'basic_blocks', 'child_id': 'basic_blocks'},
    'bb_instructions': {'basic_block_id': 'basic_blocks', 'instruction_id': 'instructions'},
    'function_bblocks': {'function_id': 'functions', 'basic_block_id': 'basic_blocks'},
    'callgraph': {'func_id': 'functions'},
    'constants': {'func_id': 'functions'},
    'compilation_units': {},
    'compilation_unit_functions': {'cu_id': 'compilation_units', 'func_id': 'functions'},
}


def ranges(exe, parts):
    img = Image(exe)
    begins, ends, unwind = img.pdata
    text_lo, text_hi = img.text.va, img.text.va + img.text.vsize
    funcs = sorted((int(b), int(e)) for b, e in zip(begins, ends) if text_lo <= b < text_hi)
    total = sum(e - b for b, e in funcs)
    cuts, acc, k = [text_lo], 0, 1
    for b, e in funcs:
        if acc >= total * k / parts and b > cuts[-1]:
            cuts.append(b)
            k += 1
        acc += e - b
    cuts.append(text_hi)
    return [(IMAGE_BASE + lo, IMAGE_BASE + hi) for lo, hi in zip(cuts, cuts[1:])]


def export(args):
    work = os.path.join(os.path.dirname(os.path.abspath(args.out)), 'diaphora_parts')
    os.makedirs(work, exist_ok=True)
    rs = ranges(args.exe, args.parts)
    procs = []
    for k, (lo, hi) in enumerate(rs):
        d = os.path.join(work, f'p{k}')
        os.makedirs(d, exist_ok=True)
        idb = os.path.join(d, f'p{k}.i64')
        part = os.path.join(d, f'p{k}.sqlite')
        if os.path.exists(part) and not os.path.exists(part + '-crash') and args.resume:
            print(f'part {k}: {lo:#x}-{hi:#x} already exported')
            continue
        if not os.path.exists(idb):
            shutil.copy2(args.idb, idb)
        env = dict(os.environ, DIAPHORA_AUTO='1', DIAPHORA_EXPORT_FILE=part,
                   DIAPHORA_FROM_ADDRESS=f'{lo:#x}', DIAPHORA_TO_ADDRESS=f'{hi:#x}')
        if not args.no_decompiler:
            env['DIAPHORA_USE_DECOMPILER'] = '1'
        cmd = [IDAT, '-A', f'-L{os.path.join(d, "ida.log")}', f'-S{os.path.join(DIAPHORA, "diaphora.py")}', idb]
        print(f'part {k}: {lo:#x}-{hi:#x}', flush=True)
        procs.append((k, subprocess.Popen(cmd, env=env, cwd=d)))
    t = time.monotonic()
    for k, p in procs:
        code = p.wait()
        print(f'part {k} exit {code} after {time.monotonic() - t:.0f} s', flush=True)
    parts = [os.path.join(work, f'p{k}', f'p{k}.sqlite') for k in range(len(rs))]
    bad = [p for p in parts if not os.path.exists(p) or os.path.exists(p + '-crash')]
    if bad:
        sys.exit(f'parts not exported: {bad}')
    merge_parts(args.out, parts)


def merge_parts(out, parts):
    if os.path.exists(out):
        os.remove(out)
    shutil.copy2(parts[0], out)
    db = sqlite3.connect(out)
    db.execute('PRAGMA journal_mode = OFF')
    db.execute('PRAGMA synchronous = OFF')
    for part in parts[1:]:
        db.execute('ATTACH DATABASE ? AS part', (part,))
        off = {t: db.execute(f'select coalesce(max(id), 0) from main.{t}').fetchone()[0] for t in TABLES}
        # A function next to a range cut can be exported by both neighbours: the first part's copy wins, and the
        # later copy is dropped with its instructions, basic blocks and everything hanging off them.
        db.execute('create temp table skip_funcs as select id from part.functions '
                   'where rva in (select rva from main.functions)')
        db.execute('create temp table skip_bbs as select basic_block_id as id from part.function_bblocks '
                   'where function_id in (select id from skip_funcs)')
        skipped = db.execute('select count(*) from skip_funcs').fetchone()[0]
        keep = {
            'functions': 'id not in (select id from skip_funcs)',
            'instructions': 'func_id not in (select id from skip_funcs)',
            'callgraph': 'func_id not in (select id from skip_funcs)',
            'constants': 'func_id not in (select id from skip_funcs)',
            'function_bblocks': 'function_id not in (select id from skip_funcs)',
            'compilation_unit_functions': 'func_id not in (select id from skip_funcs)',
            'basic_blocks': 'id not in (select id from skip_bbs)',
            'bb_relations': 'parent_id not in (select id from skip_bbs)',
            'bb_instructions': 'basic_block_id not in (select id from skip_bbs)',
        }
        for table, fks in TABLES.items():
            cols = [r[1] for r in db.execute(f'PRAGMA main.table_info({table})')]
            sel = []
            for c in cols:
                if c == 'id':
                    sel.append(f'id + {off[table]}')
                elif c in fks:
                    sel.append(f'{c} + {off[fks[c]]}')
                else:
                    sel.append(c)
            # A compilation unit can straddle two ranges: the first part's row wins, its functions follow it.
            verb = 'insert or ignore' if table == 'compilation_units' else 'insert'
            where = f' where {keep[table]}' if table in keep and skipped else ''
            db.execute(f'{verb} into main.{table} ({", ".join(cols)}) select {", ".join(sel)} from part.{table}{where}')
        db.execute('drop table skip_funcs')
        db.execute('drop table skip_bbs')
        db.commit()
        db.execute('DETACH DATABASE part')
        if skipped:
            print(f'{os.path.basename(part)}: {skipped} functions already exported by an earlier part', flush=True)
        n = db.execute('select count(*) from functions').fetchone()[0]
        print(f'merged {os.path.basename(part)}: {n} functions', flush=True)
    db.execute('delete from compilation_unit_functions where cu_id not in (select id from compilation_units)')
    primes, counts = 1, {}
    for (v,) in db.execute('select primes_value from functions'):
        primes *= decimal.Decimal(v)
        counts[v] = counts.get(v, 0) + 1
    db.execute('update program set callgraph_primes = ?, callgraph_all_primes = ?', (str(primes), json.dumps(counts)))
    db.commit()
    db.close()
    print('wrote', out)


def diff(args):
    env = dict(os.environ, DIAPHORA_AUTO_DIFF='1', DIAPHORA_DB1=args.old, DIAPHORA_DB2=args.new,
               DIAPHORA_DIFF_OUT=args.out, DIAPHORA_LOG_PRINT='1')
    sys.exit(subprocess.call([sys.executable, '-B', os.path.join(DIAPHORA, 'diaphora.py')], env=env, cwd=DIAPHORA))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('export')
    e.add_argument('--idb', required=True)
    e.add_argument('--exe', required=True)
    e.add_argument('--out', required=True)
    e.add_argument('--parts', type=int, default=7)
    e.add_argument('--no-decompiler', action='store_true')
    e.add_argument('--resume', action='store_true', help='keep parts already exported')
    m = sub.add_parser('merge')
    m.add_argument('--out', required=True)
    m.add_argument('parts', nargs='+')
    d = sub.add_parser('diff')
    d.add_argument('--old', required=True)
    d.add_argument('--new', required=True)
    d.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.cmd == 'export':
        export(a)
    elif a.cmd == 'merge':
        merge_parts(a.out, a.parts)
    else:
        diff(a)


if __name__ == '__main__':
    main()
