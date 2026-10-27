import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import package_snapshot_families as package


class SnapshotPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.originals = self.root / 'sources'
        self.conversion = self.root / 'conversion'
        self.originals.mkdir()
        (self.conversion / 'package').mkdir(parents=True)
        self.snapshot_path = self.root / 'snapshot.json'
        self.snapshot = dict(id=7, pipeline='0x1234', input=[])
        for stage in ('vs', 'ps', 'root'):
            self.snapshot[stage] = self.binary(self.originals, ('native-' + stage).encode().ljust(40, b'x'), '.bin')
        self.info = {
            'vs': dict(stage='vs', cameraBindings=1, camera=dict(register=1, space=0, bytes=848),
                       resources=[dict(kind='cbuffer', id=0, name='', type='%CameraShaderConsts',
                                       register=1, space=0, count=1, bytes=848)]),
            'ps': dict(stage='ps', cameraBindings=0,
                       resources=[dict(kind='cbuffer', id=0, name='', type='%Material',
                                       register=4, space=0, count=1, bytes=292)])}
        inventory = {}
        for stage in ('vs', 'ps'):
            key = hashlib.sha256(stage.encode()).hexdigest()
            directory = self.conversion / 'batch/cache' / key
            directory.mkdir(parents=True)
            (directory / 'native.ll').write_text(stage)
            inventory[self.snapshot[stage]['sha256']] = dict(self.info[stage], key=key, status='validated',
                artifacts={'native.ll': hashlib.sha256(stage.encode()).hexdigest()})
        self.write(self.conversion / 'batch/manifest.json', dict(shaders=inventory))
        self.converted = dict(sourceId=7, status='validated', failures=[], runtimeEligible=False,
                              native={s: self.snapshot[s] for s in ('vs', 'ps', 'root')}, input=[], cameraStageMask=1,
                              stages={s: dict(original=self.info[s], layout=None) for s in ('vs', 'ps')})
        self.converted['stages']['vs']['layout'] = dict(main_first=True, original_size=848, eye_stride=1024,
                                                       binding_size=2048, camera_register=1, space=0)
        for stage in ('vs', 'ps'):
            self.converted[stage] = self.binary(self.conversion / 'package', ('camera-' + stage).encode().ljust(40, b'x'))
            self.converted[stage]['cameraPatched'] = stage == 'vs'
        self.converted['interfaceLink'] = dict(entry=self.converted['ps'])
        self.publish()
        self.commands = []

        def run(command):
            self.commands.append([str(x) for x in command])
            if '-dumpbin' in command:
                Path(command[3]).write_text(Path(command[4]).with_suffix('.ll').read_text())
            else:
                Path(command[3]).write_bytes(hashlib.sha256(Path(command[2]).read_bytes()).digest())
            return 'PASS CPU mock validation'

        def pairs(text, space):
            register = 1 if text == 'vs' else 4
            return text + '-paired', dict(pairs=[dict(kind='cbuffer', sourceSpace=0, sourceRegister=register,
                                                     space=space, register=register * 2, count=2)])

        for mocked in (
            patch.object(package.batch, 'classify', side_effect=lambda text: copy.deepcopy(self.info[text])),
            patch.object(package.batch, 'toolchain_identity', return_value=dict(files={}, schema=1)),
            patch.object(package.batch, 'run', side_effect=run),
            patch.object(package, 'patch_pairs', side_effect=pairs),
            patch.object(package, 'link_pixel_inputs', side_effect=lambda vs, ps: (vs + '/' + ps, dict(changed=True))),
        ):
            mocked.start()
            self.addCleanup(mocked.stop)

    def write(self, path, value):
        path.write_text(json.dumps(value))

    def binary(self, directory, data, suffix='.dxil'):
        digest = hashlib.sha256(data).hexdigest()
        (directory / (digest + suffix)).write_bytes(data)
        return dict(file=digest + suffix, sha256=digest, bytes=len(data))

    def publish(self, snapshots=None, conversions=None):
        self.write(self.snapshot_path, dict(nativeShaderSources=dict(directory='sources', records=snapshots or [self.snapshot])))
        self.write(self.conversion / 'coverage.json', dict(records=conversions or [self.converted]))

    def run_package(self, name='out', base=None):
        return package.package(self.snapshot_path, self.conversion, self.root / name,
                               self.root / 'dxc', self.root / 'assembler.exe', base)

    def test_snapshot_only_loader_shape_and_hashes(self):
        result = self.run_package()
        self.assertEqual((result['sources'], result['newFamilies'], result['families']), (1, 1, 1))
        manifest = json.loads((self.root / 'out/families.json').read_text())
        family = manifest['families'][0]
        self.assertEqual(manifest['purpose'], 'development-linkage-only')
        self.assertEqual(family['uses'], package.uses(self.info['vs'], 'vs') + package.uses(self.info['ps'], 'ps'))
        self.assertEqual([p['space'] for p in family['pairs']], [100, 101])
        self.assertEqual(family['nativeCamera']['stages'], 1)
        self.assertFalse(family['runtimeEligible'])
        package.validate_family(family, self.root / 'out')
        provenance = json.loads((self.root / 'out/provenance.json').read_text())
        self.assertEqual(provenance['records'][0]['sourceId'], 7)
        self.assertNotIn('pDesc', family)

    def test_base_order_and_normalized_blob_differences_preserved(self):
        self.run_package('base')
        base_path = self.root / 'base/families.json'
        base = json.loads(base_path.read_text())
        family = base['families'][0]
        family['vs'] = self.binary(base_path.parent, b'old-verified-generic'.ljust(40, b'x'))
        family['nativeCamera']['ps'] = dict(self.binary(base_path.parent, b'old-normalized-camera'.ljust(40, b'x')), cameraPatched=False)
        self.write(base_path, base)
        result = self.run_package('merged', base_path)
        self.assertEqual((result['baseFamilies'], result['newFamilies'], result['preservedSourcePairs']), (1, 0, 1))
        self.assertEqual(json.loads((self.root / 'merged/families.json').read_text()), base)
        self.assertEqual(result['generatedStages'], 0)

    def test_same_source_pair_deduplicates_without_address_identity(self):
        snapshot = dict(self.snapshot, id=8, pipeline='0x9876')
        converted = dict(self.converted, sourceId=8)
        self.publish([self.snapshot, snapshot], [self.converted, converted])
        result = self.run_package()
        self.assertEqual((result['sources'], result['families']), (2, 1))

    def test_source_hash_and_input_boundary(self):
        for kind in ('hash', 'input', 'id', 'duplicate', 'extra', 'unvalidated'):
            with self.subTest(kind=kind):
                saved = copy.deepcopy(self.converted)
                if kind == 'hash':self.converted['native']['root'] = dict(self.snapshot['root'], sha256='1' * 64)
                if kind == 'input':self.converted['input'] = [dict(semantic='OTHER')]
                if kind == 'id':self.converted['sourceId'] = True
                if kind == 'unvalidated':self.converted['status'] = 'rejected'
                values = [self.converted]
                if kind == 'duplicate':values.append(self.converted)
                if kind == 'extra':values.append(dict(self.converted, sourceId=8))
                self.publish(conversions=values)
                with self.assertRaises(ValueError):self.run_package(kind)
                self.converted = saved
        self.publish()

    def test_original_binary_and_ir_corruption(self):
        original = self.originals / self.snapshot['vs']['file']
        data = original.read_bytes()
        original.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'hash or size'):self.run_package()
        original.write_bytes(data)
        path = next((self.conversion / 'batch/cache').glob('*/native.ll'))
        path.write_text('changed')
        with self.assertRaisesRegex(ValueError, 'IR hash'):self.run_package()

    def test_camera_contract_mismatches_rejected(self):
        for field in ('mask', 'flag', 'layout', 'linked'):
            with self.subTest(field=field):
                saved = copy.deepcopy(self.converted)
                if field == 'mask':self.converted['cameraStageMask'] = 3
                if field == 'flag':self.converted['vs']['cameraPatched'] = False
                if field == 'layout':self.converted['stages']['vs']['layout']['main_first'] = False
                if field == 'linked':self.converted['interfaceLink']['entry'] = dict(self.converted['ps'], sha256='1' * 64)
                self.publish()
                with self.assertRaises(ValueError):self.run_package(field)
                self.converted = saved

    def test_base_conflicting_resources_schema_and_duplicate_rejected(self):
        self.run_package('base')
        path = self.root / 'base/families.json'
        baseline = json.loads(path.read_text())
        for field in ('bytes', 'uses', 'schema', 'mask', 'duplicate', 'admission', 'boolean'):
            with self.subTest(field=field):
                base = copy.deepcopy(baseline)
                family = base['families'][0]
                if field == 'bytes':family['nativeVsBytes'] += 4
                if field == 'uses':family['uses'][1]['bytes'] += 4
                if field == 'schema':family['nativeCamera']['eyeStride'] = 2048
                if field == 'mask':family['nativeCamera']['stages'] = 3
                if field == 'duplicate':base['families'].append(copy.deepcopy(family))
                if field == 'admission':family['runtimeEligible'] = True
                if field == 'boolean':family['uses'][0]['visibility'] = True
                self.write(path, base)
                with self.assertRaises(ValueError):self.run_package(field, path)

    def test_cache_hit_and_corruption_rebuild(self):
        self.run_package()
        self.assertEqual(self.run_package()['stageCacheHits'], 2)
        cached = next((self.root / 'out/cache').glob('*/stereo.dxil'))
        cached.write_bytes(b'corrupt')
        self.assertEqual(self.run_package()['stageCacheHits'], 1)

    def test_path_traversal_and_boolean_snapshot_id_rejected(self):
        snapshot = copy.deepcopy(self.snapshot)
        snapshot['vs']['file'] = '../escape.bin'
        converted = dict(self.converted, native={s: snapshot[s] for s in ('vs', 'ps', 'root')})
        self.publish([snapshot], [converted])
        with self.assertRaises(ValueError):self.run_package()
        snapshot = dict(self.snapshot, id=True)
        self.publish([snapshot], [dict(self.converted, sourceId=True)])
        with self.assertRaises(ValueError):self.run_package()


if __name__ == '__main__':
    unittest.main()
