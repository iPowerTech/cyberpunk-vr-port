import struct
import unittest
from read_geometry_leases import decode


def fixture():
    native = bytearray(1096)
    struct.pack_into('<I', native, 40, 1)
    struct.pack_into('<I', native, 48, 1)
    struct.pack_into('<QII', native, 96, 0x10100, 256, 57)
    struct.pack_into('<QII', native, 224, 0x10200, 256, 48)
    struct.pack_into('<I', native, 1092, 128)
    row = [1,1,128,128] + [0]*16 + [15,1,1,0]
    row[11] = 1
    resources = bytes(32)+struct.pack('<3Q2I', 0x9000, 0x10000, 4096, 1, 0)
    return bytearray(struct.pack('<24I', *row)), resources, native


class LeaseDecodeTests(unittest.TestCase):
    def test_owned_resources_and_native_identity(self):
        draw, resources, native = fixture()
        rows, owners = decode(draw, resources, native)
        self.assertEqual(rows[0]['flags'], 15)
        self.assertEqual(owners[1]['bytes'], 4096)

    def test_stale_eye_out_of_range_and_missing_owner_rejected(self):
        for offset, value in ((22*4,0), (11*4,99), (4,0), (3*4,0)):
            draw, resources, native = fixture()
            struct.pack_into('<I', draw, offset, value)
            with self.assertRaises(ValueError):
                decode(draw, resources, native)
        draw, resources, native = fixture()
        struct.pack_into('<Q', native, 224, 0x20000)
        with self.assertRaises(ValueError):
            decode(draw, resources, native)

    def test_dynamic_state_bit_does_not_hide_incomplete_ownership(self):
        draw, resources, native = fixture()
        struct.pack_into('<I', draw, 20*4, 31)
        rows, _ = decode(draw, resources, native)
        self.assertEqual(rows[0]['flags'], 31)
        struct.pack_into('<I', draw, 4, 0)
        with self.assertRaises(ValueError):
            decode(draw, resources, native)


if __name__ == '__main__':
    unittest.main()
