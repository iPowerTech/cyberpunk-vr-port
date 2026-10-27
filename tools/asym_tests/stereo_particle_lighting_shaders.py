"""2.5 shared particle lighting: allocate a tile when either eye sees the particle.

ParticleTriangles.rdc, 2026-10-23: allocator event 42607 rejects particle 109 on
the left eye's Hi-Z test (particle depth 57.09, occluder 52.31), without testing
the right eye. The right-eye draw 45413 still renders it. Its rejected LOD 6 is
then decoded by the vertex shader as a 128x128 atlas tile at the origin, covering
unrelated particles' lighting and producing bright rectangular/triangular shards.

The native stereo loop requires BOTH eyes' visibility tests to pass. Change it
to ANY: stop and allocate after a visible eye; continue to the second eye after
an occluded one; reject only after both fail. The mono branch, union-frustum
test, LOD calculation, allocator and lighting evaluation are unchanged.

The original comes from engine/staticshader_final.cache. Pass a directory of
original <FNV-1a>.dxil files from extract_shader_cache.py with --shaders. Output
belongs in src/Shaders/stereo_particle_lighting (runtime group 11).
"""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

KEY = 0xe4c84ae0e5616f91


def either_eye(ir):
    # %1191: this eye's nearest particle depth < conservative Hi-Z depth.
    # %1192: advance the eye loop; %1195: exit and select why it stopped.
    # %1196: true when the loop stopped early, false after testing both eyes.
    # %1246 allocates a LOD; %1261 joins the rejected-LOD (6) paths.
    if ir.count('%1196 = phi i1 [ true, %1009 ], [ false, %1192 ]') != 1:
        raise ValueError('Unexpected stereo visibility loop exit')
    for old, new in [
        ('br i1 %1191, label %1192, label %1195',
         'br i1 %1191, label %1195, label %1192'),
        ('br i1 %1196, label %1261, label %1246',
         'br i1 %1196, label %1246, label %1261'),
    ]:
        if ir.count(old) != 1:
            raise ValueError('Unexpected particle allocator: ' + old)
        ir = ir.replace(old, new)
    return ir


def build(original):
    if ham.fnv1a(original) != KEY:
        raise ValueError('Unsupported particle allocator; expected %016x' % KEY)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = ham.assemble(either_eye(ham.disassemble(original, work)), work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('DXIL validator output hash mismatch')
    return container


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    original = (args.shaders / ('%016x.dxil' % KEY)).read_bytes()
    container = build(original)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / ('%016x.dxil' % KEY)).write_bytes(container)
    print('Validated %016x: %d -> %d bytes' % (KEY, len(original), len(container)))


if __name__ == '__main__':
    main()
