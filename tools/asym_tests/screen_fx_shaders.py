"""Engine screen effects that move the picture, made to keep their look without moving it (2026-10-18, user:
"убрать все distortions/разрывы картинки ... sandevistan/berserk" and "уникальную фишку эффекта нужно
оставить но чтобы в VR это не ломало").

The asset-side effects are edited in build/distortion-fx-20261018/edit_distort.py; the Sandevistan's look is
no asset but a static engine shader, the GameplayPostFX node's `m_postfx_CPSandevistan` (staticshader_final.
cache). Its pixel shader, from the dxc disassembly:

    I          the effect's intensity (b0 row 0.y through a curve; x0.35 when b6.x != 0)
    uv         the pixel on the screen, d = uv - 0.5, r2 = dot(d, d)
    blocks     random screen blocks get an offset o (up to 0.02 * |d|^2 * I): the tearing
    radial     20 samples at uv + o + k_i * d * r2, k_i = -0.0325 * I * i (i = 0..19), averaged: a barrel
               smear from the screen's middle that grows to the edges -- the "zoom" blur
    look       the average tinted teal (R x (0.88 - 0.53 I), G x 0.91, B), a cyan grid on what moves
               (motion vectors at the warped uv, dark areas only), brightened and vignetted from the middle,
               then a saturation boost

Each eye draws it around its own screen middle, which under the asymmetric eye windows is a different world
direction per eye, so the smear and the block offsets land differently in the two eyes. The twin sets the
barrel step and the block offsets to 0: all 20 samples read the pixel itself (the same colour the average had
at the screen's middle), the grid reads the motion at the pixel, and everything the look is made of stays.

Output: <FNV-1a of the original>.dxil signed containers for `screen_fx\\` (Render/HiddenAreaShaders.cpp,
CyberpunkVR_ScreenFxPatch).

  py -3.13 -B screen_fx_shaders.py --cache "...\\engine\\staticshader_final.cache" --out DIR"""
import argparse
import re
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

STAGES = {'vs': 1, 'ps': 0}


def program_kind(container):
    count, = struct.unpack_from('<I', container, 28)
    for i in range(count):
        off, = struct.unpack_from('<I', container, 32 + 4 * i)
        if container[off:off + 4] == b'DXIL':
            return struct.unpack_from('<I', container, off + 8)[0] >> 16
    return None


def technique_shaders(cache, name):
    """The shaders of a static technique. The cache opens with its technique table, in which each entry ENDS
    with its name (a length byte 0x80 | n, then n characters); its stage slots (u32, then 8-byte shader keys)
    follow the previous entry's name. The records follow the table: key, u32 size, the container as the
    engine hands it to D3D12."""
    records = cache.find(b'DXBC') - 12
    names = []
    for m in re.finditer(rb'[\x80-\xbf]m_', cache[:records]):
        n = cache[m.start()] & 0x3f
        text = cache[m.start() + 1:m.start() + 1 + n]
        if re.fullmatch(rb'm_[A-Za-z0-9_]+', text):
            names.append((m.start() + 1 + n, text.decode()))
    at = [i for i, (_, text) in enumerate(names) if text == name]
    if len(at) != 1:
        raise RuntimeError('technique %s found %d times' % (name, len(at)))
    start = names[at[0] - 1][0] + 4
    shaders = {}
    for slot in range(4):
        key = cache[start + 8 * slot:start + 8 * slot + 8]
        if key == bytes(8):
            continue
        for m in re.finditer(re.escape(key), cache[records:]):
            record = records + m.start()
            size, = struct.unpack_from('<I', cache, record + 8)
            container = cache[record + 12:record + 12 + size]
            if container[:4] == b'DXBC' and struct.unpack_from('<I', container, 24)[0] == size:
                shaders[program_kind(container)] = container
                break
    return shaders


def sandevistan(ir):
    # The barrel step of the radial smear: k_i = -0.0325 * I * i.
    step = re.compile(r'(?m)^(  %\d+ = fmul fast float %\d+, )0x3FA0A3D700000000$')
    if len(step.findall(ir)) != 1:
        raise RuntimeError('radial step: expected one match')
    ir = step.sub(r'\g<1>0.000000e+00', ir)
    # The block offsets: two phis that take the offset from the glitch block and 0 from the entry block.
    offset = re.compile(r'(?m)^(  %\d+ = phi float \[ )%\d+(, %\d+ \], \[ 0\.000000e\+00, %0 \])$')
    if len(offset.findall(ir)) != 2:
        raise RuntimeError('block offsets: expected two phis')
    return offset.sub(r'\g<1>0.000000e+00\g<2>', ir)


RECIPES = {'m_postfx_CPSandevistan': ('ps', sandevistan)}


def build(original, recipe):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = ham.assemble(recipe(ham.disassemble(original, work)), work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    return container


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cache', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cache = args.cache.read_bytes()
    failed = 0
    for name, (stage, recipe) in RECIPES.items():
        try:
            original = technique_shaders(cache, name)[STAGES[stage]]
            container = build(original, recipe)
        except Exception as e:
            failed += 1
            print('FAIL %s: %s' % (name, str(e)[-600:]))
            continue
        key = ham.fnv1a(original)
        (args.out / ('%016x.dxil' % key)).write_bytes(container)
        print('OK %s %s %016x: %d -> %d bytes' % (name, stage, key, len(original), len(container)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
