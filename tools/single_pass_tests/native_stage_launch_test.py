import fnmatch
import unittest
from unittest.mock import patch

from read_preparation import main


class NativeStageLaunchTests(unittest.TestCase):
    def test_live_reader_requests_every_stage_export_before_process_access(self):
        class Resolved(Exception):
            pass

        with patch('live_symbols.resolve', side_effect=Resolved) as resolve:
            with patch('live_read.Reader') as reader:
                with self.assertRaises(Resolved):
                    main(['--pid', '1', '--out', 'unused.json'])
                reader.assert_not_called()
        patterns = resolve.call_args.args[2]
        for name in ('CyberpunkVR_OwnedStageStats', 'CyberpunkVR_OwnedStageDefinitions',
                     'CyberpunkVR_OwnedStageEntries', 'CyberpunkVR_OwnedSceneStats',
                     'CyberpunkVR_OwnedSceneRoots', 'CyberpunkVR_OwnedSceneChildren',
                     'CyberpunkVR_OwnedSceneSplitStats', 'CyberpunkVR_OwnedSceneSplitSections'):
            self.assertTrue(any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns), name)

    def test_two_eye_reader_implies_split_and_coupled_before_process_access(self):
        class Resolved(Exception):
            pass

        with patch('live_symbols.resolve', side_effect=Resolved), patch('live_read.Reader') as reader:
            with self.assertRaises(Resolved):
                main(['--pid', '1', '--out', 'unused.json', '--scene-split-eyes', 'both',
                      '--retained-geometry', 'reference', '--retained-stage', '7', '--pass-targets',
                      '--merge-root', '--schedule-preparation', 'apply'])
            reader.assert_not_called()


if __name__ == '__main__':
    unittest.main()
