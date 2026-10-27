"""Package hash-verified linked VS/PS families for an explicit development launch."""
import argparse
import hashlib
from pathlib import Path
from batch_shaders import read_json, save_json


def package(native, inventory, linked, output):
    output.mkdir(parents=True, exist_ok=True)
    rows = read_json(native)['creations']
    originals = read_json(inventory)['shaders']
    variants = read_json(linked)
    families = {}

    def copy(path, expected):
        raw = path.read_bytes()
        if not 32 <= len(raw) <= 1024 * 1024 or hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError('Shader hash/size mismatch: ' + str(path))
        name = expected + '.dxil'
        (output / name).write_bytes(raw)
        return dict(file=name, sha256=expected, bytes=len(raw))

    for row in rows:
        if row['name'] != 'ID3D12Device::CreateGraphicsPipeline':
            continue
        desc = row['fields']['pDesc']
        if any(desc[s]['BytecodeLength'] for s in ('DS', 'HS', 'GS')):
            continue
        vs = desc['VS']['pShaderBytecode']['sha256']
        ps = desc['PS']['pShaderBytecode']['sha256'] if desc['PS']['BytecodeLength'] else ''
        if any(sha not in variants['shaders'] for sha in (vs, ps) if sha):
            continue
        key = (vs, ps)
        if key in families:
            continue
        family = dict(nativeVs=vs, nativePs=ps, nativeVsBytes=desc['VS']['BytecodeLength'],
                      nativePsBytes=desc['PS']['BytecodeLength'], pairs=[], uses=[], runtimeEligible=False)
        for sha, stage in ((vs, 'vs'), (ps, 'ps')):
            if not sha:
                continue
            variant = variants['shaders'][sha]
            if variant['status'] != 'validated' or variant['stage'] != stage:
                raise ValueError('Unvalidated/wrong shader stage')
            path, digest = Path(variant['file']), variant['sha256']
            if stage == 'ps':
                link = variants['pipelineShaders'][str(row['fields']['pPipelineState'])]
                if (link['status'] != 'validated' or link['vsOriginal'] != vs or link['psOriginal'] != ps or
                        link['vsConvertedSHA'] != variants['shaders'][vs]['sha256'] or link['psConvertedSHA'] != digest):
                    raise ValueError('Missing/mismatched signature link')
                if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                    raise ValueError('Pixel input changed since linking')
                path, digest = linked.parent / link['file'], link['sha256']
            family[stage] = copy(path, digest)
            visibility = 1 if stage == 'vs' else 5
            for binding in variant['report']['pairs']:
                if binding['sourceSpace'] or binding['count'] != 2 or binding['register'] != binding['sourceRegister'] * 2:
                    raise ValueError('Pair ABI changed')
                family['pairs'].append(dict(type=2 if binding['kind'] == 'cbuffer' else 0, visibility=visibility,
                                            register=binding['sourceRegister'], space=binding['space']))
            for binding in originals[sha]['resources']:
                family['uses'].append(dict(type={'srv': 0, 'uav': 1, 'cbuffer': 2, 'sampler': 3}[binding['kind']],
                                           visibility=visibility, register=binding['register'], space=binding['space'],
                                           count=binding['count'], bytes=binding.get('bytes', 0)))
        families[key] = family
    if not families:
        raise ValueError('No linked graphics families')
    manifest = dict(schema=1, purpose='development-linkage-only', families=list(families.values()))
    save_json(output / 'families.json', manifest)
    return dict(families=len(families), blobs=len(list(output.glob('*.dxil'))), manifest=str(output / 'families.json'))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native', type=Path, required=True)
    p.add_argument('--inventory', type=Path, required=True)
    p.add_argument('--linked', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    print(package(a.native, a.inventory, a.linked, a.out))
