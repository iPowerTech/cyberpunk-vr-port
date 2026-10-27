import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import batch_link_pairs as batch
from link_stage_signatures_test import stage


class BatchLinkTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.out = self.root / 'out'
        self.ids = [hashlib.sha256(name.encode()).hexdigest() for name in ('original-vs', 'original-ps')]
        shaders = {}
        for sha, kind in zip(self.ids, ('vs', 'ps')):
            data = kind.encode()
            (self.root / (sha + '.dxil')).write_bytes(data)
            (self.root / (sha + '.ll')).write_text('untrusted loose IR must not be read')
            shaders[sha] = dict(status='validated', stage=kind, sha256=hashlib.sha256(data).hexdigest())
        (self.root / 'pairs.json').write_text(json.dumps(dict(shaders=shaders)))
        desc = {name: dict(pShaderBytecode=dict(sha256=sha)) for name, sha in zip(('VS', 'PS'), self.ids)}
        rows = [dict(name='ID3D12Device::CreateGraphicsPipeline', fields=dict(pPipelineState=i, pDesc=desc)) for i in (1, 2)]
        (self.root / 'native.json').write_text(json.dumps(dict(creations=rows)))
        self.commands = []

    def run_tool(self, command):
        self.commands.append(command)
        if str(command[1]) == '-dumpbin':
            return stage('vs' if Path(command[2]).stem == self.ids[0] else 'ps')
        Path(command[3]).write_bytes(b'validated linked PS')
        return 'PASS'

    def invoke(self):
        argv = ['batch_link_pairs.py', '--native', str(self.root / 'native.json'), '--pairs', str(self.root / 'pairs.json'),
                '--out', str(self.out), '--dxc-dir', 'sdk', '--assembler', 'assembler.exe']
        with patch('sys.argv', argv), patch.object(batch, 'run', side_effect=self.run_tool), patch('builtins.print'):
            return batch.main()

    def test_hash_verified_binary_is_authoritative_and_pairs_are_reused(self):
        self.assertEqual(self.invoke(), 0)
        result = json.loads((self.out / 'manifest.json').read_text())
        self.assertEqual(result['linkSummary'], dict(validated=2))
        self.assertEqual(len(self.commands), 3)  # two disassemblies and one shared assembly
        self.assertEqual(result['pipelineShaders']['1'], result['pipelineShaders']['2'])
        self.assertEqual(result['pipelineShaders']['1']['vsConvertedSHA'], hashlib.sha256(b'vs').hexdigest())
        self.assertTrue(result['pipelineShaders']['1']['report']['relocated'])

    def test_corrupt_converted_input_is_rejected_before_assembly(self):
        (self.root / (self.ids[1] + '.dxil')).write_bytes(b'changed')
        self.assertEqual(self.invoke(), 1)
        result = json.loads((self.out / 'manifest.json').read_text())
        self.assertEqual(result['linkSummary'], dict(rejected=2))
        self.assertIn('hash changed', result['pipelineShaders']['1']['reason'])
        self.assertEqual(len(self.commands), 1)


if __name__ == '__main__':
    unittest.main()
