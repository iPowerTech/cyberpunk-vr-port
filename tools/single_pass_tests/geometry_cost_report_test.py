import unittest

import geometry_cost_report as gc


def group(node, side, ms, ranges=1, commands=1):
    return dict(node=node, side=side, pso=0, ranges=ranges, commands=commands, lists=1, detail=0, sum_ms=ms, max_ms=ms)


def profile(re0, re1, frames=(4, 4), other=1.0):
    groups = [group(gc.RENDER_ELEMENTS, 0, re0, 100, 700), group(gc.RENDER_ELEMENTS, 1, re1, 100, 700),
              group(0x37D5C4, 0, other), group(0x37D5C4, 1, other)]
    for side in (0, 1):
        for node in gc.FRAME_NODES:
            groups.append(group(node, side, .1, frames[side], frames[side]))
    return dict(active=False, completed=256, discarded=0, groups=groups)


def interval(label, control, re0, re1, fps, hits=None, frames=(4, 4)):
    value = profile(re0, re1, frames)
    summary = gc.summarize_profile(value)
    counts = gc.profile_frames(value)
    return dict(label=label, control=control, controlAfter=control, hits=(1000 if control else 0) if hits is None else hits,
                frameRate=dict(fps=fps), profile=summary, profileFrames=counts,
                perFrame={side: gc.per_frame(summary, counts[side])[side] for side in (0, 1)})


class GeometryCostReportTest(unittest.TestCase):
    def test_summary_separates_render_elements_and_totals(self):
        summary = gc.summarize_profile(profile(4.0, 5.0))
        self.assertAlmostEqual(summary['sides'][0]['renderElementsMs'], 4.0)
        self.assertAlmostEqual(summary['sides'][1]['renderElementsMs'], 5.0)
        self.assertGreater(summary['sides'][0]['totalMs'], 4.0)

    def test_incomplete_profile_is_rejected(self):
        with self.assertRaises(ValueError):
            gc.summarize_profile(dict(active=True, completed=0, groups=[]))
        with self.assertRaises(ValueError):
            gc.summarize_profile(dict(error='report overflow'))

    def test_frames_use_once_per_frame_nodes(self):
        self.assertEqual(gc.profile_frames(profile(1, 1, frames=(4, 5))), {0: 4, 1: 5})
        with self.assertRaises(ValueError):
            gc.profile_frames(dict(groups=[group(gc.RENDER_ELEMENTS, 0, 1)]))

    def test_frame_rate_from_real_counter(self):
        rows = [dict(time=0.0, report=dict(real=100, gpu_ms=13.0, gpu_usage=96)),
                dict(time=2.0, report=dict(real=246, gpu_ms=14.0, gpu_usage=97))]
        rate = gc.frame_rate(rows)
        self.assertAlmostEqual(rate['fps'], 73.0)
        self.assertAlmostEqual(rate['medianGpuMs'], 13.5)
        with self.assertRaises(ValueError):
            gc.frame_rate(rows[:1])
        with self.assertRaises(ValueError):
            gc.frame_rate([rows[0], dict(time=1.0, report=dict(real=100))])

    def test_ceiling_uses_unaffected_eye_as_reference(self):
        results = [interval('baseline', 0, 4.0, 4.0, 73),
                   # main vertex-only: main RenderElements halves while vrcam is unchanged
                   interval('main_vertex', 0x11, 2.0, 4.0, 75),
                   interval('vrcam_vertex', 0x21, 4.0, 1.6, 76),
                   interval('main_skip', 0x12, 0.2, 4.0, 78),
                   interval('vrcam_skip', 0x22, 4.0, 0.2, 78),
                   interval('both_vertex', 0x31, 2.0, 1.6, 79),
                   interval('baseline_end', 0, 4.0, 4.0, 73)]
        self.assertTrue(gc.validate(results))
        value = gc.ceiling(results)
        self.assertAlmostEqual(value['baselineFps'], 73)
        self.assertAlmostEqual(value['main']['vertexRemainingFraction'], .5)
        self.assertAlmostEqual(value['vrcam']['vertexRemainingFraction'], .4)
        self.assertAlmostEqual(value['main']['nativeRenderElementsMs'], 1.0)
        self.assertAlmostEqual(value['main']['vertexOnlyRenderElementsMs'], .5)
        self.assertAlmostEqual(value['main']['pixelShareOfRenderElements'], .5)

    def test_validate_requires_engagement_only_when_active(self):
        good = [interval(name, control, 1, 1, 70) for name, control in gc.CONFIGS]
        self.assertTrue(gc.validate(good))
        idle = [dict(row) for row in good]
        idle[1]['hits'] = 0
        with self.assertRaises(ValueError):
            gc.validate(idle)
        leaked = [dict(row) for row in good]
        leaked[0]['hits'] = 5
        with self.assertRaises(ValueError):
            gc.validate(leaked)
        changed = [dict(row) for row in good]
        changed[2]['controlAfter'] = 0
        with self.assertRaises(ValueError):
            gc.validate(changed)
        with self.assertRaises(ValueError):
            gc.validate(good[1:])

    def test_configs_are_valid_controls(self):
        for name, control in gc.CONFIGS:
            self.assertEqual(control & ~0x33, 0, name)
            if control:
                self.assertIn(control & 3, (1, 2), name)
                self.assertTrue(control & 0x30, name)


if __name__ == '__main__':
    unittest.main()
