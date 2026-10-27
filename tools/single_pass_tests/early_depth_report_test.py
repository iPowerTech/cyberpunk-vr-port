import unittest
from early_depth_report import validate_camera_stages
import struct
import argparse
from early_depth_report import FIELDS,NATIVE_FIELDS,OPTIMIZED_FIELDS,validate,decode_gpu_times,validate_native_timing
from early_depth_report import ALL_DRAWS,MAX_SOURCES,draw_limit
from read_constants import decode_depth_comparison


class EarlyDepthReportTests(unittest.TestCase):
    def test_camera_stages_require_complete_binding_and_cleanup(self):
        validate_camera_stages(dict(enabled=True,stages=3,stageMasks=[3,3,3,0]),1)
        validate_camera_stages(dict(enabled=True,stages=3,stageMasks=[3,3,1,2]),1)
        for masks in ([3,1,3,0],[3,3,1,0],[3,3,3,1],[3,3,-1,4],[]):
            with self.assertRaises(RuntimeError):validate_camera_stages(dict(enabled=True,stages=3,stageMasks=masks),1)
    def good(self):
        stats=dict.fromkeys(FIELDS,0);stats.update(recorded=32,matched=32,mainEnded=1,peerEnded=1)
        return dict(state=3,result=0,stats=stats,comparison=dict(equal=True,draws=32),coverage=[[4,4]]*32,masks=[[4,0]]*32,extraCoverage=[[0,0]]*32,
                    descriptorFailure=0,descriptorStoreStats=[0,2,20,2,20,0],gpuInstances=[0,0,0,0],
                    maskStats=dict(mainPackets=100,peerPackets=100,mainInstances=200,peerInstances=200,both=128,mainOnly=0,pairedRuns=32,originFailure=0,
                                   peerOnly=0,peerOnlyRuns=0,knownPeerOnly=0,unrecordedPeerOnly=0))

    def test_complete_native_comparison(self):validate(self.good(),32)

    def test_native_camera_needs_exact_upload_bytes_and_draw_coverage(self):
        r=self.good();r['nativeCamera']=dict(enabled=True,stats=[1,32,1],stages=1,stageMasks=[1,1,1,0]);validate(r,32)
        for values in ([0,32,1],[1,31,1],[1,32,0]):
            r['nativeCamera']['stats']=values
            with self.assertRaises(RuntimeError):validate(r,32)

    def test_all_draws_keeps_exact_matching_and_rejects_capacity_exhaustion(self):
        validate(self.good(),ALL_DRAWS)
        for count in (0,MAX_SOURCES,MAX_SOURCES+1):
            r=self.good();r['stats'].update(recorded=count,matched=count)
            with self.assertRaises(RuntimeError):validate(r,ALL_DRAWS)
        r=self.good();r['stats']['matched']-=1
        with self.assertRaises(RuntimeError):validate(r,ALL_DRAWS)

    def test_gpu_identity_is_not_itself_an_admission_proof(self):
        r=self.good();r['stats']['peerSeen']=32;r['gpuInstances']=[16,16,0,0]
        validate(r,32)
        for bad in ([16,16,1,48],[33,16,0,0],[16,33,0,0],[]):
            r['gpuInstances']=bad
            with self.assertRaises(RuntimeError):validate(r,32)

    def test_draw_limit_cli(self):
        self.assertEqual(draw_limit('all'),ALL_DRAWS)
        for count in (1,128,203,MAX_SOURCES,ALL_DRAWS):self.assertEqual(draw_limit(str(count)),count)
        for value in ('',None,'0','-1',str(MAX_SOURCES+1),'2.5'):
            with self.assertRaises(argparse.ArgumentTypeError):draw_limit(value)

    def test_tracking_failure_rejects_even_equal_images(self):
        for key,value in (('descriptorFailure',4),('descriptorStoreStats',[8,2,20,2,20,0]),('descriptorStoreStats',[])):
            r=self.good();r[key]=value
            with self.assertRaises(RuntimeError):validate(r,32)

    def test_timestamp_frequencies_are_per_queue(self):
        r=decode_gpu_times([10,110,100000,4000,5000,2000000])
        self.assertEqual((r['mainMs'],r['peerMs'],r['sumMs']),(1.,.5,1.5))
        for raw in ([],[1,2,0,1,2,3],[2,1,3,1,2,3],[1,1,3,1,2,3],[1,2,3,1,2,-1]):
            with self.assertRaises(ValueError):decode_gpu_times(raw)

    def test_native_timing_cannot_claim_replacement_or_image_comparison(self):
        r=self.native();validate_native_timing(r)
        for key,value in (('comparison',{'equal':True}),('nativeDrawsReplaced',1),('nativeTiming',False),('state',2)):
            bad=dict(r,**{key:value})
            with self.assertRaises(RuntimeError):validate_native_timing(bad)
        for key in ('imports','mainExported','mainFallback'):
            r=self.native();r['nativeTargets'][key]=1
            with self.assertRaises(RuntimeError):validate_native_timing(r)

    def native(self):
        r=self.good();r['stats']=dict.fromkeys(FIELDS,0);r['stats'].update(mainSeen=40,peerSeen=41,mainEnded=1,peerEnded=1)
        r.update(comparison=None,nativeDifferences=None,nativeTiming=True,nativeDrawsReplaced=0,
                 coverage=[],masks=[],extraCoverage=[],sources=[],gpuTimes=decode_gpu_times([10,110,100000,20,120,100000]))
        r['nativeTargets']=dict(zip(NATIVE_FIELDS,(3,40,41,0,0,0,0,0)))
        r['optimized']=dict(zip(OPTIMIZED_FIELDS,(0,2**64-1,0,0,0,0,0,0)))
        return r

    def test_optimized_route_requires_gpu_admission_and_no_references(self):
        r=self.good();r['stats'].update(mainSeen=40,peerSeen=41)
        r.update(comparison=None,nativeDifferences=None,netDrawCommandReduction=30,nativeDrawsReplaced=30)
        r['nativeTargets']=dict(enabled=2,mainDraws=40,peerDraws=41,mainFallback=8,peerRuns=11,mainExported=1,peerExported=1,imports=2)
        r['optimized']=dict(enabled=1,gpuDecision=1,mainCommands=40,peerUnconditional=9,peerOptimizedRuns=2,peerFallbackBatches=32,referenceDraws=0,cpuAdmitted=1)
        validate(r,32,True,True)
        for key,value in (('gpuDecision',0),('referenceDraws',1),('mainCommands',39),('peerFallbackBatches',31),('cpuAdmitted',0)):
            old=r['optimized'][key];r['optimized'][key]=value
            with self.assertRaises(RuntimeError):validate(r,32,True,True)
            r['optimized'][key]=old
        r['optimized'].update(gpuDecision=0,cpuAdmitted=0);r.update(netDrawCommandReduction=0,nativeDrawsReplaced=0)
        validate(r,32,True,True,True)
        r['nativeDrawsReplaced']=1
        with self.assertRaises(RuntimeError):validate(r,32,True,True,True)

    def test_whole_target_requires_every_draw_and_actual_copyback(self):
        r=self.good();r['stats'].update(mainSeen=40,peerSeen=41);r['comparison']['draws']=40
        r['nativeTargets']=dict(enabled=1,mainDraws=40,peerDraws=41,mainFallback=8,peerRuns=10,mainExported=1,peerExported=1,imports=2)
        r['nativeDifferences']=[0,0,0,0];validate(r,32,True)
        for field in ('mainDraws','peerDraws','mainFallback','mainExported','peerExported','imports'):
            value=r['nativeTargets'][field];r['nativeTargets'][field]=0
            with self.assertRaises(RuntimeError):validate(r,32,True)
            r['nativeTargets'][field]=value
        r['nativeDifferences'][2]=1
        with self.assertRaises(RuntimeError):validate(r,32,True)

    def test_peer_only_coverage_and_unhandled_geometry_are_explicit(self):
        r=self.good();r['extraCoverage'][0]=[2,2];r['maskStats'].update(peerOnly=2,peerOnlyRuns=1,knownPeerOnly=100,unrecordedPeerOnly=98)
        validate(r,32)
        r['extraCoverage'][0]=[1,2]
        with self.assertRaises(RuntimeError):validate(r,32)
        r['extraCoverage'][0]=[2,2];r['maskStats']['unrecordedPeerOnly']=0
        with self.assertRaises(RuntimeError):validate(r,32)

    def test_main_only_lod_accounted_separately(self):
        r=self.good();r['coverage']=[[3,4]]*32;r['masks']=[[3,1]]*32;r['maskStats'].update(both=96,mainOnly=32,pairedRuns=64)
        validate(r,32)
        r['coverage'][0]=[4,4]
        with self.assertRaises(RuntimeError):validate(r,32)

    def test_missing_prediction_evidence_rejected(self):
        for field in ('masks','maskStats'):
            r=self.good();del r[field]
            with self.assertRaises(RuntimeError):validate(r,32)
        for field in ('mainPackets','peerPackets','mainInstances','peerInstances','both','pairedRuns'):
            r=self.good();r['maskStats'][field]=0
            with self.assertRaises(RuntimeError):validate(r,32)

    def test_empty_peer_requires_main_only_plan(self):
        raw=struct.pack('<4I8Q2f',64,64,32,1,0,0,0,0,0,200,0,0,0.,0.)
        self.assertFalse(decode_depth_comparison(raw)['equal'])
        image=decode_depth_comparison(raw,minimum_written=(0,16));self.assertTrue(image['equal'])
        r=self.good();r.update(comparison=image,coverage=[[0,4]]*32,masks=[[0,4]]*32)
        r['maskStats'].update(both=0,mainOnly=128)
        validate(r,32)
        r['comparison']['written'][0]=1
        with self.assertRaises(RuntimeError):validate(r,32)

    def test_missing_pixels_counts_and_inputs_do_not_pass(self):
        for key,value in [('state',2),('result',-1),('comparison',None),('comparison',dict(equal=False,draws=32))]:
            r=self.good();r[key]=value
            with self.assertRaises(RuntimeError):validate(r,32)
        for key in ('recorded','matched','inputMismatch','failure','mainEnded','peerEnded'):
            r=self.good();r['stats'][key]=r['stats'][key]+1 if key in ('inputMismatch','failure') else 0
            with self.assertRaises(RuntimeError):validate(r,32)


if __name__=='__main__':unittest.main()
