import contextlib
import copy
import io
import sys
import tempfile
import unittest
from unittest.mock import patch

from scene_access_report import CONTROLS, arm_controls, restore_controls, validate, validate_capture, validate_options
from scene_access_report_test import fixture


class SceneAccessLaunchTest(unittest.TestCase):
    def test_only_both_eye_retained_reference_mode_is_accepted(self):
        validate_options(False, eyes='one', retained_geometry=None, pass_targets=False)
        validate_options(True, eyes='both', retained_geometry='reference', pass_targets=True)
        for kwargs in (dict(eyes='one', retained_geometry='reference', pass_targets=True),
                       dict(eyes='both', retained_geometry='replace', pass_targets=True),
                       dict(eyes='both', retained_geometry='reference', pass_targets=False)):
            with self.assertRaises(ValueError):validate_options(True, **kwargs)

    def test_both_clis_reject_incompatible_mode_before_process_access(self):
        import read_preparation
        import run_launch_tests
        for flags in ([], ['--scene-split'], ['--scene-split-eyes', 'both'],
                      ['--scene-split-eyes', 'both', '--retained-geometry', 'reference']):
            with self.subTest(reader=flags), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                read_preparation.main(['--pid', '1', '--out', 'unused-access.json', '--scene-access', *flags])
            self.assertEqual(error.exception.code, 2)
            with self.subTest(launcher=flags), patch.object(sys, 'argv', ['run_launch_tests.py', '--out', 'unused-access', '--scene-access', *flags]), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                run_launch_tests.main()
            self.assertEqual(error.exception.code, 2)

    def test_reader_requests_scene_access_exports_without_early_process_access(self):
        import read_preparation
        import fnmatch
        class Resolved(Exception):pass
        with patch('live_symbols.resolve', side_effect=Resolved) as resolved, patch('live_read.Reader') as reader:
            with self.assertRaises(Resolved):
                read_preparation.main(['--pid', '1', '--out', 'unused-access.json', '--scene-access', '--scene-split-eyes', 'both',
                                       '--retained-geometry', 'reference', '--pass-targets', '--merge-root', '--schedule-preparation', 'apply'])
            reader.assert_not_called()
        patterns = resolved.call_args.args[2]
        for name in (*CONTROLS, 'CyberpunkVR_SceneAccessStats', 'CyberpunkVR_SceneAccessSeq',
                     'CyberpunkVR_SceneAccessRecordings', 'CyberpunkVR_SceneAccessResources',
                     'CyberpunkVR_SceneAccessEvents', 'CyberpunkVR_SceneAccessChildren'):
            self.assertTrue(any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns))

    def test_launcher_accepts_both_eye_mode_before_owned_launch_boundary(self):
        import run_launch_tests
        class Boundary(Exception):pass
        with tempfile.TemporaryDirectory() as directory, patch.object(sys, 'argv', ['run_launch_tests.py', '--out', directory,
                '--scene-access', '--scene-split-eyes', 'both', '--retained-geometry', 'reference', '--pass-targets']), \
                patch.object(run_launch_tests.boot, 'process_ids', side_effect=Boundary):
            with self.assertRaises(Boundary):run_launch_tests.main()

    def test_arm_only_sets_access_control_and_runtime_owns_child_scope(self):
        values = dict.fromkeys(CONTROLS, 0);writes = []
        def write(name, value):writes.append((name, value));values[name] = value
        self.assertEqual(arm_controls(values.__getitem__, write), dict.fromkeys(CONTROLS, 0))
        self.assertEqual(writes, [(CONTROLS[0], 1)])
        self.assertEqual(values[CONTROLS[1]], 0)
        self.assertEqual(restore_controls(values.__getitem__, write), dict(values=dict.fromkeys(CONTROLS, 0), errors={}, restored=True))

    def test_busy_controls_are_not_overwritten(self):
        for name in CONTROLS:
            values = dict.fromkeys(CONTROLS, 0);values[name] = 1;writes = []
            with self.assertRaises(ValueError):arm_controls(values.__getitem__, lambda *args:writes.append(args))
            self.assertEqual(writes, [])

    def test_partial_arm_failure_attempts_to_restore_both_controls(self):
        values = dict.fromkeys(CONTROLS, 0);writes = []
        def write(name, value):
            writes.append((name, value));values[name] = value
            if value:raise RuntimeError('arm failure')
        with self.assertRaisesRegex(RuntimeError, 'arm failure'):arm_controls(values.__getitem__, write)
        self.assertEqual(writes, [(CONTROLS[0], 1), (CONTROLS[0], 0), (CONTROLS[1], 0)])
        self.assertEqual(values, dict.fromkeys(CONTROLS, 0))

    def test_cleanup_attempts_second_control_after_first_failure(self):
        writes = []
        def write(name, value):
            writes.append(name)
            if name == CONTROLS[0]:raise RuntimeError('cannot write')
        result = restore_controls(lambda name:0, write)
        self.assertFalse(result['restored'])
        self.assertEqual(writes, list(CONTROLS))
        self.assertEqual(result['values'], {CONTROLS[1]:0})
        self.assertIn(CONTROLS[0], result['errors'])

    def test_failed_readback_cannot_claim_cleanup(self):
        result = restore_controls(lambda name:1, lambda name, value:None)
        self.assertFalse(result['restored'])
        self.assertEqual(set(result['errors']), set(CONTROLS))

    def test_capture_requires_validated_journal_and_both_control_readbacks(self):
        report, publication, owned = fixture()
        checked = validate(report, publication, owned_stats=owned)
        capture = dict(passed=True, sceneSplitEyes='both', sceneAccessRequested=True, sceneAccess=checked,
                       sceneAccessCleanup=dict(values=dict.fromkeys(CONTROLS, 0), errors={}, restored=True),
                       ownedExecution=dict(stats=owned, sceneSplit=dict(publication=publication)))
        self.assertEqual(validate_capture(capture, requested=True), checked)
        self.assertIsNone(validate_capture({}, requested=False))
        for key, value in (('passed', False), ('sceneSplitEyes', 'one'), ('sceneAccessRequested', False)):
            bad = copy.deepcopy(capture);bad[key] = value
            with self.assertRaises(ValueError):validate_capture(bad, requested=True)
        for value in (1, False):
            bad = copy.deepcopy(capture);bad['sceneAccessCleanup']['values'][CONTROLS[0]] = value
            with self.assertRaises(ValueError):validate_capture(bad, requested=True)
        bad = copy.deepcopy(capture);bad['sceneAccess']['reorderPermission'] = True
        with self.assertRaises(ValueError):validate_capture(bad, requested=True)


if __name__ == '__main__':
    unittest.main()
