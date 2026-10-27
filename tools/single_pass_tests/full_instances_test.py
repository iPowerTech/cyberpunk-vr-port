import struct
import unittest
from verify_full_instances import full_batch, verify


def fixture(count=3, origin=2, side=1, offset=0):
    draw, packet = bytearray(1096), bytearray(256)
    struct.pack_into('<I', draw, 40, 1)
    struct.pack_into('<I', draw, 48, side)
    struct.pack_into('<I', draw, 56, count)
    struct.pack_into('<I', draw, 68, 2)
    struct.pack_into('<QII', draw, 224, 0x1000, 4096, 48)
    struct.pack_into('<Q', draw, 992, 0x9000)
    struct.pack_into('<I', draw, 1092, 128)
    struct.pack_into('<4I', packet, 0, 127, count, 0, origin)
    struct.pack_into('<3I', packet, 216, offset, count*48, 48)
    struct.pack_into('<2Q', packet, 240, 0x1060, 0x9000)
    data = bytes(range(48))*count
    if origin == 1 and count == 1:
        struct.pack_into('<I', draw, 1016, 48)
        draw[1024:1072] = data
    return draw, packet, data


class FullInstanceTests(unittest.TestCase):
    def test_full_range_and_every_byte_match(self):
        left, a, data = fixture()
        right, b, _ = fixture(side=0, offset=len(data))
        pairs = [dict(draws=[1,2], packets=[0,1])]
        result = verify(pairs, left+right, a+b, data+data)
        self.assertEqual(result['count'], 1)
        self.assertEqual(result['pairs'][0]['instances'], 3)
        with self.assertRaisesRegex(ValueError, 'differ'):
            verify(pairs, left+right, a+b, data+data[:-1]+b'X')

    def test_partial_packet_or_gpu_offset_not_accepted(self):
        for at, value in ((4,2), (220,48), (240,0x1090), (248,0x9001)):
            draw, packet, data = fixture()
            struct.pack_into('<Q' if at>=240 else '<I', packet, at, value)
            with self.assertRaises(ValueError):
                full_batch(draw, packet, data)

    def test_native_range_and_used_slot_required(self):
        for at, value in ((68,100), (1092,0)):
            draw, packet, data = fixture()
            struct.pack_into('<I', draw, at, value)
            with self.assertRaises(ValueError):
                full_batch(draw, packet, data)

    def test_cpu_single_requires_observed_complete_bytes(self):
        draw, packet, data = fixture(count=1, origin=1)
        self.assertEqual(full_batch(draw, packet, data)[0], data)
        draw[1071] ^= 1
        with self.assertRaises(ValueError):
            full_batch(draw, packet, data)

    def test_wrong_eye_and_frame_rejected(self):
        left, a, data = fixture()
        right, b, _ = fixture(side=1, offset=len(data))
        with self.assertRaisesRegex(ValueError, 'identity'):
            verify([dict(draws=[1,2], packets=[0,1])], left+right, a+b, data+data)


if __name__ == '__main__':
    unittest.main()
