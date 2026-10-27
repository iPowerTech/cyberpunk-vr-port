"""A small reversible simulator-only turn and scene-pixel motion checks."""
import math
import struct


def pose_from_status(status):
    head=status['head_tracking']
    pose={**head['position'],**{key:head[key] for key in ('yaw','pitch','roll')}}
    if set(pose)!={'x','y','z','yaw','pitch','roll'} or not all(math.isfinite(value) for value in pose.values()):
        raise ValueError('Invalid simulator head pose')
    return pose


def same_pose(first,second):
    return all(abs(first[key]-second[key])<.001 for key in ('x','y','z')) and all(
        abs(math.remainder(first[key]-second[key],2*math.pi))<.001 for key in ('yaw','pitch','roll'))


class HeadTurn:
    def __init__(self,directory):
        from simulator_probe import read_status,send,await_pose
        self.directory=directory
        self.read,self.send,self.wait=read_status,send,await_pose
        self.original=pose_from_status(self.read(directory))
        self.current=dict(self.original)
        self.changed=False

    def apply(self,index):
        current=pose_from_status(self.read(self.directory))
        if not same_pose(current,self.current):raise RuntimeError('Simulator pose changed outside this test')
        if (self.directory/'head_pose_command.json').exists():raise RuntimeError('Another head pose command is pending')
        target=dict(self.original)
        if index==1:target['yaw']+=math.radians(10)
        self.current=target
        self.changed=True
        self.send(self.directory,target)
        return self.wait(self.directory,target)['head_tracking']

    def restore(self):
        if not self.changed:return 'unchanged'
        current=pose_from_status(self.read(self.directory))
        if not same_pose(current,self.current):raise RuntimeError('Concurrent simulator pose change; not overwritten')
        if not same_pose(current,self.original):
            self.send(self.directory,self.original)
            self.wait(self.directory,self.original)
        self.current=dict(self.original)
        self.changed=False
        return 'restored'


def scene_pixels(data):
    if len(data)<54 or data[:2]!=b'BM':raise ValueError('Invalid screenshot')
    offset,=struct.unpack_from('<I',data,10)
    width,height=struct.unpack_from('<ii',data,18)
    bpp,=struct.unpack_from('<H',data,28)
    compression,=struct.unpack_from('<I',data,30)
    if width<64 or width%2 or abs(height)<64 or bpp not in (24,32) or compression:
        raise ValueError('Unsupported stereo image layout')
    stride=((width*bpp+31)//32)*4;step=bpp//8;h=abs(height);eye_width=width//2
    if offset+stride*h>len(data):raise ValueError('Truncated stereo image')
    eyes=[bytearray(),bytearray()]
    for eye in range(2):
        for y in range(h*35//100,h*65//100,max(1,h//100)):
            row=h-1-y if height>0 else y
            for x in range(eye_width//5,eye_width*4//5,max(1,eye_width//100)):
                at=offset+row*stride+(eye*eye_width+x)*step
                eyes[eye].extend(data[at:at+3])
    return (width,h),eyes


def verify_motion(first,turned,returned):
    frames=[scene_pixels(data) for data in (first,turned,returned)]
    if len({frame[0] for frame in frames})!=1:raise ValueError('Screenshot dimensions changed during motion test')
    difference=lambda a,b:sum(abs(x-y) for x,y in zip(a,b))/len(a)
    outward=[difference(a,b) for a,b in zip(frames[0][1],frames[1][1])]
    back=[difference(a,b) for a,b in zip(frames[0][1],frames[2][1])]
    if any(a<3 or b>=a*.7 for a,b in zip(outward,back)):
        raise RuntimeError(f'Both eyes must move and return toward the initial scene: turned={outward}, returned={back}')
    return dict(turnedMeanAbsoluteRgb=outward,returnedMeanAbsoluteRgb=back,
                limitation='Scene motion smoke test; not full image equivalence or temporal-artifact validation.')
