import struct
import unittest
from verify_mesh_plan import geometry_signature


class GeometryCorrelationTests(unittest.TestCase):
    def record(self):
        data = bytearray(1096)
        struct.pack_into('<I',data,84,191)
        struct.pack_into('<I',data,72,0xffff)
        struct.pack_into('<I',data,1092,(1<<0)|(1<<2)|(1<<7))
        return data

    def test_missing_bindings_cannot_be_called_a_match(self):
        data=self.record()
        for flags in (0,57,63,128,255):
            struct.pack_into('<I',data,84,flags)
            self.assertIsNone(geometry_signature(data))
        struct.pack_into('<I',data,84,191)
        struct.pack_into('<I',data,72,1)
        self.assertIsNone(geometry_signature(data))

    def test_unused_vertex_stream_not_part_of_contract(self):
        a=self.record();b=self.record();b[112+4*16]=55
        self.assertEqual(geometry_signature(bytes(a)),geometry_signature(bytes(b)))
        b[112+2*16]=55
        self.assertNotEqual(geometry_signature(bytes(a)),geometry_signature(bytes(b)))

    def test_index_pipeline_and_draw_arguments_remain_part_of_contract(self):
        a=self.record()
        for offset in (24,32,52,60,64,96):
            b=self.record();b[offset]=77
            self.assertNotEqual(geometry_signature(bytes(a)),geometry_signature(bytes(b)))

    def test_instance_batch_mapping_is_separate(self):
        a=self.record();b=self.record();b[56]=3;b[68]=8;b[112+7*16]=88
        self.assertEqual(geometry_signature(bytes(a)),geometry_signature(bytes(b)))
        with self.assertRaises(ValueError):geometry_signature(bytes(a[:-1]))


if __name__=='__main__':unittest.main()
