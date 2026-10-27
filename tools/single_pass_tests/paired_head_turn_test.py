import math
import struct
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch,Mock
from paired_head_turn import HeadTurn,pose_from_status,same_pose,verify_motion


def bitmap(left,right):
    width,height=128,64
    data=bytearray(54+width*height*4);data[:2]=b'BM'
    struct.pack_into('<I',data,10,54);struct.pack_into('<IiiHHI',data,14,40,width,height,1,32,0)
    for y in range(height):
        for x in range(width):
            at=54+(y*width+x)*4;data[at:at+3]=bytes([left if x<width//2 else right])*3
    return data


class HeadTurnTests(unittest.TestCase):
    def test_both_eyes_must_change_and_return(self):
        report=verify_motion(bitmap(10,20),bitmap(40,50),bitmap(12,22))
        self.assertEqual(report['turnedMeanAbsoluteRgb'],[30,30])
        for images in ((bitmap(10,20),bitmap(10,50),bitmap(10,20)),
                       (bitmap(10,20),bitmap(40,50),bitmap(40,50))):
            with self.assertRaises(RuntimeError):verify_motion(*images)

    def test_invalid_image_is_rejected(self):
        with self.assertRaises(ValueError):verify_motion(b'',bitmap(1,2),bitmap(1,2))
        with self.assertRaises(ValueError):verify_motion(bitmap(1,2)[:-1],bitmap(1,2),bitmap(1,2))

    def test_finite_pose_and_wrapped_angles(self):
        pose=pose_from_status(dict(head_tracking=dict(position=dict(x=0,y=1.7,z=0),yaw=0,pitch=0,roll=0)))
        self.assertTrue(same_pose(pose,dict(pose,yaw=math.tau)))
        self.assertFalse(same_pose(pose,dict(pose,yaw=.1)))
        with self.assertRaises(ValueError):pose_from_status(dict(head_tracking=dict(position=dict(x=math.nan,y=1.7,z=0),yaw=0,pitch=0,roll=0)))

    def test_pose_is_restored_and_concurrent_changes_are_not_overwritten(self):
        current=dict(x=0,y=1.7,z=0,yaw=0,pitch=0,roll=0)
        def status(*_):return dict(head_tracking=dict(position={k:current[k] for k in ('x','y','z')},**{k:current[k] for k in ('yaw','pitch','roll')}))
        send=Mock(side_effect=lambda directory,pose:current.update(pose))
        api=SimpleNamespace(read_status=status,send=send,await_pose=status)
        with patch.dict('sys.modules',simulator_probe=api),patch.object(Path,'exists',return_value=False):
            turn=HeadTurn(Path('C:/test-simulator'))
            turn.apply(1);self.assertGreater(current['yaw'],.17)
            self.assertEqual(turn.restore(),'restored');self.assertEqual(current['yaw'],0)
            turn.apply(1);current['yaw']=.4
            count=send.call_count
            with self.assertRaisesRegex(RuntimeError,'Concurrent'):turn.restore()
            self.assertEqual(send.call_count,count)


if __name__=='__main__':unittest.main()
