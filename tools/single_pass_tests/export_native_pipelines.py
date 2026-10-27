"""Export exact root/PSO creation descriptions in one CPU-only capture read."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/rdc_static'), str(ROOT / 'tools/quickboot_tests')]
import launch_to_save as boot
import rdc_static_mcp as static


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if boot.process_ids('Cyberpunk2077.exe') or boot.process_ids('qrenderdoc.exe'):
        raise RuntimeError('Close game and GUI capture before export')
    a.out.mkdir(parents=True, exist_ok=True)
    rd = static.rd()
    rows = []
    try:
        _, sd = static.capture(str(a.capture))

        def value(node):
            kind = node.type.basetype
            if kind == rd.SDBasic.Buffer:
                data = bytes(sd.buffers[node.AsInt()])
                sha = hashlib.sha256(data).hexdigest()
                path = a.out / (sha + '.bin')
                if not path.exists():
                    path.write_bytes(data)
                return dict(sha256=sha, bytes=len(data), file=path.name)
            if kind == rd.SDBasic.Array:
                return [value(node.GetChild(i)) for i in range(node.NumChildren())]
            if node.NumChildren():
                children = [node.GetChild(i) for i in range(node.NumChildren())]
                if len({c.name for c in children}) != len(children):
                    raise ValueError('Duplicate structured field names: ' + node.name)
                return {c.name: value(c) for c in children}
            if kind == rd.SDBasic.Resource:
                return int(node.AsResourceId())
            return static.field_value(node)

        for index, chunk in enumerate(sd.chunks):
            if 'Create' in chunk.name and ('Pipeline' in chunk.name or 'RootSignature' in chunk.name):
                rows.append(dict(chunk=index, name=chunk.name, fields=value(chunk)))
        output = dict(capture=str(a.capture.resolve()), captureBytes=a.capture.stat().st_size, creations=rows)
        (a.out / 'index.json').write_text(json.dumps(output, indent=2) + '\n')
        print(json.dumps(dict(creations=len(rows), out=str(a.out / 'index.json'))))
    finally:
        print(static.rdc_close())


if __name__ == '__main__':
    main()
