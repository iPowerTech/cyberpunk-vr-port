import struct
import unittest
from analyze_skinning_reads import resource, bone_range, deformation, merge, srv


def buffer():
    value = [0] * 20
    value[:3] = [2, 13, 96]
    value[4:11] = [39, 1, 5768, 0, 15728640, 0, 1]
    value[13:20] = [1, 62914560, 1 | (1 << 32) | (1 << 48), 1 << 32, 1, 2, 0x200000]
    return value


def texture():
    value = buffer()
    value[4:11] = [61, 4, 5768, 0, 0xffffffff, 0, 0]
    value[13:20] = [3 | (61 << 32), 256, 32 | (1 << 32) | (1 << 48), 0, 1, 1, 0]
    return value


def instance(base, stride=48):
    data = bytearray(64)
    struct.pack_into('<2I', data, 48, base, stride)
    return data


class SkinningReadsTest(unittest.TestCase):
    def test_metadata(self):
        self.assertEqual(resource(buffer())['heapType'], 2)
        self.assertEqual(resource(texture())['mips'], 1)
        for flags in (0, 1, 3, 7, 15):
            value = buffer()
            value[1] = flags
            with self.assertRaises(ValueError):
                resource(value)

    def test_bone_ranges(self):
        self.assertEqual(bone_range(buffer(), instance(676080))['bytes'], 12288)
        ranges = [bone_range(buffer(), instance(base)) for base in (676080, 673616, 672736, 668464, 760832)]
        self.assertEqual(merge(ranges), [dict(offset=668464, bytes=19904), dict(offset=760832, bytes=12288)])

    def test_invalid_ranges(self):
        for data in (instance(2), instance(0, 2), instance(0xfffffff0), instance(62914560), bytearray(63)):
            with self.assertRaises(ValueError):
                bone_range(buffer(), data)
        for index, value in ((7, 1), (7, 1 << 63), (8, 0), (9, 4), (10, 0), (14, 16)):
            v = buffer()
            v[index] = value
            with self.assertRaises(ValueError):
                bone_range(v, instance(0))

    def test_texture_mip(self):
        self.assertEqual(deformation(texture()), dict(mip=0, width=256, height=32, texelBytes=8192))
        v = texture()
        v[15] = 32 | (1 << 32) | (3 << 48)
        v[7] = 1
        self.assertEqual(deformation(v), dict(mip=1, width=128, height=16, texelBytes=2048))

    def test_unsupported_texture(self):
        for index, value in ((4, 28), (8, 0), (7, 1), (9, 1), (10, 1), (17, 4), (14, 0)):
            v = texture()
            v[index] = value
            with self.assertRaises(ValueError):
                deformation(v)

    def test_register_visibility(self):
        b = dict(kind=0, space=0, key=(0, 0, 0, 0, 2, 0, 1), register=10, count=2, ids=[1, 2])
        values = [[0] * 20, buffer(), texture()]
        self.assertEqual(srv({1: b}, values, 10), buffer())
        ps = {**b, 'key': (0, 0, 0, 0, 2, 0, 5)}
        self.assertEqual(srv({1: b, 2: ps}, values, 10), buffer())
        for bindings in ({1: ps}, {1: b, 2: b}, {1: {**b, 'ids': []}}):
            with self.assertRaises(ValueError):
                srv(bindings, values, 10)


if __name__ == '__main__':
    unittest.main()
