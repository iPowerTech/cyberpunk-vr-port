import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from package_families import package


class PackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.native, self.inventory, self.linked = [self.root / (name + '.json') for name in ('native', 'inventory', 'linked')]
        self.vs, self.ps = '1' * 64, '2' * 64
        self.variants = {}
        for name in ('vs', 'ps', 'linked-ps'):
            data = name.encode().ljust(40, b'x')
            path = self.root / (name + '.dxil')
            path.write_bytes(data)
            self.variants[name] = dict(file=str(path), sha256=hashlib.sha256(data).hexdigest(), status='validated', stage=name,
                                       report=dict(pairs=[dict(kind='cbuffer', sourceSpace=0, sourceRegister=1, count=2, register=2, space=100 if name == 'vs' else 101)]))
        empty = dict(BytecodeLength=0)
        desc = dict(VS=dict(BytecodeLength=40, pShaderBytecode=dict(sha256=self.vs)),
                    PS=dict(BytecodeLength=40, pShaderBytecode=dict(sha256=self.ps)), DS=empty, HS=empty, GS=empty)
        self.native.write_text(json.dumps(dict(creations=[dict(name='ID3D12Device::CreateGraphicsPipeline',
                                                             fields=dict(pPipelineState=i, pDesc=desc)) for i in (1, 2)])))
        use = dict(kind='cbuffer', register=1, space=0, count=1, bytes=848)
        self.inventory.write_text(json.dumps(dict(shaders={sha: dict(resources=[use]) for sha in (self.vs, self.ps)})))
        link = dict(self.variants['linked-ps'], vsOriginal=self.vs, psOriginal=self.ps,
                    vsConvertedSHA=self.variants['vs']['sha256'], psConvertedSHA=self.variants['ps']['sha256'])
        self.manifest = dict(shaders={self.vs: self.variants['vs'], self.ps: self.variants['ps']}, pipelineShaders={'1': link, '2': link})
        self.linked.write_text(json.dumps(self.manifest))

    def test_deduplicates_family_but_preserves_stage_usage(self):
        out = self.root / 'out'
        result = package(self.native, self.inventory, self.linked, out)
        self.assertEqual((result['families'], result['blobs']), (1, 2))
        family = json.loads((out / 'families.json').read_text())['families'][0]
        self.assertEqual([v['visibility'] for v in family['uses']], [1, 5])
        self.assertEqual([v['space'] for v in family['pairs']], [100, 101])
        self.assertFalse(family['runtimeEligible'])
        for stage in ('vs', 'ps'):
            value = family[stage]
            self.assertEqual(hashlib.sha256((out / value['file']).read_bytes()).hexdigest(), value['sha256'])

    def test_corrupt_input_rejected(self):
        Path(self.variants['vs']['file']).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'hash/size'):
            package(self.native, self.inventory, self.linked, self.root / 'out')

    def test_mismatched_shader_pair_rejected(self):
        self.manifest['pipelineShaders']['1']['vsOriginal'] = '3' * 64
        self.linked.write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'signature link'):
            package(self.native, self.inventory, self.linked, self.root / 'out')


if __name__ == '__main__':
    unittest.main()
