"""Decompose native camera mount, calibrated offset, and headset translation."""
import argparse,json,math,struct,sys
from pathlib import Path
from body_heading_ab import Comparison
from live_symbols import resolve

class CentreProbe(Comparison):
    def __init__(self,args):
        super().__init__(args)
        names=['s_anchorRecipe','s_neckMount','s_neutralEyeModel','s_referenceCameraHeight',
               'g_VRBodyBone','g_VRCamModelPos','g_pSharedHands','CyberpunkVR_CamMountCompensate']
        values=resolve(args.pid,args.dll.resolve(),names)
        self.addr.update({n:int(values[n],16) for n in names})
        self.symbols.update(values)
    def capture(self):
        r=super().capture()
        if not r:return r
        r['anchor']=self.read(self.addr['s_anchorRecipe'],'<9f')
        fields=self.types.get('symbol:s_anchorRecipe',{}).get('fields',{})
        if 'centreYaw' in fields:r['centre_reference_yaw']=self.read(self.addr['s_anchorRecipe']+fields['centreYaw']['offset'],'<f')[0]
        if 'bodyOrbit' in fields:r['body_orbit']=bool(self.r.read(self.addr['s_anchorRecipe']+fields['bodyOrbit']['offset'],1)[0])
        r['neck_mount']=self.read(self.addr['s_neckMount'],'<3f')
        r['neutral_eye']=self.read(self.addr['s_neutralEyeModel'],'<3f')
        r['body_bones']=self.read(self.addr['g_VRBodyBone'],'<33f')
        r['cam_model']=self.read(self.addr['g_VRCamModelPos'],'<3f')
        r['mount_compensation']=self.get('CyberpunkVR_CamMountCompensate','<i')
        data=self.read(self.addr['g_lcf'],'<7f3I3f')
        r['native_camera_base']=data[10:13]
        shared=self.read(self.addr['g_pSharedHands'],'<Q')[0]
        if shared:r['shared_calibration']=self.read(shared+85*4,'<39f')
        return r
    def protocol(self):
        self.settings(xr_physical_body_rotation=0,xr_tracked_body_rotation=0,xr_hybrid_body_rotation=1,xr_movement_source=0)
        for c in self.original_hands:self.hand(c)
        self.head_at(0);self.phase_hold('baseline',1.5)
        previous=0
        for angle in (45,90,135,180,90,0,-45,-90,0):
            self.head_sweep(previous,angle,90,f'centre/{angle}/turn')
            self.phase_hold(f'centre/{angle}/hold',.7);self.checkpoint(f'centre/{angle}');previous=angle

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True)
    for name in ('dll','types','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077'))
    p.add_argument('--simulator',type=Path,default=Path.home()/'Downloads/OpenXR-Simulator-v1.5.0-win64/openxr_simulator.dll')
    a=p.parse_args();a.batch_runtime=None;a.case='centre';a.poses='low_ready';a.version='before'
    assert a.out.parent.is_dir() and not a.out.exists()
    CentreProbe(a).run()
