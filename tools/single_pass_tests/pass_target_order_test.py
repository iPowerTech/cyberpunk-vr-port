import struct
import unittest
from pass_target_order import analyze,decode,relation,TARGET_BYTES


def fixture():
    events=[];draws=bytearray();targets=bytearray()
    for index,(side,stage,depth) in enumerate(((1,3,102),(0,3,101),(0,4,101),(1,4,102))):
        scope=index+1;command=0x1000+index*0x100;qpc=10+index*10;colors=3 if stage==4 else 0
        row=bytearray(1096);struct.pack_into('<5Q3I',row,0,index+1,qpc,command,0x7000,0x8000,1,0x23a938,side);draws+=row
        target=bytearray(TARGET_BYTES);struct.pack_into('<2Q4I',target,0,index+1,scope,stage,1,1,colors)
        for slot,identity,fmt,flags in ([(i,201+i,24 if i<2 else 28,0) for i in range(colors)]+[(8,depth,20,1 if stage==4 else 0)]):
            struct.pack_into('<3Q6I',target,32+slot*48,identity,identity*4096,128,64,fmt,fmt,2 if slot==8 else 1,flags,0)
        targets+=target
        base=dict(frame=1,side=side,stage=stage,scope=scope,output=command,context=0,input=0,flags0=0,flags1=0,reserved=0)
        events.extend((dict(base,kind=16,context=command,qpc=qpc-1),dict(base,kind=13,qpc=qpc+1),
                       dict(base,kind=14,qpc=qpc+2,flags0=1),dict(base,kind=15,context=command,input=99,qpc=qpc+3,flags1=1,reserved=1)))
    for index,event in enumerate(events):event['sequence']=index+1
    return dict(records=events),bytes(draws),bytes(targets)


class PassTargetOrderTests(unittest.TestCase):
    def test_native_depth_to_color_and_shared_rt_identity(self):
        p,d,t=fixture();r=analyze(p,d,t)
        self.assertEqual((r['depthScopes'],r['gbufferScopes']),(2,2))
        self.assertTrue(all(g['oppositeDepthEarlier'] for g in r['gbufferConsumers']))
        self.assertEqual(set(r['sharedAcrossEyes']),{'201','202','203'})
        self.assertEqual(r['unscopedDraws'],[])

    def test_fourth_motion_target_is_part_of_gbuffer(self):
        p,d,t=fixture();t=bytearray(t)
        for index in (2,3):
            struct.pack_into('<I',t,index*TARGET_BYTES+28,4)
            struct.pack_into('<3Q6I',t,index*TARGET_BYTES+32+3*48,204,204*4096,128,64,34,34,1,0,0)
        r=analyze(p,d,t)
        self.assertEqual(r['gbufferScopes'],2)
        self.assertTrue(all(g['colors']==4 for g in r['gbufferConsumers']))
        self.assertIn('204',r['sharedAcrossEyes'])

    def test_same_batch_or_unserialized_trace_is_not_completion(self):
        p,d,t=fixture();r=analyze(p,d,t);producer,consumer=r['groups'][0],r['groups'][2]
        consumer['submission']['batch']=producer['submission']['batch']
        self.assertEqual(relation(producer,consumer),'same-submit-may-overlap')
        consumer['submission']['batch']+=1;producer['submission']['serialized']=False
        self.assertEqual(relation(producer,consumer),'unserialized-observation')
        producer['submission']['queue']+=1
        self.assertEqual(relation(producer,consumer),'cross-queue-unproved')

    def test_missing_or_changed_targets_are_not_complete_passes(self):
        p,d,t=fixture();bad=bytearray(t);struct.pack_into('<I',bad,2*TARGET_BYTES+24,3)
        r=analyze(p,d,bad);self.assertFalse(r['groups'][2]['complete']);self.assertEqual(r['gbufferScopes'],1)
        p,d,t=fixture();p['records'][10]['flags0']=2;r=analyze(p,d,t)
        self.assertFalse(r['groups'][2]['complete'])

    def test_resource_cookie_cannot_change_description(self):
        p,d,t=fixture();bad=bytearray(t);struct.pack_into('<Q',bad,3*TARGET_BYTES+32+16,129)
        with self.assertRaises(ValueError):analyze(p,d,bad)

    def test_reused_or_partial_list_cannot_get_false_submission(self):
        p,d,t=fixture();p['records'].append(dict(p['records'][11],qpc=35,sequence=99))
        r=analyze(p,d,t);self.assertIsNone(r['groups'][2]['submission'])
        p,d,t=fixture();p['records'][8]['qpc']=31
        with self.assertRaises(ValueError):analyze(p,d,t)

    def test_bad_abi_or_draw_identity_fails(self):
        p,d,t=fixture()
        with self.assertRaises(ValueError):decode(d,t[:-1])
        bad=bytearray(t);struct.pack_into('<Q',bad,0,500)
        with self.assertRaises(ValueError):decode(d,bad)


if __name__=='__main__':unittest.main()
