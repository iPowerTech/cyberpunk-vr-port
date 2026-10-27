"""Hidden-area shader patches (docs/hidden-area-mask-20261009.md).

Each recipe takes the dxc disassembly of one engine shader and makes it skip the work for pixels
outside the visible ellipse, in the way the engine already skips pixels that need nothing (sky,
rough surfaces): the outputs stay defined. The visibility test is shared:

    x, y = the pixel's NDC with y down
    a = x < 0 ? AL : AR,  b = y < 0 ? BU : BD
    visible = a*x*x + b*y*y <= 1

AL..BD are four float placeholders in [1, 2), stored as P = 1 + A/8. The plugin rewrites them in
place when it builds the pipeline (src/Render/DxilPatch.cpp), so the slider and per-eye FOV need no
new shader build. Output: <original FNV-1a>.dxil = signed container + trailer
'HAM1', u32 count, u32 bit offset per placeholder.

  py -3.13 -B ham_shaders.py --shaders DIR_WITH_ORIGINAL_DXIL --out OUT_DIR
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import dxil_container as dc

ROOT = Path(__file__).resolve().parents[2]
DXC_DIR = Path(r'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64')
DXIL_TOOL = ROOT / 'build/single-pass-tests/Release/dxil_tool.exe'
PLACEHOLDERS = (1.2345678, 1.3456789, 1.4567891, 1.5678912)   # AL, AR, BU, BD


def lit(value):
    """LLVM IR literal of a float32 value."""
    value, = struct.unpack('<f', struct.pack('<f', value))
    return '0x%016X' % struct.unpack('>Q', struct.pack('>d', value))[0]


def visible(prefix, x, y):
    """IR lines defining %{prefix}.vis (i1) from NDC values x and y (y down)."""
    p = '%' + prefix
    return [
        f'  {p}.xx = fmul fast float {x}, {x}',
        f'  {p}.yy = fmul fast float {y}, {y}',
        f'  {p}.xn = fcmp fast olt float {x}, 0.000000e+00',
        f'  {p}.yn = fcmp fast olt float {y}, 0.000000e+00',
        f'  {p}.pa = select i1 {p}.xn, float {lit(PLACEHOLDERS[0])}, float {lit(PLACEHOLDERS[1])}',
        f'  {p}.pb = select i1 {p}.yn, float {lit(PLACEHOLDERS[2])}, float {lit(PLACEHOLDERS[3])}',
        f'  {p}.a0 = fadd fast float {p}.pa, -1.000000e+00',
        f'  {p}.b0 = fadd fast float {p}.pb, -1.000000e+00',
        f'  {p}.a = fmul fast float {p}.a0, 8.000000e+00',
        f'  {p}.b = fmul fast float {p}.b0, 8.000000e+00',
        f'  {p}.ax = fmul fast float {p}.a, {p}.xx',
        f'  {p}.by = fmul fast float {p}.b, {p}.yy',
        f'  {p}.d = fadd fast float {p}.ax, {p}.by',
        f'  {p}.vis = fcmp fast ole float {p}.d, 1.000000e+00',
    ]


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('expected exactly one match for: ' + old.strip()[:100])
    return text.replace(old, new)


def function_body(ir):
    start = ir.index('define void @')
    end = ir.index('\n}\n', start)
    return start, end


def skip_after(ir, anchor, x, y, skip, prelude=()):
    """Split the entry block right after the line `anchor`: visible pixels run the original code,
    hidden ones run `skip` instead. Every return goes through one exit block."""
    start, end = function_body(ir)
    body = ir[start:end].replace('\n  ret void', '\n  br label %ham.exit')
    # The rest of the entry block (%0) now lives in ham.body, so phi nodes name that predecessor.
    body = re.sub(r'(?m)^(  %\S+ = phi .*)$', lambda m: re.sub(r'%0 \]', '%ham.body ]', m.group(1)), body)
    body = replace_once(body, anchor, anchor + '\n'.join(
        list(prelude) + visible('ham', x, y) +
        ['  br i1 %ham.vis, label %ham.body, label %ham.skip', '', 'ham.skip:'] + list(skip) +
        ['  br label %ham.exit', '', 'ham.body:']) + '\n')
    return ir[:start] + body + '\n\nham.exit:\n  ret void' + ir[end:]


# ---- recipes -------------------------------------------------------------------------------------
# Hidden pixels get what the engine itself produces where there is nothing to do: black for an
# opaque full-screen pass, no write (discard) for a blended overlay, "unoccluded" for AO.

def cone_ao(ir):
    """DrawConeAO cone trace (half resolution, one pixel per thread, %21/%22 its NDC). It has no sky
    early-out; hidden pixels get no occlusion and a straight bent normal."""
    return skip_after(ir, '  %22 = fadd fast float %20, -1.000000e+00\n', '%21', '%22',
                      ['  call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %1, i32 %6, i32 %7, i32 undef, '
                       'float 5.000000e-01, float 5.000000e-01, float 1.000000e+00, float 1.000000e+00, i8 15)'])


def fog_overlay(ir):
    """RenderFogOverlay, blended over the scene (%26/%27 the pixel's NDC): hidden pixels are left alone."""
    return skip_after(ir, '  %27 = fadd fast float %25, -1.000000e+00\n', '%26', '%27',
                      ['  call void @dx.op.discard(i32 82, i1 true)'])


# FNV-1a of the original shader -> (name, recipe). A recipe is only worth having for a pass that
# does not already skip the hidden cells: after the far-plane reset they look like sky, and SSR,
# light integration and the shadow mask skip sky on their own (docs/hidden-area-mask-20261009.md).
# Patched and measured without a saving, so removed: the SSR tile classifier (0x78cc00b0064fdcc3),
# CAS (0x4cd91d4bbe85c03c, slower by 0.02-0.03 ms) and the final tonemap (0xec08e969bf38010f).
RECIPES = {
    0x34d5c22bdeded25c: ('DrawConeAO cone trace', cone_ao),
    0xcfb4cd58a4c45e19: ('RenderFogOverlay', fog_overlay),
}


# ---- build ---------------------------------------------------------------------------------------

def disassemble(dxil, work):
    source = work / 'in.dxil'
    source.write_bytes(dxil)
    out = work / 'in.ll'
    subprocess.run([str(DXC_DIR / 'dxc.exe'), '-dumpbin', str(source), '-Fc', str(out)], check=True, capture_output=True)
    return out.read_text()


def assemble(ir, work):
    source, out = work / 'out.ll', work / 'out.dxil'
    source.write_text(ir)
    result = subprocess.run([str(DXIL_TOOL), str(DXC_DIR), str(source), str(out)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError('assembly failed:\n' + result.stderr[-3000:])
    return out.read_bytes()


def build(original, recipe):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = assemble(recipe(disassemble(original, work)), work)
    if dc.dxil_hash(container) != container[4:20]:
        raise RuntimeError('validator output hash mismatch')
    offsets = []
    for value in PLACEHOLDERS:
        found = dc.find_bits(container, dc.float_pattern(value))
        if len(found) != 1:
            raise RuntimeError('placeholder %r found %d times' % (value, len(found)))
        offsets += found
    return container + b'HAM1' + struct.pack('<I%dI' % len(offsets), len(offsets), *offsets)


def fnv1a(data):
    h = 1469598103934665603
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--shaders', type=Path, required=True, help='original shaders named <fnv1a>.dxil')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for key, (name, recipe) in RECIPES.items():
        source = args.shaders / ('%016x.dxil' % key)
        if not source.exists():
            print('MISSING %016x %s' % (key, name))
            failed += 1
            continue
        original = source.read_bytes()
        if fnv1a(original) != key:
            raise RuntimeError('%s does not hash to its name' % source)
        try:
            patched = build(original, recipe)
        except Exception as error:
            print('FAIL %016x %s: %s' % (key, name, error))
            failed += 1
            continue
        (args.out / source.name).write_bytes(patched)
        print('OK   %016x %s (%d bytes)' % (key, name, len(patched)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
