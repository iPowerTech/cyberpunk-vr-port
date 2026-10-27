import struct
import unittest
from read_cpu_collector import decode,decode_peer


class CpuCollectorDecodeTests(unittest.TestCase):
    def test_complete_native_lifecycle_required(self):
        row=[100,100,25000,25000,1,1,1,0]
        self.assertTrue(decode(struct.pack('<8I',*row))['destructorCompleted'])
        for index,value in ((1,101),(2,0),(3,24999),(4,0),(5,0),(6,0),(7,1)):
            bad=row.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode(struct.pack('<8I',*bad))

    def test_query_requires_both_groups_and_distinct_exact_frustum(self):
        base=struct.pack('<8I',100,100,82,82,1,1,1,0)
        row=[1,1,0,1,0x12345,0x12345,1,1]
        self.assertTrue(decode(base,struct.pack('<8I',*row))['queryJobsStarted'])
        for index,value in ((0,0),(1,0),(2,1),(3,0),(5,0x99999),(6,0),(7,0)):
            bad=row.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode(base,struct.pack('<8I',*bad))

    def test_fine_ranges_require_exact_ids_and_complete_capture(self):
        base=struct.pack('<8I',100,100,82,82,1,1,1,0)
        query=struct.pack('<8I',1,1,0,1,12345,12345,1,1)
        row=[7,100,30,30,0,0,0,4240,1,1,1,1]
        result=decode(base,query,struct.pack('<12I',*row))
        self.assertTrue(result['fineVisibility']['occlusionActive'])
        self.assertFalse(result['meshListsPrepared'])
        for index,value in ((0,0),(1,0),(3,29),(4,1),(5,1),(6,1),(7,4194305),(8,0),(10,0)):
            bad=row.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode(base,query,struct.pack('<12I',*bad))
        with self.assertRaises(ValueError):
            decode(base,None,struct.pack('<12I',*row))

    def test_complete_scene_pool_requires_matching_visible_ranges(self):
        base=struct.pack('<8I',100,100,82,82,1,1,1,0)
        query=struct.pack('<8I',1,1,0,1,12345,12345,1,1)
        fine=struct.pack('<12I',7,100,30,30,0,0,0,4240,1,1,1,1)
        row=[1,0,22,22,250,7,30,30,0,1,10704,1]
        control=struct.pack('<8I',7,0,0,7,30,0,0,1)
        self.assertEqual(decode(base,query,fine,struct.pack('<12I',*row),control)['scenePool']['buckets'],22)
        for index,value in ((0,0),(1,1),(2,0),(3,21),(4,0),(5,6),(6,29),(8,1),(9,0),(11,0)):
            bad=row.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode(base,query,fine,struct.pack('<12I',*bad),control)
        other=struct.pack('<12I',8,110,35,35,0,0,0,4680,1,1,1,1)
        provenance=[7,1,5,7,30,0,0,1]
        self.assertEqual(decode(base,query,other,struct.pack('<12I',*row),struct.pack('<8I',*provenance))['scenePool']['outsidePoolIds'],5)
        for index,value in ((0,6),(1,0),(2,0),(3,6),(4,29),(5,1),(6,1),(7,0)):
            bad=provenance.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode(base,query,other,struct.pack('<12I',*row),struct.pack('<8I',*bad))

    def test_peer_requires_early_same_frame_exact_visibility(self):
        base=struct.pack('<8I',100,100,82,82,1,1,1,0)
        query=struct.pack('<8I',1,1,0,1,12345,12345,1,1)
        fine=struct.pack('<12I',7,100,30,30,0,0,0,4240,1,1,1,1)
        scene=struct.pack('<12I',1,0,22,22,250,7,30,30,0,1,10704,1)
        control=struct.pack('<8I',7,0,0,7,30,0,0,1)
        row=[1,1,1,1,1,1,28,30,0,0,1,1,1,0,27,4]
        self.assertEqual(decode_peer(base,struct.pack('<16I',*row),query,fine,scene,control)['earlyMainIds'],30)
        for index,value in [(i,0) for i in (0,1,2,3,4,5,6,7,10,11,12)]+[(8,1),(9,1),(13,1),(14,30),(15,0)]:
            bad=row.copy();bad[index]=value
            with self.assertRaises(ValueError):
                decode_peer(base,struct.pack('<16I',*bad),query,fine,scene,control)


if __name__=='__main__':
    unittest.main()
