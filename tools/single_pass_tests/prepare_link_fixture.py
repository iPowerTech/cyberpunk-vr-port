"""Prepare a real geometry fixture with a generated-input/clip-distance collision."""
import argparse
import json
from pathlib import Path
from batch_shaders import PASSES, metadata, run
from link_stage_signatures import ELEMENT, element_text, link_pixel_inputs, signature
from patch_resource_pairs import patch_pairs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--vertex', type=Path, required=True)
    p.add_argument('--dxc-dir', type=Path, required=True)
    p.add_argument('--assembler', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    dxc = a.dxc_dir / 'dxc.exe'
    binary, text = a.out / 'native-ps.dxil', a.out / 'native-ps.ll'
    run([dxc, '-T', 'ps_6_0', '-E', 'main', '-Fo', binary, Path(__file__).with_name('probe_link_ps.hlsl')])
    run([dxc, '-dumpbin', binary, '-Fc', text])
    vertex = a.vertex.read_text()
    clip = [e for e in signature(vertex, 'vs', True)[2] if e['fields'][1].lower() == '!"sv_clipdistance"']
    if len(clip) != 1:
        raise ValueError('Expected captured VS with one clip-distance element')
    source = text.read_text()
    fronts = [(node, ELEMENT.fullmatch(value)) for node, value in metadata(source).items()
              if '!"SV_IsFrontFace"' in value]
    if len(fronts) != 1 or not fronts[0][1]:
        raise ValueError('Expected one generated front-face input')
    node, match = fronts[0]
    fields = list(match.groups())
    fields[8], fields[9] = clip[0]['fields'][8], clip[0]['fields'][9]
    source = source.replace(node + ' = !{' + match[0] + '}', node + ' = !{' + element_text(fields) + '}')
    text.write_text(source)
    run([a.assembler, a.dxc_dir, text, binary, *PASSES])
    converted, _ = patch_pairs(source, 101)
    paired_text = a.out / 'paired-ps.ll'
    paired_text.write_text(converted)
    run([a.assembler, a.dxc_dir, paired_text, paired_text.with_suffix('.dxil'), *PASSES])
    linked, report = link_pixel_inputs(vertex, converted)
    if not report['relocated']:
        raise ValueError('Fixture failed to exercise generated-input relocation')
    linked_text = a.out / 'linked-ps.ll'
    linked_text.write_text(linked)
    run([a.assembler, a.dxc_dir, linked_text, linked_text.with_suffix('.dxil'), *PASSES])
    (a.out / 'link.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
