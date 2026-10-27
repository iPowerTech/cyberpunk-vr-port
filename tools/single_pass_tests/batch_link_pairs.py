"""Create family-specific linked pixel signatures; never modify the original batch."""
import argparse
from collections import Counter
import hashlib
from pathlib import Path
from batch_shaders import PASSES, read_json, run, save_json
from link_stage_signatures import link_pixel_inputs


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native', type=Path, required=True)
    p.add_argument('--pairs', type=Path, required=True)
    p.add_argument('--dxc-dir', type=Path, required=True)
    p.add_argument('--assembler', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    manifest = read_json(a.pairs)
    a.out.mkdir(parents=True, exist_ok=True)
    for sha, item in manifest['shaders'].items():
        item['file'] = str((a.pairs.parent / (sha + '.dxil')).resolve())
    pipelines = {}
    cache = {}
    disassembly = {}

    def disassemble(sha):
        item = manifest['shaders'][sha]
        binary = Path(item['file'])
        if hashlib.sha256(binary.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Converted shader hash changed')
        if sha not in disassembly:
            disassembly[sha] = run([a.dxc_dir / 'dxc.exe', '-dumpbin', binary])
        return disassembly[sha]

    for row in read_json(a.native)['creations']:
        if row['name'] != 'ID3D12Device::CreateGraphicsPipeline':
            continue
        source = row['fields']['pDesc']
        vs = source['VS']['pShaderBytecode']['sha256']
        ps = source['PS']['pShaderBytecode']['sha256']
        if vs not in manifest['shaders'] or ps not in manifest['shaders']:
            continue
        key = (vs, ps)
        if key not in cache:
            item = dict(status='rejected', vsOriginal=vs, psOriginal=ps,
                        vsConvertedSHA=manifest['shaders'][vs]['sha256'], psConvertedSHA=manifest['shaders'][ps]['sha256'])
            try:
                vertex = disassemble(vs)
                pixel = disassemble(ps)
                ir, report = link_pixel_inputs(vertex, pixel)
                checksum = hashlib.sha256(ir.encode()).hexdigest()
                text = a.out / (checksum + '.ll')
                binary = text.with_suffix('.dxil')
                text.write_text(ir, encoding='utf-8')
                run([a.assembler, a.dxc_dir, text, binary, *PASSES])
                item.update(status='validated', file=binary.name, report=report,
                            sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
            except Exception as error:
                item['reason'] = str(error)
            cache[key] = item
        pipelines[str(row['fields']['pPipelineState'])] = cache[key]
    manifest['pipelineShaders'] = pipelines
    manifest['linkSummary'] = dict(Counter(item['status'] for item in pipelines.values()))
    save_json(a.out / 'manifest.json', manifest)
    print(manifest['linkSummary'])
    for item in cache.values():
        if item['status'] == 'rejected':
            print(item)
    return int(any(item['status'] == 'rejected' for item in cache.values()))


if __name__ == '__main__':
    raise SystemExit(main())
