import struct
import unittest

import gpu_trace_shaders as g


class GpuTraceShadersTest(unittest.TestCase):
    def test_parse_samples(self):
        storage = struct.pack('<QII', 0x2000612C0, 0, (1 << 16) | (8 << 8) | 0x31)
        storage += struct.pack('<QII', 0x200061300, 1, (19 << 8) | 0x02)
        self.assertEqual(list(g.parse_samples(storage)), [(0x2000612C0, 8, 1, 0x31), (0x200061300, 19, 0, 2)])

    def test_parse_rejects_partial_record(self):
        with self.assertRaises(ValueError):
            list(g.parse_samples(b'\0' * 15))

    def test_block_lookup(self):
        blocks = g.Blocks([(0x2000, 0x100, 7), (0x1000, 0x80, 3)])
        self.assertEqual(blocks.find(0x1000), 3)
        self.assertEqual(blocks.find(0x107F), 3)
        self.assertIsNone(blocks.find(0x1080))
        self.assertEqual(blocks.find(0x20FF), 7)
        self.assertIsNone(blocks.find(0x2100))
        self.assertIsNone(blocks.find(0x10))

    def test_reason_names(self):
        self.assertEqual(g.reason_name(8), 'long_scoreboard')
        self.assertEqual(g.reason_name(19), 'drain')
        self.assertEqual(g.reason_name(5), 'code5')

    def test_sections_rejects_other_files(self):
        with self.assertRaises(ValueError):
            g.sections(b'PK\x03\x04' + b'\0' * 64)


if __name__ == '__main__':
    unittest.main()
