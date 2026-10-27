"""CPU-only checks for exact alpha material and descriptor addressing."""
import struct
import unittest

from analyze_alpha_depth import alpha_inputs, descriptor, dither_footprint, used_words


class AlphaDepthTests(unittest.TestCase):
    def test_eye_specific_dither_selector_and_material_offsets(self):
        modifiers, global_data, material = bytearray(448), bytearray(476), bytearray(292)
        struct.pack_into('<4f', modifiers, 0, 1, 99, .75, -.25)
        struct.pack_into('<I', global_data, 452, 5945)
        struct.pack_into('<I', material, 0, 32767)
        struct.pack_into('<f', material, 272, .375)
        struct.pack_into('<f', material, 288, 2)
        result = alpha_inputs(modifiers, global_data, material)
        self.assertEqual((result['ditherSlice'], result['alphaTextureIndex'], result['alphaCutoff'], result['uvScale']), (57, 32767, .375, 2))
        struct.pack_into('<I', global_data, 452, 5947)
        self.assertEqual(alpha_inputs(modifiers, global_data)['ditherSlice'], 59)
        self.assertIsNone(alpha_inputs(modifiers, global_data)['alphaTextureIndex'])

    def test_unknown_bytes_and_out_of_range_material_rejected(self):
        with self.assertRaises(ValueError):
            alpha_inputs(bytes(16), bytes(452))
        modifiers, material = bytearray(16), bytearray(292)
        struct.pack_into('<f', modifiers, 0, 1)
        struct.pack_into('<I', material, 0, 32768)
        with self.assertRaises(ValueError):
            alpha_inputs(modifiers, bytes(476), material)
        with self.assertRaises(ValueError):
            used_words(bytes(8), (8,))

    def test_unexpanded_bindless_table_is_not_a_texture(self):
        key = (31, 0, 1, 0, 32768, 0, 5)
        binding = dict(key=key, kind=0, space=1, register=0, count=32768,
                       ids=[], status=1, heap=3, gpu=123, version=99)
        result = descriptor({key: binding}, [(0,) * 20], 0, 5, 321, 1)
        self.assertIsNone(result['value'])
        self.assertEqual(result['capturedSlots'], 0)
        with self.assertRaises(ValueError):
            descriptor({key: binding}, [(0,) * 20], 0, 5, 32768, 1)

    def test_dither_view_relative_mip_and_slice(self):
        value = [2, 13, 50, 0, 11, 5, 5768, 1, 1, 2, 64, 0, 0,
                 3 | (11 << 32), 128, 128 | (66 << 32) | (2 << 48), 0, 1, 1, 0]
        report = dither_footprint(value, 5945)
        self.assertEqual((report['mip'], report['viewSlices'], report['resourceSlices'], report['logicalBytes']), (1, [57], [59], 32768))
        self.assertFalse(report['contentsVerified'])
        self.assertEqual(dither_footprint(value, None)['logicalBytes'], 2097152)
        value[10] = 57
        with self.assertRaises(ValueError):
            dither_footprint(value, 5945)

    def test_pixel_descriptor_does_not_match_vertex_visibility(self):
        key = (13, 0, 1, 0, 32768, 0, 1)
        binding = dict(key=key, kind=0, space=1, register=0, count=32768,
                       ids=[], status=1, heap=3, gpu=123, version=99)
        with self.assertRaises(ValueError):
            descriptor({key: binding}, [(0,) * 20], 0, 5, 0, 1)


if __name__ == '__main__':
    unittest.main()
