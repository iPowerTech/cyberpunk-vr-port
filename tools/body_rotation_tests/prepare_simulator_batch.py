"""Prepare a private test build without editing the user's simulator checkout.

Configure/build the resulting directory normally. Changing the active runtime
manifest is deliberately not part of this script; save/restore it separately.
"""
import argparse,hashlib,json,shutil
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
a=p.parse_args();assert a.source.is_dir() and not a.out.exists()
a.out.mkdir(parents=True)
for name in ('src','include','tests'):shutil.copytree(a.source/name,a.out/name)
shutil.copy2(a.source/'CMakeLists.txt',a.out/'CMakeLists.txt')
shutil.copy2(Path(__file__).with_name('simulator_batch.hpp'),a.out/'src/body_pose_batch.h')
shutil.copy2(Path(__file__).with_name('simulator_batch_tests.cpp'),a.out/'src/body_pose_batch_tests.cpp')
with (a.out/'CMakeLists.txt').open('a',encoding='utf-8') as f:
 f.write('\nadd_executable(tracking_batch_tests src/body_pose_batch_tests.cpp)\nadd_test(NAME tracking_batch COMMAND tracking_batch_tests)\n')
source=a.out/'src/runtime.cpp';text=source.read_text(encoding='utf-8')
def replace(before,after):
 global text
 assert text.count(before)==1,before
 text=text.replace(before,after)
replace('#include "mcp_integration.h"','#include "mcp_integration.h"\n#include "body_pose_batch.h"')
replace('        mcp::ControllerPoseCommand ctrlCmd = mcp::CheckControllerPoseCommand();',r'''
        const auto batchPath=mcp::GetSimulatorDataPath()+"\\tracking_frame_command.json";
        const auto batch=body_test_batch::Read(batchPath);
        if(batch.valid){
            body_test_batch::Frame frame{};
            if(batch.enabled){
                rt::g_headPos={batch.head[0],batch.head[1],batch.head[2]};
                rt::g_headYaw=batch.head[3];rt::g_headPitch=batch.head[4];rt::g_headRoll=batch.head[5];
                frame.head={rt::QuatFromYawPitchRoll(batch.head[3],batch.head[4],batch.head[5]),rt::g_headPos};
                for(int h=0;h<2;++h){
                    auto& c=h==0?rt::g_leftController:rt::g_rightController;
                    c.posOffset={batch.hands[h][0],batch.hands[h][1],batch.hands[h][2]};
                    c.yawOffset=batch.hands[h][3];c.pitchOffset=batch.hands[h][4];
                    rt::GetControllerPose(c,&frame.hands[h]);
                }
            }
            body_test_batch::Publish(batch.enabled,frame);
            DeleteFileA(batchPath.c_str());mcp::WriteCommandAck("tracking_frame",true);
        }
        mcp::ControllerPoseCommand ctrlCmd = mcp::CheckControllerPoseCommand();''')
replace('    // Composition-layer placement reads these same two helpers, which is what keeps',r'''
    body_test_batch::Frame batch{};
    if(li && body_test_batch::At(li->displayTime,batch)){
        const float ipd=rt::g_useCustomIpd?rt::g_customIpd:rt::GetUiIpdMeters();
        for(uint32_t eye=0;eye<2;++eye){
            views[eye].type=XR_TYPE_VIEW;views[eye].pose=batch.head;views[eye].fov=rt::GetViewFov(eye);
            const auto p=rt::RotateVectorByQuaternion(batch.head.orientation,{eye==0?-ipd*.5f:ipd*.5f,0,0});
            views[eye].pose.position.x+=p.x;views[eye].pose.position.y+=p.y;views[eye].pose.position.z+=p.z;
        }
        return XR_SUCCESS;
    }
    // Composition-layer placement reads these same two helpers, which is what keeps''')
replace('    // Check if this is a controller space\n',r'''
    body_test_batch::Frame batch{};
    if(body_test_batch::At(time,batch)){
        const auto hand=rt::g_controllerSpaces.find(space);
        const auto ref=rt::g_referenceSpaces.find(space);
        if(hand!=rt::g_controllerSpaces.end())location->pose=batch.hands[hand->second==1?0:1];
        else if(ref!=rt::g_referenceSpaces.end() && ref->second.type==XR_REFERENCE_SPACE_TYPE_VIEW)location->pose=batch.head;
        else location->pose={{0,0,0,1},{0,0,0}};
        location->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|
            XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
        return XR_SUCCESS;
    }
    // Check if this is a controller space
''')
source.write_text(text,encoding='utf-8')
manifest={'source':str(a.source.resolve()),'test_copy':str(a.out.resolve()),'source_files':{}}
for name in ('src/runtime.cpp','src/ui_enhancements.h','src/mcp_integration.h'):
 manifest['source_files'][name]=hashlib.sha256((a.source/name).read_bytes()).hexdigest()
(a.out/'test-source.json').write_text(json.dumps(manifest,indent=2))
print(a.out)
