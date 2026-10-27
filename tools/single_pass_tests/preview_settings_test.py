import json
from pathlib import Path
import tempfile
import unittest
from preview_settings import PreviewSettings


class PreviewSettingsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / 'settings.json'
        self.settings = dict(window_width=2193, window_height=14, render_width=2064, render_height=2208,
                             full_render=False, preview_fps=90, ipd_mm=64, simulated_input=False)
        self.raw = json.dumps(self.settings, indent=4).encode()
        self.path.write_bytes(self.raw)
        self.preview = PreviewSettings(self.path)

    def test_render_settings_unchanged_and_original_bytes_restored(self):
        report = self.preview.start()
        expected = dict(self.settings, window_width=1280, window_height=720)
        self.assertEqual(json.loads(self.path.read_bytes()), expected)
        self.assertEqual(report['original']['window_height'], 14)
        self.assertTrue(self.preview.restore())
        self.assertEqual(self.path.read_bytes(), self.raw)
        self.assertFalse(self.preview.restore())

    def test_preview_save_shrink_does_not_accumulate_between_runs(self):
        self.preview.start()
        current = json.loads(self.path.read_bytes())
        current['window_height'] -= 30
        self.path.write_text(json.dumps(current))
        self.preview.restore()
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_unrelated_intervening_changes_survive(self):
        self.preview.start()
        current = json.loads(self.path.read_bytes())
        current['show_stats'] = True
        self.path.write_text(json.dumps(current))
        self.preview.restore()
        self.assertEqual(json.loads(self.path.read_bytes()), dict(self.settings, show_stats=True))

    def test_invalid_source_is_not_overwritten(self):
        self.path.write_text('{broken')
        with self.assertRaises(ValueError):
            self.preview.start()
        self.assertFalse(self.preview.restore())
        self.assertEqual(self.path.read_text(), '{broken')

    def test_invalid_restore_does_not_destroy_new_file(self):
        self.preview.start()
        self.path.write_text('[]')
        with self.assertRaises(ValueError):
            self.preview.restore()
        self.assertEqual(self.path.read_text(), '[]')

    def test_duplicate_start_rejected(self):
        self.preview.start()
        with self.assertRaises(RuntimeError):
            self.preview.start()
        self.preview.restore()
        self.assertEqual(self.path.read_bytes(), self.raw)


if __name__ == '__main__':
    unittest.main()
