import copy
import unittest
from unittest.mock import Mock, patch

from owned_execution_report_test import complete_compiler
from run_launch_tests import run_reversible_capture, validate_scene_split_repeat
from scene_split_report import validate_publication
from scene_split_report_test import publication_fixture


def capture_fixture(both, epoch):
    capture, frontier = publication_fixture(both)
    capture.update(pid=42, dll='test-build', submissionFrontier=frontier, sceneSplitEyes='both' if both else 'one')
    execution = capture['ownedExecution']
    stats = execution['stats']
    stats[1] += epoch - 1
    stats[2] = epoch
    stats[3] += epoch - 1
    execution.update(compiler=complete_compiler(stats, execution['resources']), closing=0, sequence=epoch * 2)
    execution['sceneSplit']['stats'][2:5] = stats[1:4]
    for key, value in zip(('consumerId', 'epoch', 'pairGeneration'), stats[1:4]):
        execution['sceneSplit'][key] = execution['sceneSections'][key] = value
    frontier['ranges'][0][18:21] = stats[1:4]
    execution['sceneSplit']['publication'] = validate_publication(capture, frontier=frontier, require_frontier=True)
    return capture


class SceneSplitTurnTest(unittest.TestCase):
    def test_repeat_and_turn_preserve_schema1_or_schema2(self):
        for both in (False, True):
            first, repeat, turned = [capture_fixture(both, epoch) for epoch in (1, 2, 3)]
            self.assertTrue(validate_scene_split_repeat(first, repeat)['verified'])
            self.assertTrue(validate_scene_split_repeat(repeat, turned)['verified'])

    def test_turned_native_draw_counts_are_observed_not_fixed(self):
        before, after = capture_fixture(True, 2), capture_fixture(True, 3)
        for group in after['passTargets']['groups']:
            group['draws'] = group['nativeDraws'] = group['knownTargets'] = group['draws'] * 3 + 1
            end = next(row for row in after['records'] if row.get('kind') == 14 and row['scope'] == group['scope'])
            end['flags0'] = group['draws']
        after['ownedExecution']['sceneSplit']['publication'] = validate_publication(
            after, frontier=after['submissionFrontier'], require_frontier=True)
        result = validate_scene_split_repeat(before, after)
        self.assertEqual([row['draws'] for row in result['publication']['lists']], [16, 28, 13, 25])

    def test_selected_eye_mode_cannot_change_on_turn(self):
        with self.assertRaisesRegex(ValueError, 'selected eye mode'):
            validate_scene_split_repeat(capture_fixture(False, 2), capture_fixture(True, 3))

    def test_turn_needs_fresh_same_process_consumer(self):
        first, second = capture_fixture(True, 2), capture_fixture(True, 3)
        for key, value in (('pid', 99), ('dll', 'another-build'), ('passed', False)):
            bad = copy.deepcopy(second)
            bad[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_scene_split_repeat(first, bad)
        with self.assertRaisesRegex(ValueError, 'fresh epoch'):
            validate_scene_split_repeat(first, first)

    def test_turn_revalidates_actual_native_publication(self):
        before, after = capture_fixture(True, 2), capture_fixture(True, 3)
        for row in range(4):
            bad = copy.deepcopy(after)
            bad['submissionFrontier']['slots'][row][19] = 0
            with self.subTest(row=row), self.assertRaises(ValueError):
                validate_scene_split_repeat(before, bad)
        bad = copy.deepcopy(after)
        del bad['submissionFrontier']
        with self.assertRaisesRegex(ValueError, 'requires native submission'):
            validate_scene_split_repeat(before, bad)

    def test_derived_publication_cannot_diverge_from_evidence(self):
        before, after = capture_fixture(True, 2), capture_fixture(True, 3)
        after['ownedExecution']['sceneSplit']['publication']['lists'][3]['nativeGeneration'] += 1
        with self.assertRaisesRegex(ValueError, 'actual native evidence'):
            validate_scene_split_repeat(before, after)

    def test_turn_requires_all_control_cleanup(self):
        before, after = capture_fixture(True, 2), capture_fixture(True, 3)
        for key in ('sceneSplitControlCleanup', 'sceneSplitFrontierCleanup'):
            bad = copy.deepcopy(after)
            bad[key] = 1
            with self.subTest(key=key), self.assertRaises(ValueError):
                validate_scene_split_repeat(before, bad)


class ReversibleCaptureTest(unittest.TestCase):
    def run_probe(self, failure=None, *, cleanup_failure=None):
        self.events = []
        self.variants = {}
        counts = {}

        def event(name, result=None):
            counts[name] = counts.get(name, 0) + 1
            self.events.append(name)
            if failure in (name, (name, counts[name])):
                raise RuntimeError('primary test failure: ' + name)
            if name == 'restore' and cleanup_failure:
                raise RuntimeError(cleanup_failure)
            return result

        turn = Mock()
        turn.apply.side_effect = lambda index: event('apply' + str(index), {'yaw': index})
        turn.restore.side_effect = lambda: event('restore', 'restored')
        with patch('run_launch_tests.verify_motion', side_effect=lambda *images: event('motion', dict(images=images))):
            return run_reversible_capture(turn, self.variants,
                lambda: event('capture', {'proof': True}),
                lambda label: event('shot-' + label),
                lambda: event('hud', {'stable': True}),
                lambda label: event('image-' + label, label.encode()))

    def test_success_returns_capture_only_after_pose_and_hud_cleanup(self):
        self.assertEqual(self.run_probe(), {'proof': True})
        self.assertEqual(self.events, ['shot-initial', 'apply1', 'hud', 'capture', 'shot-turned', 'apply0', 'hud',
                                      'shot-returned', 'image-initial', 'image-turned', 'image-returned', 'motion', 'restore', 'hud'])
        self.assertTrue(self.variants['passed'])
        self.assertEqual(self.variants['poseCleanup'], 'restored')
        self.assertTrue(self.variants['cleanupHud']['stable'])

    def test_every_intermediate_failure_still_restores_pose_and_checks_hud(self):
        for failure in ('shot-initial', 'apply1', ('hud', 1), 'capture', 'shot-turned', 'apply0',
                        ('hud', 2), 'shot-returned', 'image-initial', 'image-turned', 'image-returned', 'motion'):
            with self.subTest(failure=failure), self.assertRaisesRegex(RuntimeError, 'primary test failure'):
                self.run_probe(failure)
            self.assertEqual(self.events[-2:], ['restore', 'hud'])
            self.assertFalse(self.variants['passed'])

    def test_cleanup_failure_does_not_replace_primary_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'primary test failure: capture'):
            self.run_probe('capture', cleanup_failure='restoration failed')
        self.assertEqual(self.variants['poseCleanupError'], 'restoration failed')
        self.assertFalse(self.variants['passed'])

    def test_failed_pose_cleanup_cannot_report_success(self):
        with self.assertRaisesRegex(RuntimeError, 'restoration failed'):
            self.run_probe(cleanup_failure='restoration failed')
        self.assertFalse(self.variants['passed'])

    def test_failed_post_cleanup_hud_cannot_report_success(self):
        with self.assertRaisesRegex(RuntimeError, 'primary test failure: hud'):
            self.run_probe(('hud', 3))
        self.assertFalse(self.variants['passed'])
        self.assertEqual(self.variants['poseCleanup'], 'restored')
        self.assertIn('poseCleanupError', self.variants)


if __name__ == '__main__':
    unittest.main()
