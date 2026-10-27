"""Build the 2.5 stereo bloom prefilter (CyberpunkVR_StereoBloomPatch).

The first downsample currently averages nine HDR samples arithmetically. Thin
emissives retained by DLSS Transformer dominate that average and the subsequent
bloom pyramid. Normalize the samples by their HDR luminance, w=1/(1+Y), before
averaging. Constant-colour regions retain their original value; isolated peaks
contribute less. This only modifies bloom, not the scene colour sent to tonemapping.

The native threshold, nine sample positions, stereo boundary handling, mip clamp
and R11G11B10 rounding remain. The mono branch keeps its original arithmetic.
Only the captured 2.5 pixel shader is supported; other hashes fail closed. The
optional BloomUseCompute path is not covered by this patch.

Extract originals from engine/staticshader_final.cache with extract_shader_cache.py,
then pass that directory with --shaders and the destination directory with --out.
Validated with Transformer in a live A/B, including horizontal/vertical head poses
and continuous yaw sweeps. The mono/VRCAM path keeps the original PSO at the binder.
"""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'hidden_area_tests'))
import ham_shaders as ham
import dxil_container as dc

KEY = 0x83848f96b80814d5


def prefilter(ir):
    def replace(old, new):
        nonlocal ir
        if ir.count(old) != 1:
            raise ValueError('Unexpected bloom shader structure: ' + old)
        ir = ir.replace(old, new)

    replace('  %13 = phi float', '  %bloom.weightSum = phi float [ 0.000000e+00, %0 ], [ %bloom.nextWeightSum, %51 ]\n  %13 = phi float')
    replace('  %68 = fmul fast float %67, %57', '''  %bloom.denominator = fadd fast float %60, 1.000000e+00
  %bloom.hdrWeight = fdiv fast float 1.000000e+00, %bloom.denominator
  %bloom.weight = select i1 %32, float 1.000000e+00, float %bloom.hdrWeight
  %bloom.nextWeightSum = fadd fast float %bloom.weightSum, %bloom.weight
  %bloom.thresholdWeight = fmul fast float %67, %bloom.weight
  %68 = fmul fast float %bloom.thresholdWeight, %57''')
    replace('  %69 = fmul fast float %67, %58', '  %69 = fmul fast float %bloom.thresholdWeight, %58')
    replace('  %70 = fmul fast float %67, %59', '  %70 = fmul fast float %bloom.thresholdWeight, %59')
    replace('  %77 = fmul fast float %71, 0x3FBC71C720000000', '''  %bloom.inverseWeight = fdiv fast float 1.000000e+00, %bloom.nextWeightSum
  %bloom.normalization = select i1 %32, float 0x3FBC71C720000000, float %bloom.inverseWeight
  %bloom.negativeNormalization = fsub fast float -0.000000e+00, %bloom.normalization
  %77 = fmul fast float %71, %bloom.normalization''')
    for dst, src in [(78, 72), (79, 73)]:
        replace(f'  %{dst} = fmul fast float %{src}, 0x3FBC71C720000000',
                f'  %{dst} = fmul fast float %{src}, %bloom.normalization')
    for dst, src in [(89, 71), (91, 72), (93, 73)]:
        replace(f'  %{dst} = fmul fast float %{src}, 0xBFBC71C720000000',
                f'  %{dst} = fmul fast float %{src}, %bloom.negativeNormalization')
    return ir


def build(original):
    if ham.fnv1a(original) != KEY:
        raise ValueError('Unsupported bloom shader; expected %016x' % KEY)
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        container = ham.assemble(prefilter(ham.disassemble(original, work)), work)
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
