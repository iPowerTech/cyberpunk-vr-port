import unittest

import port_gpu_stages as stages


class SummarizeTest(unittest.TestCase):
    def test_per_frame_sum_and_order(self):
        report = {'pending': False, 'batches': 32, 'invalid': 0, 'stages': [
            {'name': 'CaptureMain', 'count': 32, 'mean_us': 50.0, 'max_us': 70.0},
            {'name': 'SubmitColor', 'count': 32, 'mean_us': 120.0, 'max_us': 200.0},
            {'name': 'HudBlur', 'count': 16, 'mean_us': 40.0, 'max_us': 41.0},
            {'name': 'SecondHud', 'count': 0, 'mean_us': 0.0, 'max_us': 0.0}]}
        value = stages.summarize(report)
        self.assertEqual(value['frames'], 32)
        self.assertEqual([s['name'] for s in value['stages']], ['SubmitColor', 'CaptureMain', 'HudBlur'])
        self.assertAlmostEqual(value['perFrameUs'], 50 + 120 + 40 * 16 / 32)

    def test_empty_report(self):
        self.assertEqual(stages.summarize({})['perFrameUs'], 0.0)


if __name__ == '__main__':
    unittest.main()
