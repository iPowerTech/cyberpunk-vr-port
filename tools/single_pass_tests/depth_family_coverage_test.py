import copy
import struct
import unittest
from depth_family_coverage import analyze


def fixture():
    camera=dict(type=2,visibility=1,space=0,count=1,register=1,bytes=848)
    material=dict(camera,register=5,bytes=112)
    family=dict(nativeVs='vs',nativePs='',nativePsBytes=0,uses=[camera,material])
    manifest=dict(families=[family,dict(family,nativePs='ps',nativePsBytes=100)])
    registry=dict(records=[dict(original='0x40',asset=1),dict(original='0x50',asset=2)])
    start=dict(kind=13,scope=1,frame=1,side=0,output=12,qpc=100)
    preparation=dict(records=[start,dict(start,kind=14,qpc=200,flags0=2)],earlyDepthGpu=dict(sources=[[1,8]]))
    raw=bytearray(1096*3)
    for index,(qpc,pipeline) in enumerate(((90,64),(150,80),(210,64))):
        struct.pack_into('<5Q5I',raw,index*1096,index+1,qpc,12,pipeline,4,1,0x23A938,0,30,2)
    return preparation,registry,manifest,raw


class DepthFamilyCoverageTests(unittest.TestCase):
    def test_one_based_assets_and_exact_scope(self):
        result=analyze(*fixture())
        self.assertEqual(result['selectedMain'],1)
        self.assertEqual([g['kind'] for g in result['groups']],['camera-material-cbv','pixel-shader'])
        self.assertEqual([g['draws'] for g in result['groups']],[1,1])
        self.assertEqual(result['groups'][0]['indexInvocations'],60)

    def test_missing_shader_and_invalid_asset_are_distinct(self):
        p,r,m,b=fixture();r['records'].pop()
        self.assertIn('unregistered',[g['kind'] for g in analyze(p,r,m,b)['groups']])
        for asset in (0,3):
            bad=copy.deepcopy(r);bad['records'][0]['asset']=asset
            with self.assertRaises(ValueError):analyze(p,bad,m,b)

    def test_incomplete_or_wrong_capture_is_rejected(self):
        p,r,m,b=fixture()
        with self.assertRaises(ValueError):analyze(p,r,m,b[:-1])
        p['earlyDepthGpu']['sources']=[[3,8]]
        with self.assertRaises(ValueError):analyze(p,r,m,b)
        p,r,m,b=fixture();p['records'].pop()
        with self.assertRaises(ValueError):analyze(p,r,m,b)

    def test_native_end_count_and_uncaptured_scope(self):
        p,r,m,b=fixture();p['records'][1]['flags0']=3
        with self.assertRaises(ValueError):analyze(p,r,m,b)
        p,r,m,b=fixture();start=dict(p['records'][0],scope=2,frame=2,qpc=300)
        p['records'].extend([start,dict(start,kind=14,qpc=400,flags0=20)])
        result=analyze(p,r,m,b)
        self.assertEqual(result['uncapturedScopes'],[dict(frame=2,side=0,scope=2)])


if __name__=='__main__':unittest.main()
