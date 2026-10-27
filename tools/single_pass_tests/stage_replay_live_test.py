"""Decoder/summary tests for stage_replay_live.py, kept in sync with the C++ header by parsing it."""
import struct
import unittest

import stage_replay_live as live


class EnumSyncTest(unittest.TestCase):
    def test_ops_follow_the_header_order(self):
        names = live.op_names()
        self.assertEqual(names[0], 'None')
        self.assertEqual(names[-1], 'Count')
        for name in ('Pipeline', 'RootTable', 'DrawIndexed', 'Indirect', 'Copy', 'Barrier', 'Other'):
            self.assertIn(name, names)
        self.assertLess(names.index('Indirect'), names.index('Marker'))
        self.assertEqual(live.BLOCKING, set(names[names.index('Dispatch'):names.index('Count')]))

    def test_columns_match_the_runtime_enum(self):
        cols = live.columns()
        self.assertEqual(len(cols), 32)
        self.assertEqual(cols[0], 'Calls')
        self.assertEqual(cols[-1], 'Packets')

    def test_fallback_names(self):
        names = live.fallback_names()
        self.assertEqual(names[0], 'None')
        self.assertIn('StaleFrame', names)


class DecodeTest(unittest.TestCase):
    def test_commands_and_copy_sources(self):
        names = live.op_names()
        payload = struct.pack('<3Q', 7, 8, 9)
        blob = live.COMMAND.pack(names.index('Copy'), 2, 3, 40, 0, 0, 0)
        blob += live.COMMAND.pack(names.index('RootTable'), 2, 1, 4, 0x1000, 40, 0)
        blob += live.COMMAND.pack(names.index('DrawIndexed'), 0, 0, 36, 1, 0, 2 ** 64 - 1)
        rows = live.decode_commands(blob, payload, names)
        self.assertEqual([row['op'] for row in rows], ['Copy', 'RootTable', 'DrawIndexed'])
        self.assertEqual(rows[0]['sources'], [7, 8, 9])
        summary = live.summarize_stream(rows)
        self.assertEqual(summary['draws'], 1)
        self.assertEqual(summary['cameraTables'], 1)
        self.assertEqual(summary['cameraTableParams'], [4])
        self.assertEqual(summary['tablesMatched'], 1)

    def test_compare_streams_aligns_draws_by_pipeline(self):
        def draws(*psos):
            out = []
            for pso in psos:
                out.append(dict(op='Pipeline', b=pso))
                out.append(dict(op='DrawIndexed', a=36))
            return out
        value = live.compare_streams(draws(1, 2, 3, 4), draws(1, 2, 4))
        self.assertEqual(value['matchedDraws'], 3)
        self.assertEqual((value['mainDraws'], value['vrcamDraws']), (4, 3))

    def test_stage_mask(self):
        self.assertEqual(live.parse_stages('7,9'), (1 << 7) | (1 << 9))
        self.assertEqual(live.parse_stages(''), 0)


class TimelineTest(unittest.TestCase):
    def test_events_decode_and_order(self):
        def word(frame, side, stage, ordinal, kind):
            return frame | side << 32 | stage << 40 | ordinal << 48 | kind << 56
        words = []
        # frame 7: MAIN partitions 0..1 end at 30, VRCAM starts at 40 -> MAIN first
        words += [word(7, 0, 6, 0, 1), 1, 10, 20, 0, 0, word(7, 0, 6, 1, 1), 2, 15, 30, 0, 0, word(7, 1, 6, 0, 0), 3, 40, 90, 0, 0]
        # frame 8: VRCAM ends before MAIN starts
        words += [word(8, 1, 6, 0, 0), 3, 100, 110, 0, 0, word(8, 0, 6, 0, 1), 1, 120, 130, 0, 0]
        # frame 9: overlap; present event ignored
        words += [word(9, 1, 6, 0, 0), 3, 200, 260, 0, 0, word(9, 0, 6, 0, 1), 1, 210, 230, 0, 0, word(9, 255, 255, 0, 5), 4, 300, 300, 0, 0]
        events = live.decode_events(words)
        self.assertEqual(events[0]['kind'], 'capture')
        self.assertEqual(events[-1]['kind'], 'present')
        value = live.order_summary(events, 6, 1000)
        self.assertEqual((value['mainFinishesFirst'], value['vrcamFinishesFirst'], value['overlap']), (1, 1, 1))
        self.assertEqual(value['frames'], 3)

    def test_cpu_per_frame(self):
        def ev(frame, side, start, end):
            return dict(frame=frame, side=side, stage=6, start=start, end=end)
        events = [ev(1, 1, 0, 30), ev(1, 1, 10, 20), ev(1, 0, 100, 140), ev(1, 0, 110, 115),
                  ev(2, 1, 0, 10), ev(2, 0, 50, 60), dict(frame=2, side=255, stage=255, start=70, end=70)]
        value = live.cpu_per_frame(events, 1000, trim=0)
        self.assertEqual(value['frames'], 2)
        self.assertAlmostEqual(value['mainCpuMs'], (45 + 10) / 2 * 1000 / 1000)
        self.assertAlmostEqual(value['vrcamSpanMs'], (30 + 10) / 2)
        self.assertAlmostEqual(value['bothSpanMs'], (140 + 60) / 2)
        self.assertIsNone(live.cpu_per_frame([], 1000))

    def test_barrier_payload_decode(self):
        names = live.op_names()
        payload = live.BARRIER.pack(0, 0, 0xABC, 0xFFFFFFFF, 0x400, 0x1)
        blob = live.COMMAND.pack(names.index('Barrier'), 0, 1, 0, 0xABC, 0, 0)
        row = live.decode_commands(blob, payload, names)[0]
        self.assertEqual(row['barriers'][0]['resource'], 0xABC)
        self.assertEqual((row['barriers'][0]['before'], row['barriers'][0]['after']), (0x400, 0x1))


if __name__ == '__main__':
    unittest.main()
