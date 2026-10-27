"""The function a code address belongs to, in a build: its .pdata entry and, for a chunk, the primary function the
chunk's chained unwind info points to; plus that function's address-library hashes.

    py -3.13 -B tools/patchday/func_start.py 0x2234682 [0x...] [--game build/patchday/base-2.5]
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from pe import AddressLibrary, Image, find_game_files  # noqa: E402


def primary(img, rva):
    """Follow UNW_FLAG_CHAININFO from the .pdata entry covering rva to the function's first entry."""
    f = img.function_at(rva)
    seen = 0
    while f is not None and seen < 16:
        begins, ends, unwinds = img.pdata
        import numpy as np
        i = int(np.searchsorted(begins, f[0], side='right')) - 1
        uw = int(unwinds[i])
        hdr = img.read(uw, 4)
        flags = hdr[0] >> 3
        count = hdr[2]
        if not flags & 0x4:  # UNW_FLAG_CHAININFO
            return f
        off = uw + 4 + ((count + 1) & ~1) * 2
        b, e, _ = struct.unpack('<III', img.read(off, 12))
        f = (b, e)
        seen += 1
    return f


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rvas', nargs='+')
    ap.add_argument('--game', default=os.path.join(REPO, 'build', 'patchday', 'base-2.5'))
    a = ap.parse_args()
    exe, lib = find_game_files(a.game)
    img = Image(exe)
    L = AddressLibrary(lib, img)
    for r in a.rvas:
        rva = int(r, 16)
        chunk = img.function_at(rva)
        p = primary(img, rva)
        print(f'{rva:#x}: chunk {chunk[0]:#x}-{chunk[1]:#x}' if chunk else f'{rva:#x}: no pdata',
              f'-> function {p[0]:#x} hashes {L.exact(p[0])}' if p else '')


if __name__ == '__main__':
    main()
