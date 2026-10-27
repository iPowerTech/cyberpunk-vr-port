import struct
import unittest
from read_constants import decode_geometry_freeze


class FreezeDecodeTests(unittest.TestCase):
    def test_only_completed_sealed_copies_are_ready(self):
        data = b''.join(struct.pack('<IIiIQ', seq, status, 0, 0, 512) for seq, status in ((1, 1), (2, 2), (3, 3)))
        result = decode_geometry_freeze(data, [1, 2, 3, 4])
        self.assertEqual(result['unready'], [1, 2, 4])
        self.assertEqual(result['bytes'], 1536)

    def test_duplicate_and_invalid_ready_records_fail(self):
        row = struct.pack('<IIiIQ', 1, 3, 0, 0, 512)
        for data in (row+row, struct.pack('<IIiIQ', 1, 3, -1, 0, 512), struct.pack('<IIiIQ', 1, 3, 0, 0, 0)):
            with self.assertRaises(ValueError):
                decode_geometry_freeze(data, [1])


if __name__ == '__main__':
    unittest.main()
