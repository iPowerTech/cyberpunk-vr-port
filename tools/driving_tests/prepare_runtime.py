"""Private simulator copy with atomic poses and bounded grip input transport.

Does not alter the user's simulator checkout or activate the resulting runtime.
"""
import argparse
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'body_rotation_tests/prepare_simulator_batch.py'),
                '--source',str(a.source),'--out',str(a.out)],check=True)

def edit(path,replacements):
    text=path.read_text(encoding='utf-8')
    for old,new in replacements:
        assert text.count(old)==1,(path,old)
        text=text.replace(old,new)
    path.write_text(text,encoding='utf-8')

edit(a.out/'src/body_pose_batch.h',[
 ('struct Frame { XrPosef head{},hands[2]{}; };','struct Frame { XrPosef head{},hands[2]{};float grip[2]{}; };'),
 ('float head[6]{},hands[2][5]{};','float head[6]{},hands[2][5]{},grip[2]{};'),
 ('inline bool At(XrTime time,Frame& f){','inline bool Current(Frame& f){std::lock_guard lock(mutex);if(!enabled)return false;f=current;return true;}\ninline bool At(XrTime time,Frame& f){'),
 ('    cmd.valid=true;return cmd;','    for(int h=0;h<2;++h){cmd.grip[h]=float(obj.number(h==0?"lgrip":"rgrip",0.0));if(!std::isfinite(cmd.grip[h])||cmd.grip[h]<0||cmd.grip[h]>1)return {};}\n    cmd.valid=true;return cmd;')])
edit(a.out/'src/runtime.cpp',[
 ('                    rt::GetControllerPose(c,&frame.hands[h]);','                    rt::GetControllerPose(c,&frame.hands[h]);frame.grip[h]=batch.grip[h];'),
 ('            body_test_batch::Publish(batch.enabled,frame);','            if(!batch.enabled){rt::g_leftController.gripValue=rt::g_rightController.gripValue=0;rt::g_leftController.gripPressed=rt::g_rightController.gripPressed=false;}\n            body_test_batch::Publish(batch.enabled,frame);'),
 ('    // Pace on a high-resolution waitable timer.',
  '    body_test_batch::Frame gripFrame{};\n    if(body_test_batch::Current(gripFrame)){\n        for(int h=0;h<2;++h){auto& c=h==0?rt::g_leftController:rt::g_rightController;c.gripValue=gripFrame.grip[h];c.gripPressed=gripFrame.grip[h]>=.5f;}\n    }\n    // Pace on a high-resolution waitable timer.')])
edit(a.out/'src/body_pose_batch_tests.cpp',[
 ('"enabled":true,"x":0.003','"enabled":true,"lgrip":0.25,"rgrip":1.0,"x":0.003'),
 ('    Frame f{};Check(!At(1,f));','    Check(cmd.grip[0]==.25f&&cmd.grip[1]==1.f);\n    Frame f{};Check(!Current(f));Check(!At(1,f));'),
 ('Publish(true,Make(1));Check(At(1,f)', 'Publish(true,Make(1));Check(Current(f)&&f.head.position.x==1);Check(At(1,f)')])
print('Private driving transport prepared:',a.out)
