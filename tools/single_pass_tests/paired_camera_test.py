import struct
import unittest
from paired_camera import decode_cameras,compare_cameras,STRIDE


class PairedCameraTests(unittest.TestCase):
    def fixture(self):
        rows=[]
        for side in (0,1):
            data=bytearray(STRIDE);view=0x10000+side*0x3000
            struct.pack_into('<3Q4IiI',data,0,side+1,10+side*20,0,1,0x23A938,1,0,side,1)
            struct.pack_into('<3Q4I',data,896,1,view,view+0x70,1485,1485,1,11)
            struct.pack_into('<2I',data,1856,1,0);struct.pack_into('<2I',data,2768,1,1)
            masks=[0]*4
            for i in range(212):
                if i!=147:masks[i//64]|=1<<(i%64)
            struct.pack_into('<4Q',data,4528,*masks);struct.pack_into('<I',data,4612,1)
            struct.pack_into('<I',data,2776+147*4,0x7FC00000)
            rows.append(data)
        prep=[dict(frame=1,side=s,kind=13,stage=3,qpc=20+s*20,thread=1,context=1) for s in (0,1)]
        return rows,prep,[0x10000,0x13000]

    def test_exact_camera_and_defined_words(self):
        rows,prep,views=self.fixture()
        report=compare_cameras(decode_cameras(b''.join(rows)),prep,1,views)
        self.assertTrue(report['passed'])
        self.assertEqual(report['unavailableShaderWords'],[147])

    def test_prepared_history_must_also_match_temporal_weight(self):
        rows,prep,views=self.fixture();struct.pack_into('<I',rows[0],932,12)
        masks=[(1<<64)-1]*3+[(1<<20)-1];struct.pack_into('<4Q',rows[0],4528,*masks)
        struct.pack_into('<f',rows[0],2776+588,.02);struct.pack_into('<f',rows[1],48+588,.02)
        result=compare_cameras(decode_cameras(b''.join(rows)),prep,1,views)
        self.assertTrue(result['passed']);self.assertEqual(result['unavailableShaderWords'],[])
        struct.pack_into('<f',rows[0],2776+588,.03)
        self.assertFalse(compare_cameras(decode_cameras(b''.join(rows)),prep,1,views)['passed'])

    def test_wrong_peer_order_missing_pass_and_shader_difference_fail(self):
        for offset,value in ((4612,0),(2776,1),(4528,0),(1856,0),(2768,0)):
            rows,prep,views=self.fixture();struct.pack_into('<I',rows[0],offset,value)
            self.assertFalse(compare_cameras(decode_cameras(b''.join(rows)),prep,1,views)['passed'])
        rows,prep,views=self.fixture();decoded=decode_cameras(b''.join(rows))
        self.assertFalse(compare_cameras(decoded,prep[:1],1,views)['passed'])
        decoded[1]['qpc']=15
        self.assertFalse(compare_cameras(decoded,prep,1,views)['passed'])

    def test_does_not_choose_an_earlier_matching_camera(self):
        rows,prep,views=self.fixture();decoded=decode_cameras(b''.join(rows))
        wrong=dict(decoded[1],qpc=35,camera=bytes([1])+bytes(847))
        self.assertFalse(compare_cameras(decoded+[wrong],prep,1,views)['passed'])

    def test_other_recording_thread_cannot_substitute_its_camera(self):
        rows,prep,views=self.fixture();decoded=decode_cameras(b''.join(rows))
        decoy=dict(decoded[1],qpc=35,thread=2,camera=bytes([1])+bytes(847))
        self.assertTrue(compare_cameras(decoded+[decoy],prep,1,views)['passed'])
        decoded[1]['thread']=2
        self.assertFalse(compare_cameras(decoded,prep,1,views)['passed'])
        decoded[1]['thread']=1;decoded[1]['context']=2
        self.assertFalse(compare_cameras(decoded,prep,1,views)['passed'])

    def test_truncated_and_invalid_identity_rejected(self):
        rows,_,_=self.fixture()
        with self.assertRaises(ValueError):decode_cameras(rows[0][:-1])
        struct.pack_into('<Q',rows[0],0,3)
        with self.assertRaises(ValueError):decode_cameras(rows[0])

    def test_stationary_frames_cannot_certify_motion_history(self):
        rows,prep,views=self.fixture();decoded=decode_cameras(b''.join(rows))
        self.assertFalse(compare_cameras(decoded,prep,1,views,require_motion=True)['passed'])
        decoded[0]['previous']=bytes([1])+decoded[0]['previous'][1:]
        report=compare_cameras(decoded,prep,1,views,require_motion=True)
        self.assertTrue(report['passed']);self.assertEqual(report['movingFrames'],[1])


if __name__=='__main__':unittest.main()
