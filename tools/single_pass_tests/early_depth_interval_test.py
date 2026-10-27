import copy
import unittest
from early_depth_interval import STATS,validate,cpu_profile,validate_origin,frame_cadence
from early_depth_report import MAX_SOURCES


def fixture(frames=240,cancelled=False):
    rows=[dict(frame=i+1,selected=128,matched=128,mainSources=225,peerSources=226,mainCommands=228,
               peerUnconditional=97,peerOptimized=0,peerFallback=129,decision=1,cpuAdmitted=1,references=0,
               mainTicks=100,mainFrequency=1000000,peerTicks=100,peerFrequency=1000000) for i in range(frames)]
    stats=dict.fromkeys(STATS,0)
    stats.update(considered=frames,recorded=frames,completed=frames,admitted=frames,created=1,reused=frames-1,constantDraws=128*frames,
                 mainSources=225*frames,peerSources=226*frames,executed=325*frames,maxPool=1,cancelled=int(cancelled))
    p=dict.fromkeys(('RunFrames','RootSkipped','PairMerged','PairCompleted','DepthMain','DepthVrcam','RunEyeCopies','RunEyeSubmissions'),frames)
    p.update(RunState=2,RunFailure=0)
    return dict(state=2,stats=stats,rows=rows,preparation=p,liveFailure=0,traceCountsUnchanged=True,arenas=[1,frames-1],geometryChecks=[2,0],instanceProofs=[0,0],
                descriptorStorage=dict(dense=True,active=1),
                liveMetadata=dict(enabled=True,samples=3,mismatches=0,afterActive=0),
                uploadLookup=dict(enabled=True,counts=[10000*frames,0,10000*frames,100*frames,10000*frames,10000*frames]),
                frameStarts=list(range(1000,1000+frames*100,100)),cpu=dict(frequency=1000),
                cadence=frame_cadence(rows,list(range(1000,1000+frames*100,100)),1000),
                origin=dict(scoped=True,compatible=1,verified=1,before=[128,0,0,0],after=[128,0,frames*128,0]))


class EarlyDepthIntervalTests(unittest.TestCase):
    def test_upload_lookup_requires_actual_receipts_and_isolated_baseline(self):
        r=fixture();validate(r,240)
        for counts in ([0,0,50000,1,50000,50000],[50000,0,0,1,50000,50000],[50000,0,50000,-1,50000,50000],
                       [50000,0,50000,1,0,50000],[50000,0,50000,1,50000,0],[]):
            r['uploadLookup']['counts']=counts
            with self.assertRaises(RuntimeError):validate(r,240)
        r['uploadLookup']=dict(enabled=False,counts=[0,0,0,50000,0,0]);validate(r,240)
        for counts in ([1,0,0,50000,0,0],[0,0,1,50000,0,0],[0,0,0,1,0,0],[0,0,0,50000,1,0],[0,0,0,50000,0,1]):
            r['uploadLookup']['counts']=counts
            with self.assertRaises(RuntimeError):validate(r,240)
    def test_metadata_needs_actual_active_samples_and_capture_restoration(self):
        r=fixture();validate(r,240)
        for key,value in (('enabled',1),('samples',0),('mismatches',1),('afterActive',1)):
            bad=copy.deepcopy(r);bad['liveMetadata'][key]=value
            with self.assertRaises(RuntimeError):validate(bad,240)
        r['liveMetadata']['enabled']=False;validate(r,240)
    def test_native_camera_requires_every_frame_and_no_private_banks(self):
        r=fixture();r['nativeCamera']=dict(enabled=True,stats=[240,240*128],stages=1,stageMasks=[240,240,240,0]);r['arenas']=[0,0]
        validate(r,240)
        r['nativeCamera']['stats'][1]-=1
        with self.assertRaises(RuntimeError):validate(r,240)
        r['nativeCamera']['stats'][1]+=1;r['arenas']=[1,239]
        with self.assertRaises(RuntimeError):validate(r,240)
    def test_frame_cadence_uses_real_contiguous_roots_not_draw_counts(self):
        rows=[dict(frame=3),dict(frame=1),dict(frame=2)]
        actual=frame_cadence(rows,[1300,1000,1100],1000)
        self.assertEqual(actual['medianMs'],150.)
        self.assertAlmostEqual(actual['pairsPerSecond'],1000/150)
        for times in ([1300,1000,1000],[1300,0,1100],[]):
            with self.assertRaises(ValueError):frame_cadence(rows,times,1000)
        with self.assertRaises(ValueError):frame_cadence([dict(frame=1),dict(frame=3)],[1000,1300],1000)
    def test_descriptor_mode_is_not_just_a_requested_switch(self):
        r=fixture();r['descriptorStorage']=dict(dense=False,active=0);validate(r,240)
        for value in ({},dict(dense=True,active=0),dict(dense=False,active=1)):
            r['descriptorStorage']=value
            with self.assertRaises(RuntimeError):validate(r,240)
    def test_full_cohort_above_old_limit_and_capacity_guard(self):
        r=fixture()
        for row in r['rows']:row.update(selected=203,matched=203)
        r['stats']['constantDraws']=203*240;r['origin']['after'][2]=203*240
        validate(r,240)
        r['rows'][0].update(selected=MAX_SOURCES,matched=MAX_SOURCES,mainSources=MAX_SOURCES)
        with self.assertRaises(RuntimeError):validate(r,240)

    def test_origin_needs_reference_and_actual_scope_usage(self):
        good=fixture()['origin'];validate_origin(good,240*128)
        legacy=dict(good,scoped=False,after=[128,0,0,240*128]);validate_origin(legacy,240*128)
        mixed=dict(good,after=[128,0,240*123,240*5]);validate_origin(mixed,240*128,240*5)
        for calls in (0,240*4,240*6):
            with self.assertRaises(RuntimeError):validate_origin(mixed,240*128,calls)
        for bad in (dict(good,verified=0),dict(good,compatible=0),dict(good,before=[127,0,0,0]),
                    dict(good,after=[128,1,240*128,0]),dict(good,after=[128,0,240*128,1]),dict(good,after=[128,0,0,0])):
            with self.assertRaises(RuntimeError):validate_origin(bad,240*128)
    def test_cpu_profile_preserves_nested_phases(self):
        r=cpu_profile([2,10]*5,10000)
        self.assertEqual(r['membership'],dict(calls=2,milliseconds=1.))
        self.assertEqual(r['drawTotal'],dict(calls=2,milliseconds=1.))
        with self.assertRaises(ValueError):cpu_profile([0]*9,1)
        with self.assertRaises(ValueError):cpu_profile([0]*10,0)

    def test_completed_and_cancelled_pool(self):
        validate(fixture(),240)
        r=fixture(61,True);r['preparation']['RunFrames']=62;r['preparation']['DepthMain']=62;r['preparation']['DepthVrcam']=62
        validate(r,240,60)
        r['preparation']['PairCompleted']=62
        with self.assertRaises(RuntimeError):validate(r,240,60)

    def test_counts_and_fences_must_be_complete(self):
        for key,value in (('completed',239),('reused',0),('created',4),('maxPool',2),('references',1),('busy',1),('failure',1)):
            r=fixture();r['stats'][key]=value
            with self.assertRaises(RuntimeError):validate(r,240)
        for key,value in (('mainFrequency',0),('decision',2),('references',1),('matched',127),('cpuAdmitted',0)):
            r=fixture();r['rows'][0][key]=value
            with self.assertRaises(RuntimeError):validate(r,240)
        r=fixture();r['traceCountsUnchanged']=False
        with self.assertRaises(RuntimeError):validate(r,240)
        for checks in ([1,0],[2,1],[]):
            r=fixture();r['geometryChecks']=checks
            with self.assertRaises(RuntimeError):validate(r,240)

    def test_fallback_is_counted_not_a_skipped_draw(self):
        r=fixture();r['rows'][0].update(decision=0,cpuAdmitted=0,matched=127)
        r['stats'].update(admitted=239,fallback=1,executed=325*240+129)
        validate(r,240)
        bad=copy.deepcopy(r);bad['stats']['executed']-=129
        with self.assertRaises(RuntimeError):validate(bad,240)

    def test_no_eligible_prediction_is_an_entire_native_frame(self):
        r=fixture();r['rows'][0].update(selected=0,matched=0,decision=0,cpuAdmitted=0,mainCommands=225,peerUnconditional=226,peerFallback=0)
        r['stats'].update(admitted=239,fallback=1,constantDraws=239*128,executed=325*239+451)
        validate(r,240)
        r['rows'][0]['decision']=1
        with self.assertRaises(RuntimeError):validate(r,240)


if __name__=='__main__':unittest.main()
