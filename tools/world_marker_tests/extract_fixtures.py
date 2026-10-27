"""Extract the native 2.5 ink VS/PS pairs used by the WARP parity/link tests."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'patchday'))
from shader_pairs import entries, read, technique_records

DIGESTS = ['016ad194189b8351b5af0bdffe347d12', '22b065573ec4c7638ee3f3a7c0329b9b',
           '4fe45679985b38494ad6cf1ec82ec2c8', '3f0e72c5318fe2f6418669e1e9c25d9b',
           '5b31e86262dc371203bd2d2019afd634', 'ad3045795b73d03c25c785e61f68044e']

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    index = {key[:16]: (offset, size) for key, offset, size in entries(args.cache)}
    records = technique_records(args.cache)
    found = {}
    for key, (offset, size) in index.items():
        if size not in (3042, 3442, 3350, 3302):
            continue
        blob = read(args.cache, offset, size)
        digest = blob[4:20].hex()
        if digest in DIGESTS:
            variant = DIGESTS.index(digest)
            name, pair = next((name, pair) for name, pair in records.items() if pair[0] == key)
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / f'{variant}.vs.dxil').write_bytes(blob)
            (args.output / f'{variant}.ps.dxil').write_bytes(read(args.cache, *index[pair[1]]))
            found[variant] = name
    assert len(found) == len(DIGESTS), found
    (args.output / 'manifest.json').write_text(json.dumps(found, indent=2), encoding='utf-8')
    print(json.dumps(found, indent=2))
