import sys
import unittest

import control_ab
import node_cpu_audit
import node_cut_ab
import second_view_cost as svc
import thread_stack_sampler as sampler


class SecondViewBoundTest(unittest.TestCase):
    def rows(self, on, repeat, off):
        thread = lambda game: dict(gameThread=game, workersTotal=600, workersMean=40, processTotal=700)
        return [dict(label='both_views', frameRate=dict(fps=on), threads=thread(96)),
                dict(label='both_views_repeat', frameRate=dict(fps=repeat), threads=thread(94)),
                dict(label='main_only', frameRate=dict(fps=off), threads=thread(88))]

    def test_bound_uses_both_stereo_intervals(self):
        value = svc.bound(self.rows(66, 68, 100))
        self.assertAlmostEqual(value['fpsBothViews'], 67)
        self.assertAlmostEqual(value['driftFps'], 2)
        self.assertAlmostEqual(value['secondViewFrameCostMs'], 1000 / 67 - 10)
        self.assertAlmostEqual(value['maxGainPercent'], 100 * (100 / 67 - 1))
        self.assertAlmostEqual(value['gameThreadBothViews'], 95)

    def test_mono_interval_is_last(self):
        self.assertEqual([name for name, _ in svc.INTERVALS], ['both_views', 'both_views_repeat', 'main_only'])
        self.assertEqual([value for _, value in svc.INTERVALS], ['1', '1', '0'])

    def test_thread_summary(self):
        value = svc.summarize_threads(dict(total=300, threads=[
            dict(name='GameThread', percentOfCore=90), dict(name='redDispatcher1', percentOfCore=40),
            dict(name='redDispatcher2', percentOfCore=60), dict(name='', percentOfCore=10)]))
        self.assertEqual(value['gameThread'], 90)
        self.assertEqual(value['workersTotal'], 100)
        self.assertEqual(value['workersMean'], 50)


class SamplerAggregationTest(unittest.TestCase):
    def test_symbolize_folds_exe_frames_to_function_starts(self):
        mods = [(0x1000, 0x9000, 'Game.exe'), (0x20000, 0x30000, 'Plugin.dll')]
        stacks = [[0x1000 + 0x250, 0x1000 + 0x1105, 0x20000 + 0x1234]]
        named = sampler.symbolize(stacks, mods, [0x100, 0x200, 0x1100], 'Game.exe', {0x20000 + 0x1233: 'Hook'})
        # Leaf keeps its own address; callers are return addresses, looked up one byte earlier.
        self.assertEqual(named, [['exe+200', 'exe+1100', 'Plugin.dll!Hook']])
        self.assertEqual(sampler.symbolize([[0x50]], mods, [], 'Game.exe'), [['?']])

    def test_inclusive_counts_each_function_once_per_sample(self):
        value = sampler.aggregate([['a', 'b', 'a'], ['b'], []])
        inclusive = dict(value['inclusive'])
        self.assertEqual(value['samples'], 3)
        self.assertAlmostEqual(inclusive['a'], 33.33)
        self.assertAlmostEqual(inclusive['b'], 66.67)
        self.assertEqual(dict(value['exclusive'])['a'], 33.33)

    def test_pdata_reader_on_this_interpreter(self):
        starts = sampler.pdata_starts(sys.executable)
        self.assertGreater(len(starts), 0)
        self.assertEqual(starts, sorted(starts))


class ControlAbTest(unittest.TestCase):
    def row(self, label, fps):
        return dict(label=label, frameRate=dict(fps=fps), threads=dict(gameThread=90, workersTotal=600))

    def test_abba_order(self):
        self.assertEqual(control_ab.order(2), ['A', 'B', 'B', 'A', 'A', 'B', 'B', 'A'])

    def test_summary_reports_separation_and_delta(self):
        rows = [self.row(l, f) for l, f in zip(control_ab.order(1), (70, 74, 75, 71))]
        value = control_ab.summarize(rows)
        self.assertAlmostEqual(value['A']['fpsMean'], 70.5)
        self.assertAlmostEqual(value['B']['fpsMean'], 74.5)
        self.assertTrue(value['separated'])
        self.assertAlmostEqual(value['deltaFrameMs'], 1000 / 74.5 - 1000 / 70.5)
        overlapping = [self.row(l, f) for l, f in zip(control_ab.order(1), (70, 72, 69, 73))]
        self.assertFalse(control_ab.summarize(overlapping)['separated'])

    def test_present_timing_splits_the_interval(self):
        f = 10_000_000
        before = (100, 0, 0, 0, 0, 0, f, 0)
        ms = f // 1000
        after = (110, 130 * ms, 10 * ms, 40 * ms, 5 * ms, 60 * ms, f, 0)
        value = control_ab.present_timing(before, after)
        self.assertEqual(value['presents'], 10)
        self.assertAlmostEqual(value['interval'], 13.0)
        self.assertAlmostEqual(value['workMs'], 13.0 - 1.0 - 4.0 - 0.5)
        self.assertAlmostEqual(value['outsideHookMs'], 7.0)
        self.assertIsNone(control_ab.present_timing(after, after))

    def test_frame_time_costs_show_cpu_savings_at_flat_fps(self):
        def row(label, workers):
            return dict(label=label, frameRate=dict(fps=72, medianGpuMs=11.0, medianFrameIntervalMs=13.9),
                        threads=dict(gameThread=90, workersTotal=workers))
        value = control_ab.summarize([row(l, w) for l, w in zip(control_ab.order(1), (600, 540, 545, 605))])
        self.assertFalse(value['separated'])
        self.assertTrue(value['workerCpuSeparated'])
        self.assertAlmostEqual(value['A']['workerCpuMsPerFrame'], 6.025 * 1000 / 72)
        self.assertAlmostEqual(value['deltaWorkerCpuMsPerFrame'], (5.425 - 6.025) * 1000 / 72)
        self.assertAlmostEqual(value['deltaGpuMs'], 0.0)


class NodeCpuAuditTest(unittest.TestCase):
    def test_rows_normalize_by_modal_frame_count(self):
        lines = ['[prof] CFG|x=1', '[prof] AUDIT|nodes=3|window_ms=100.0',
                 '[prof] N|1|1|0x1EC1D0|SceneDrv|10|10|5.0|8.0|9.0|12.0',
                 '[prof] N|2|2|0x23A938|RenderElements|10|10|2.0|3.0|2.0|3.0',
                 '[prof] N|3|0|0x209FF0|RenderFinal2D|10|0|1.0|0.0|1.0|0.0']
        value = node_cpu_audit.rows_from_dump(lines)
        self.assertEqual((value['framesMain'], value['framesVrcam']), (10, 10))
        self.assertAlmostEqual(value['totalSelfMainMs'], 0.8)
        self.assertAlmostEqual(value['totalSelfVrcamMs'], 1.1)
        self.assertEqual(value['rows'][0]['name'], 'SceneDrv')
        self.assertAlmostEqual(value['rows'][0]['selfVrcamMs'], 0.8)


class NodeCutRequestTest(unittest.TestCase):
    def test_pack_matches_the_plugin_layout(self):
        value = node_cut_ab.pack(0x23A938, 1)
        self.assertEqual(value >> 63, 1)
        self.assertEqual(value & 0xFFFFFFFF, 0x23A938)
        self.assertEqual((value >> 32) & 0xFF, 0xFF)
        self.assertEqual((value >> 40) & 0xFF, 1)
        self.assertEqual((node_cut_ab.pack(0x1EC1D0, 0, 7) >> 32) & 0xFF, 7)
        for bad in ((0, 1), (0x1EC1D0, 4), (2 ** 32, 1)):
            with self.assertRaises(ValueError):
                node_cut_ab.pack(*bad)

    def test_rule_parsing(self):
        self.assertEqual(node_cut_ab.parse_rule('23A938'), (0x23A938, 0xFF))
        self.assertEqual(node_cut_ab.parse_rule('0x1EC1D0:5'), (0x1EC1D0, 5))


if __name__ == '__main__':
    unittest.main()
