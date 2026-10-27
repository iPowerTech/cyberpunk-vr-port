import ast
import inspect
import struct
import symtable
import unittest

from read_preparation import arm_scene_access_controls, capture_scene_access_failure
from scene_access_report import BLOCKS, CONTROLS, PREFIX, restore_controls
from scene_access_report_test import fixture


class SceneAccessFailureTest(unittest.TestCase):
    def test_main_cleanup_import_cannot_shadow_the_existing_global_helper(self):
        import read_preparation
        source = inspect.getsource(read_preparation)
        syntax = ast.parse(source)
        module_names = {node.name for node in syntax.body if isinstance(node, (ast.FunctionDef, ast.ClassDef))}
        module_names.update(alias.asname or alias.name.split('.')[0]
                            for node in syntax.body if isinstance(node, (ast.Import, ast.ImportFrom)) for alias in node.names)
        main = next(child for child in symtable.symtable(source, 'read_preparation.py', 'exec').get_children()
                    if child.get_name() == 'main')
        self.assertTrue(main.lookup('restore_controls').is_global())
        self.assertFalse(main.lookup('restore_controls').is_local())
        self.assertTrue(main.lookup('restore_scene_access_controls').is_imported())
        self.assertEqual([symbol.get_name() for symbol in main.get_symbols()
                          if symbol.is_imported() and symbol.get_name() in module_names], [])
        main_ast = next(node for node in syntax.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        calls = [node for node in ast.walk(main_ast) if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)]
        self.assertEqual([len(node.args) for node in calls if node.func.id == 'restore_controls'], [5])
        self.assertEqual([len(node.args) for node in calls if node.func.id == 'restore_scene_access_controls'], [2])

    def test_legacy_and_scene_cleanup_keep_their_distinct_call_signatures(self):
        import read_preparation
        writes = []
        self.assertEqual(read_preparation.restore_controls(lambda *args:writes.append(args), lambda:False), 'restored')
        self.assertEqual(writes, [('Request', 0), ('State', 0), ('PairRequest', 0), ('PairMergeLimit', 1)])
        writes.clear();camera = []
        self.assertEqual(read_preparation.restore_controls(lambda *args:writes.append(args), lambda:False,
                                                          True, True, lambda *args:camera.append(args)), 'restored')
        self.assertEqual(writes[-4:], [('GraphRequest', 0), ('GraphState', 0), ('ScheduleMode', 0), ('CameraTrace', 0)])
        self.assertEqual(camera, [('Request', 0), ('State', 0), ('NodeFilter', 0)])
        controls = dict.fromkeys(CONTROLS, 1)
        restored = restore_controls(controls.__getitem__, lambda name, value:controls.__setitem__(name, value))
        self.assertTrue(restored['restored'])
        self.assertEqual(restored['values'], dict.fromkeys(CONTROLS, 0))

    def test_active_unpublished_arrays_preserve_bounded_scalar_failure(self):
        stats = [1, 1, 0, 5, 2, 3, 20, 4, 2, 7, 2, 1, 3, 0, 6, 0]
        calls = []
        def read(name, size):
            calls.append((name, size))
            if name == PREFIX+'Seq':return struct.pack('<I', 2)
            if name == PREFIX+'Stats':return struct.pack('<16Q', *stats)
            return struct.pack('<Q', 0)
        def memory(pointer, size):raise AssertionError('Unpublished pointers must not be dereferenced')
        report = dict(passed=False, error='original failure')
        capture_scene_access_failure(report, read, memory)
        self.assertEqual(report['sceneAccessFailureStats'], stats)
        self.assertNotIn('sceneAccess', report)
        self.assertFalse(report['passed'])
        self.assertEqual(report['error'], 'original failure')
        self.assertEqual(report['sceneAccessFailureObservation'],
                         dict(completedVerified=False, stable=True, sequenceBefore=2, sequenceAfter=2))
        self.assertTrue(all(size in (4, 8, 128) for _, size in calls))

    def test_torn_scalar_snapshot_is_saved_but_not_verified(self):
        stats = [1, 1, 0, 5, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 3, 0]
        sequences = iter((2, 4, 6))
        def read(name, size):
            if name == PREFIX+'Seq':return struct.pack('<I', next(sequences))
            if name == PREFIX+'Stats':return struct.pack('<16Q', *stats)
            return struct.pack('<Q', 0)
        report = {}
        capture_scene_access_failure(report, read, lambda *_:b'')
        self.assertEqual(report['sceneAccessFailureStats'], stats)
        self.assertFalse(report['sceneAccessFailureObservation']['stable'])
        self.assertFalse(report['sceneAccessFailureObservation']['completedVerified'])

    def test_missing_sequence_does_not_hide_available_scalar_failure(self):
        stats = [1, 1, 0, 5, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 7, 0]
        def read(name, size):
            if name == PREFIX+'Stats':return struct.pack('<16Q', *stats)
            raise RuntimeError('sequence unavailable')
        report = {}
        capture_scene_access_failure(report, read, lambda *_:b'')
        self.assertEqual(report['sceneAccessFailureStats'], stats)
        self.assertIn('sequenceBeforeError', report['sceneAccessFailureObservation'])
        self.assertIn('sequenceAfterError', report['sceneAccessFailureObservation'])
        self.assertFalse(report['sceneAccessFailureObservation']['stable'])

    def test_scalar_read_failure_does_not_replace_original_error(self):
        def read(name, size):raise RuntimeError('process ended')
        report = dict(passed=False, error='capture failed')
        capture_scene_access_failure(report, read, lambda *_:b'')
        self.assertEqual(report['error'], 'capture failed')
        self.assertIn('sceneAccessFailureStatsError', report)
        self.assertNotIn('sceneAccessFailureStats', report)
        self.assertFalse(report['sceneAccessFailureObservation']['completedVerified'])

    def test_finished_raw_report_is_not_promoted_to_validated_success(self):
        observed, _, _ = fixture()
        memory = {}
        exports = {PREFIX+'Seq':struct.pack('<I', 2), PREFIX+'Stats':struct.pack('<16Q', *observed['stats'])}
        for index, (name, suffix, layout, _) in enumerate(BLOCKS):
            pointer = 0x100000 + index*0x100000
            exports[PREFIX+suffix] = struct.pack('<Q', pointer)
            memory[pointer] = b''.join(struct.pack(layout, *row) for row in observed[name])
        report = dict(passed=False)
        capture_scene_access_failure(report, lambda name, size:exports[name][:size], lambda pointer, size:memory[pointer][:size])
        self.assertEqual(report['sceneAccess'], observed)
        self.assertFalse(report['sceneAccess']['verified'])
        self.assertFalse(report['passed'])
        self.assertNotIn('sceneAccessFailureStats', report)

    def test_partial_enable_write_claims_outer_cleanup_before_it_fails(self):
        values = dict.fromkeys(CONTROLS, 0);owned = False;calls = [];clears = 0
        def claim():
            nonlocal owned
            owned = True;calls.append('claim')
        def write(name, value):
            nonlocal clears
            calls.append((name, value))
            if value:
                self.assertTrue(owned)
                values[name] = value
                raise RuntimeError('partial enable write')
            if name == CONTROLS[0]:
                clears += 1
                if clears == 1:raise RuntimeError('first cleanup failed')
            values[name] = 0
        cleanup = None
        with self.assertRaisesRegex(RuntimeError, 'partial enable write'):
            try:arm_scene_access_controls(values.__getitem__, write, claim)
            finally:
                if owned:cleanup = restore_controls(values.__getitem__, write)
        self.assertTrue(cleanup['restored'])
        self.assertEqual(values, dict.fromkeys(CONTROLS, 0))
        self.assertEqual(calls[:2], ['claim', (CONTROLS[0], 1)])
        self.assertEqual(clears, 2)

    def test_busy_preflight_does_not_claim_or_clear_someone_elses_controls(self):
        for name in CONTROLS:
            values = dict.fromkeys(CONTROLS, 0);values[name] = 1;calls = []
            with self.assertRaises(ValueError):
                arm_scene_access_controls(values.__getitem__, lambda *args:calls.append(args), lambda:calls.append('claim'))
            self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
