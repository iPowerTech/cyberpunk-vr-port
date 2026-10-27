import unittest

from PIL import Image

import stage_replay_sweep as sweep


class EyeDifferenceTest(unittest.TestCase):
    def test_only_the_changed_eye_differs(self):
        a = Image.new('RGB', (8, 2), (100, 100, 100))
        b = a.copy()
        for x in range(4, 8):
            b.putpixel((x, 0), (160, 160, 160))
        diff = sweep.eye_difference(a, b)
        self.assertEqual(diff['vrcam']['meanAbs'], 0)
        self.assertAlmostEqual(diff['main']['meanAbs'], 60 / 2)
        self.assertAlmostEqual(diff['main']['over24'], 0.5)


class VerdictTest(unittest.TestCase):
    def diff(self, main, vrcam, main_over=0.01, vrcam_over=0.01):
        return dict(main=dict(meanAbs=main, over24=main_over), vrcam=dict(meanAbs=vrcam, over24=vrcam_over))

    def test_noise_level_replay_passes(self):
        value = sweep.verdict(self.diff(1.1, 1.0), dict(replayed=100, fallbacks=1))
        self.assertTrue(value['ok'])

    def test_broken_eye_fails(self):
        self.assertFalse(sweep.verdict(self.diff(9.0, 1.0), dict(replayed=100, fallbacks=0))['ok'])
        self.assertFalse(sweep.verdict(self.diff(1.0, 1.0, main_over=0.2), dict(replayed=100, fallbacks=0))['ok'])

    def test_no_replay_or_fallbacks_fail(self):
        self.assertFalse(sweep.verdict(self.diff(1.0, 1.0), dict(replayed=0, fallbacks=0))['ok'])
        self.assertFalse(sweep.verdict(self.diff(1.0, 1.0), dict(replayed=50, fallbacks=50))['ok'])

    def test_mask(self):
        self.assertEqual(sweep.mask([3, 6]), (1 << 3) | (1 << 6))


if __name__ == '__main__':
    unittest.main()
