import struct
import unittest
from analyze_constant_trace import analyze


def event(op, *args):
    return struct.pack('<8Q', op, *args, *([0] * (7-len(args))))


class ConstantTraceTests(unittest.TestCase):
    def test_queries_are_joined_to_eye_slot_and_native_cpu_caller(self):
        data = (event(16, 1, 4, 8, 1, 1) + event(17, 1, 1, 1, 4096, 848) +
                event(18, 1, 4096, 848, 4, 0) + event(13, 77, 0, 0x140001234, 0x140000000))
        report = analyze(data)
        self.assertEqual(report['querySlots'], {'1:b1:side=1:status=4:detail=0': 1})
        self.assertEqual(report['cpuCallers'], {'exe+1234:nested=0': 1})

    def test_submission_cardinality_and_order(self):
        analyze(event(7, 1, 2, 1) + event(8, 33) + event(9))
        for data in (event(7, 1, 2, 1), event(8, 33), event(7, 1, 2, 1) + event(9),
                     event(7, 1, 2, 1) + event(11, 1, 2, 3)):
            with self.assertRaises(ValueError):
                analyze(data)

    def test_rejects_truncated_unknown_and_unidentified_events(self):
        for data in (b'', event(1)[:-1], event(99), event(18, 1, 4096, 848, 4)):
            with self.assertRaises(ValueError):
                analyze(data)


if __name__ == '__main__':
    unittest.main()
