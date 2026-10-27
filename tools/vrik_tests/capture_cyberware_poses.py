"""Capture native-reference/VR palm views through simulator IPC, without focus.

The simulator used for this test must support the controller `roll` field.
Equipment and VRIK enable/disable are owned by the caller. No game memory writes.
"""
import argparse,json,math,os,sys,time,struct
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/body_rotation_tests'),str(ROOT/'tools/roomscale_tests'),str(ROOT/'tools/swimming_tests')]
from live_matrix import atomic,mul
from live_matrix import rotate
from live_symbols import resolve
from live_read import Reader
from live_types import describe

def inverse(q):return [-q[0],-q[1],-q[2],q[3]]
def unit(v):
    n=math.sqrt(sum(x*x for x in v));assert n>1e-8
    return [x/n for x in v]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def basis(forward,normal):
    y=unit(forward);x=unit(cross(y,normal));z=cross(x,y)
    m=[[x[i],y[i],z[i]] for i in range(3)];t=sum(m[i][i] for i in range(3))
    if t>0:
        s=math.sqrt(t+1)*2;q=[(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s/4]
    else:
        i=max(range(3),key=lambda i:m[i][i]);j=(i+1)%3;k=(i+2)%3
        s=math.sqrt(1+m[i][i]-m[j][j]-m[k][k])*2;q=[0.,0.,0.,0.]
        q[i]=s/4;q[j]=(m[j][i]+m[i][j])/s;q[k]=(m[k][i]+m[i][k])/s;q[3]=(m[k][j]-m[j][k])/s
    return unit(q)

class HandFrames:
    def __init__(self,pid,dll):
        names=['g_VRBind','g_VRHandRawRot','s_referenceModelPos','s_referenceModelRot','s_playerBinding','g_VRFKSnapPos','g_VRFKSnapRot','g_VRFKSnapCount','g_VRBoneCount']
        for side,suffix in [('L','L'),('R','')]:
            names+=['g_VR'+('Left' if side=='L' else 'Right')+'BoneIdx','g_VRSmokeFingerName'+suffix,'g_VRSmokeFingerIdx'+suffix]
            names+=['g_VRWrist'+side+'_'+axis for axis in ('I','J','K','R')]
        result=resolve(pid,dll.resolve(),names);self.address={n:int(result[n],16) for n in names}
        self.reader=Reader(pid,Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\Cyberpunk2077.exe'),0,0)
        self.sha=result['sha256'];self.local=[];self.wrist=[]
        layout=describe(dll.resolve(),['symbol:s_playerBinding'])['symbol:s_playerBinding']['fields']
        self.names_pointer=struct.unpack('<Q',self.reader.read(self.address['s_playerBinding']+layout['names']['offset'],8))[0]
        count=self.get('g_VRBoneCount','<i')[0]
        hashes=struct.unpack('<'+str(count)+'Q',self.reader.read(self.names_pointer,count*8))
        def hash_name(value):
            h=14695981039346656037
            for b in value.encode():h=((h^b)*1099511628211)&((1<<64)-1)
            return h
        wanted=['LeftHand','RightHand','LeftHand_setup','RightHand_setup','l_wrist01_GRP','r_wrist01_GRP','launcher_base']
        for side in ('Left','Right'):
            wanted += [side+'ForeArm',side+'InHandIndex']
            wanted += [side+'Hand'+digit+'1'+suffix for digit in ('Thumb','Index','Middle','Pinky') for suffix in ('','_setup')]
        for side in ('l','r'):
            wanted += [side+'_forearmTwist0'+str(i)+'_JNT' for i in (1,2,3)]
            wanted += [side+'_Wrist_'+str(i)+'_JNT' for i in (0,1,2)]
            wanted += [side+'_nanoWire01_grp',side+'_nanoWire01_jnt',side+'_nanoWire40_jnt']
            wanted += [side+'_innerWrist01_JNT',side+'_handIndex01_JNT',side+'_handMiddle01_JNT',side+'_blade01_JNT',side+'_blade02_JNT',side+'_blade05_JNT']
        wanted += ['left_blade_eff','right_blade_eff']
        self.markers={name:hashes.index(hash_name(name)) for name in wanted if hash_name(name) in hashes}
        for side,suffix in [('L','L'),('R','')]:
            prefix='Left' if side=='L' else 'Right';hand=self.get('g_VR'+prefix+'BoneIdx','<i')[0]
            raw=self.reader.read(self.address['g_VRSmokeFingerName'+suffix],32*48)
            names=[raw[i*48:(i+1)*48].split(b'\0')[0].decode() for i in range(32)]
            indices=self.get('g_VRSmokeFingerIdx'+suffix,'<32i');lookup=dict(zip(names,indices))
            p=self.vector('s_referenceModelPos',hand,3);q=self.vector('s_referenceModelRot',hand,4)
            vectors={}
            for digit in ('Middle','Index','Pinky'):
                point=self.vector('s_referenceModelPos',lookup[prefix+'Hand'+digit+'1'],3)
                vectors[digit]=rotate(inverse(q),[a-b for a,b in zip(point,p)])
            normal=cross(vectors['Index'],vectors['Pinky'])
            if side=='L':normal=[-x for x in normal]
            self.local.append(basis(vectors['Middle'],normal))
            self.wrist.append([self.get('g_VRWrist'+side+'_'+axis,'<f')[0] for axis in ('I','J','K','R')])
    def get(self,name,fmt):return struct.unpack(fmt,self.reader.read(self.address[name],struct.calcsize(fmt)))
    def vector(self,name,i,n):return struct.unpack('<'+str(n)+'f',self.reader.read(self.address[name]+i*n*4,n*4))
    def rotations(self):
        v=self.get('g_VRHandRawRot','<8f');return [v[:4],v[4:]]
    def rendered(self):
        count=self.get('g_VRFKSnapCount','<i')[0]
        return {name:{'index':i,'position':self.vector('g_VRFKSnapPos',i,3),'rotation':self.vector('g_VRFKSnapRot',i,4)}
                for name,i in self.markers.items() if 0<=i<count}
    def calibrate(self):self.frame=[mul(q,inverse(w)) for q,w in zip(self.rotations(),self.wrist)]
    def target(self,hand,normal):
        desired=mul(basis([0,1,0],normal),inverse(self.local[hand]))
        return self.controller(hand,desired),desired
    def controller(self,hand,desired):
        mapped=mul(mul(inverse(self.frame[hand]),desired),inverse(self.wrist[hand]))
        return [mapped[0],mapped[2],-mapped[1],mapped[3]]

def euler(q):
    x,y,z,w=q;p=math.asin(max(-1,min(1,2*(w*x-y*z))))
    if abs(math.cos(p))<1e-6:
        return math.atan2(2*(w*y-x*z),1-2*(y*y+z*z)),p,0
    return math.atan2(2*(x*z+w*y),1-2*(x*x+y*y)),p,math.atan2(2*(x*y+w*z),1-2*(x*x+z*z))

def send(directory,filename,value):
    path=directory/filename
    if path.exists():raise RuntimeError('Another simulator command is pending')
    atomic(path,value);end=time.monotonic()+5
    while path.exists() and time.monotonic()<end:time.sleep(.01)
    if path.exists():raise RuntimeError('Simulator command was not consumed')

def capture(directory,path):
    requested=time.time();send(directory,'screenshot_request.json',{'eye':'both','layer':'all'})
    end=time.monotonic()+8
    while time.monotonic()<end:
        status=directory/'screenshot_status.json'
        if status.exists() and status.stat().st_mtime>=requested:
            image=Image.open(directory/'screenshot.bmp');image.save(path)
            return json.loads(status.read_text())
        time.sleep(.1)
    raise RuntimeError('No new rendered screenshot')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--reference',action='store_true')
    p.add_argument('--pid',type=int,required=True);p.add_argument('--dll',type=Path,required=True)
    p.add_argument('--wrist-flex',action='store_true',help='Test wrist flexion, deviation and roll separately')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    directory=Path(os.environ['LOCALAPPDATA'])/'OpenXR-Simulator'
    frames=HandFrames(a.pid,a.dll)
    assert frames.get('g_VRBind','<i')[0]==(0 if a.reference else 4),'VRIK state does not match requested capture'
    # Native idle arms sit lower than the tracked test targets. Look down far
    # enough to include the complete native hands instead of cropped fingertips.
    head={'x':0,'y':1.7,'z':0,'yaw':0,'pitch':-1.0 if a.reference else -.5,'roll':0}
    send(directory,'head_pose_command.json',head)
    poses={'01-palm-up':(0,0,1),'02-palm-down':(0,0,-1),
           '03-palm-left':(-1,0,0),'04-palm-right':(1,0,0)}
    if a.reference:poses={'00-native-reference':(0,0,1)}
    elif a.wrist_flex:
        poses={'10-neutral':(0,0),'11-flex-up':(0,-45),'12-flex-down':(0,45),
               '13-deviate-left':(2,35),'14-deviate-right':(2,-35),'15-roll':(1,100)}
    for hand in (0,1):
        send(directory,'controller_pose_command.json',dict(hand=hand,posX=(-.2 if hand==0 else .2),posY=-.17,posZ=-.3,yaw=0,pitch=-head['pitch'],roll=0,trigger=0))
    time.sleep(.5);frames.calibrate()
    for label,normal in poses.items():
        controllers=[]
        desired=[]
        for hand in (0,1):
            if a.wrist_flex and not a.reference:
                _,base=frames.target(hand,(0,0,-1));axis,degrees=normal
                half=math.radians(degrees)*.5;turn=[0,0,0,math.cos(half)];turn[axis]=math.sin(half)
                handq=mul(turn,base);q=frames.controller(hand,handq)
            else:q,handq=frames.target(hand,normal)
            desired.append(handq)
            yaw,pitch,roll=euler(q)
            value=dict(hand=hand,posX=(-.2 if hand==0 else .2),posY=-.17,posZ=-.3,
                       yaw=yaw,pitch=pitch-head['pitch'],roll=roll,trigger=0)
            controllers.append(value)
        # A human wrist does not teleport through 180 degrees in one tracking
        # frame. Sweep the simulator to the requested rotations, retaining the
        # same strict measured final-orientation check below.
        start=[frames.controller(hand,q) for hand,q in enumerate(frames.rotations())]
        for step in range(1,9):
            t=step/8
            for hand in (0,1):
                endq=frames.controller(hand,desired[hand]);begin=start[hand]
                if sum(x*y for x,y in zip(begin,endq))<0:endq=[-v for v in endq]
                q=unit([(1-t)*x+t*y for x,y in zip(begin,endq)])
                yaw,pitch,roll=euler(q)
                value=dict(controllers[hand],yaw=yaw,pitch=pitch-head['pitch'],roll=roll)
                send(directory,'controller_pose_command.json',value)
        time.sleep(.8)
        end=time.monotonic()+2
        while True:
            measured=frames.rotations()
            errors=[2*math.degrees(math.acos(min(1,abs(sum(x*y for x,y in zip(unit(xq),unit(yq))))))) for xq,yq in zip(desired,measured)]
            if a.reference or max(errors)<2 or time.monotonic()>=end:break
            time.sleep(.05)
        if not a.reference:assert max(errors)<2,('Controller orientation did not reach both hands',errors)
        output=a.out/(label+'.png')
        if output.exists():raise RuntimeError('Refusing to replace a capture: '+str(output))
        status=capture(directory,output)
        output.with_suffix('.json').write_text(json.dumps({'label':label,'vrik':not a.reference,
            'head':head,'controllers':controllers,'capture':status,'module_sha256':frames.sha,
            'desired_hand_rotations':desired,'measured_hand_rotations':measured,'orientation_error_degrees':errors,
            'rendered_bones':None if a.reference else frames.rendered()},indent=2))
        print(output,flush=True)
    if not a.reference:
        overview=Image.new('RGB',(1920,768*((len(poses)+1)//2)),(26,26,26));draw=ImageDraw.Draw(overview)
        for i,label in enumerate(poses):
            view=Image.open(a.out/(label+'.png'));view=view.crop((0,0,view.width//2,view.height))
            view.thumbnail((960,718));x=(i%2)*960;y=(i//2)*768
            overview.paste(view,(x,y+32));draw.text((x+12,y+10),label,fill='white')
        overview.save(a.out/'overview.png')
    frames.reader.close()

if __name__=='__main__':main()
