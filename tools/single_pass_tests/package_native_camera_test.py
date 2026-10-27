import copy
import unittest

from package_native_camera import validate_stage,resource_contract


def fixture(stage='vs',camera=True):
    info=dict(stage=stage,cameraBindings=int(camera),resources=[dict(kind='srv',space=0,register=77,count=1)])
    if camera:
        info['camera']=dict(register=1,space=0,bytes=848)
        info['resources'].append(dict(kind='cbuffer',space=0,register=1,count=1,bytes=848))
    visibility=1 if stage=='vs' else 5
    uses=[dict(type=t,space=s,register=r,count=c,bytes=b,visibility=visibility) for t,s,r,c,b in resource_contract(info)]
    return info,uses


class NativeCameraPackageTests(unittest.TestCase):
    def test_both_stages_preserve_all_original_bindings(self):
        for stage in ('vs','ps'):
            info,uses=fixture(stage)
            self.assertTrue(validate_stage(info,stage,uses))
            unrelated=dict(type=3,visibility=5 if stage=='vs' else 1,space=0,register=0,count=1,bytes=0)
            self.assertTrue(validate_stage(info,stage,uses+[unrelated]))

    def test_camera_free_stage_is_not_claimed_eye_independent(self):
        for stage in ('vs','ps'):
            info,uses=fixture(stage,False)
            self.assertFalse(validate_stage(info,stage,uses))

    def test_resource_remapping_or_missing_srv_is_rejected(self):
        info,uses=fixture()
        for key,value in (('type',1),('space',100),('register',78),('count',2),('bytes',16)):
            changed=copy.deepcopy(uses);changed[0][key]=value
            with self.assertRaises(ValueError):validate_stage(info,'vs',changed)
        with self.assertRaises(ValueError):validate_stage(info,'vs',uses[1:])

    def test_wrong_stage_or_camera_layout_is_rejected(self):
        info,uses=fixture()
        with self.assertRaises(ValueError):validate_stage(info,'ps',uses)
        for key,value in (('register',2),('space',1),('bytes',832)):
            changed=copy.deepcopy(info);changed['camera'][key]=value
            with self.assertRaises(ValueError):validate_stage(changed,'vs',uses)
        info['cameraBindings']=2
        with self.assertRaises(ValueError):validate_stage(info,'vs',uses)


if __name__=='__main__':unittest.main()
