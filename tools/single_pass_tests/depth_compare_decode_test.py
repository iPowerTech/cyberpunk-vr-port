import struct
import unittest
from read_constants import decode_depth_comparison


class DepthCompareTests(unittest.TestCase):
    def test_only_nonblank_exact_outputs_pass(self):
        row = [128, 64, 3, 1, 0, 0, 0, 0, 400, 500, 0, 0, 0.0, 0.0]
        encode = lambda: struct.pack('<4I8Q2f', *row)
        self.assertTrue(decode_depth_comparison(encode())['equal'])
        for index in (4, 5, 6, 7, 10, 11):
            row[index] = 1
            self.assertFalse(decode_depth_comparison(encode())['equal'])
            row[index] = 0
        row[8] = 0
        self.assertFalse(decode_depth_comparison(encode())['equal'])

    def test_invalid_counts_and_incomplete_results_rejected(self):
        row = [128, 64, 3, 1, 0, 0, 0, 0, 400, 500, 0, 0, 0.0, 0.0]
        for index, value in ((0, 0), (1, 4097), (2, 0), (3, 0), (4, 8193), (12, float('nan')), (13, -1.0)):
            bad = row.copy()
            bad[index] = value
            with self.assertRaises(ValueError):
                decode_depth_comparison(struct.pack('<4I8Q2f', *bad))


if __name__ == '__main__':
    unittest.main()
