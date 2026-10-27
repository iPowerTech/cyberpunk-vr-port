import unittest
from unittest import mock
from pathlib import Path
import io
import json
import struct
import read_prepared_pair
from read_prepared_pair import validate, validate_gpu, retry_empty, exhausted, validate_sequence, validate_descriptors


def baseline_stats(rejected=False):
    values=[2,3,4,10,34000,180]+[0]*18
    if rejected:
        values[18:]=[1,1,0,1,1,1]
    else:
        values[5]=500;values[6:10]=[250,240,250,240];values[18:]=[0,0,1,1,0,1]
    return values


class PreparedPairReportTests(unittest.TestCase):
    def refinement_fixture(self):
        native=[1,2,10,34000,3,4,3,4]+[0]*8+[3,4,3,4,1,0,0,1]
        options=dict(required=True,allow_vrs_refinement=True,acceptance=[1,1,1,0,0,0,0,1],
                     rate=[1,2,2,10,34000]+[0]*8+[1,1,0],attribution=[16,10,10,1,1,3,4,0,0,1,1,1],pair_stats=[0]*16)
        return native,baseline_stats(),options

    def test_approved_vrs_refinement_keeps_factual_native_inequality(self):
        native,baseline,options=self.refinement_fixture()
        report=read_prepared_pair.validate_native_exit(native,baseline,**options)
        self.assertEqual(report['nativeEquivalent'],0)
        self.assertEqual(report['referenceColorLeft'],3)
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,required=True)
        native[4:20]=[0]*16;native[21]=1
        options.update(rate=[0]*16,attribution=[0]*12,acceptance=[1,0,0,0,0,0,0,1])
        self.assertEqual(read_prepared_pair.validate_native_exit(native,baseline,**options)['nativeEquivalent'],1)

    def test_refinement_requires_every_proof_and_the_explicit_policy(self):
        import copy
        native,baseline,options=self.refinement_fixture()
        for name,fields in (('rate',(0,1,2,3,4,13,14)),('attribution',(0,1,2,3,4,9,10,11))):
            for field in fields:
                bad=copy.deepcopy(options);bad[name][field]=0
                with self.subTest(name=name,field=field),self.assertRaises(ValueError):
                    read_prepared_pair.validate_native_exit(native,baseline,**bad)
        for field in range(8):
            bad=copy.deepcopy(options);bad['acceptance'][field]+=1
            with self.subTest(acceptance=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(native,baseline,**bad)
        for field in (0,1,2,3,20):
            bad=native.copy();bad[field]=0
            with self.subTest(native=field),self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(bad,baseline,**options)
        for key,value in (('allow_vrs_refinement',False),('acceptance',None),('rate',None),('attribution',None),('pair_stats',None)):
            bad=copy.deepcopy(options);bad[key]=value
            with self.subTest(missing=key),self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,**bad)
        bad=copy.deepcopy(options);bad['acceptance'][0]=0
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,**bad)

    def test_refinement_rejects_unexplained_color_depth_stencil_and_pair_differences(self):
        import copy
        native,baseline,options=self.refinement_fixture()
        for eye in range(2):
            bad=copy.deepcopy(options);bad['attribution'][5+eye]-=1;bad['attribution'][7+eye]=1
            bad['acceptance'][3+eye]=1;bad['acceptance'][7]=0
            with self.subTest(outside=eye),self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,**bad)
            bad=copy.deepcopy(options);bad['pair_stats'][6+eye]=1;bad['acceptance'][5+eye]=1;bad['acceptance'][7]=0
            with self.subTest(pair=eye),self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,**bad)
        for field in range(8,16):
            changed=native.copy();changed[field]=1;changed[16+field%4]+=1
            bad=copy.deepcopy(options);bad['acceptance'][3+field%2]=1;bad['acceptance'][7]=0
            with self.subTest(depth_or_stencil=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(changed,baseline,**bad)
        bad=copy.deepcopy(options);bad['rate'][5]=bad['rate'][11]=1;bad['rate'][14]=0
        bad['acceptance'][1]=bad['acceptance'][7]=0
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,**bad)

    def test_rejected_inputs_never_acquire_refinement_acceptance(self):
        native,baseline,options=self.refinement_fixture()
        native[22]=1;options['rate'][14:]=[0,1];options['acceptance'][1]=options['acceptance'][7]=0
        report=read_prepared_pair.validate_native_exit(native,baseline,rejected=True,**options)
        self.assertEqual(report['inputRejected'],1)
        options['acceptance'][7]=1
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,rejected=True,**options)
        with self.assertRaises(ValueError):
            read_prepared_pair.validate_native_exit([0]*24,None,allow_vrs_refinement=True)

    def test_native_rate_diagnostics_preserve_exact_and_unequal_calibration(self):
        baseline=baseline_stats()
        native=[1,2,10,34000,3,4,3,4]+[0]*8+[3,4,3,4,1,0,0,1]
        rate=[1,2,2,10,34000]+[0]*8+[1,1,0]
        attribution=[16,10,10,1,1,3,4,0,0,1,1,1]
        report=read_prepared_pair.validate_rate_diagnostics(rate,attribution,baseline,native)
        self.assertEqual(report['nativeRate']['equivalent'],1)
        self.assertEqual(report['vrsAttribution']['referenceColorDiffInsideLeft'],3)
        unequal=rate.copy();unequal[5:7]=[3,4];unequal[11:13]=[3,4];unequal[14]=0
        report=read_prepared_pair.validate_rate_diagnostics(tuple(unequal),tuple(attribution),baseline,native)
        self.assertEqual(report['nativeRate']['equivalent'],0)
        rejected=rate.copy();rejected[14:]=[0,1]
        report=read_prepared_pair.validate_rate_diagnostics(rejected,attribution,baseline,native)
        self.assertEqual(report['nativeRate']['equivalent'],0)
        rejected[14]=1
        with self.assertRaises(ValueError):read_prepared_pair.validate_rate_diagnostics(rejected,attribution,baseline,native)
        outside=attribution.copy();outside[5]=1;outside[7]=2;outside[10]=0
        report=read_prepared_pair.validate_rate_diagnostics(rate,outside,baseline,native)
        self.assertEqual(report['vrsAttribution']['referenceColorDiffOutsideLeft'],2)
        for field,value in ((0,2),(1,1),(2,0),(2,513),(3,9),(4,34001),(5,3001),(7,1001),(9,1001),
                            (11,1),(12,1),(13,2),(14,0),(15,2)):
            bad=rate.copy();bad[field]=value
            with self.subTest(rate_field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_rate_diagnostics(bad,attribution,baseline,native)
        for field,value in ((0,0),(1,0),(1,513),(2,0),(2,513),(3,2),(4,2),(5,2),(6,5),(7,1),(8,1),(9,2),(10,2),(11,2)):
            bad=attribution.copy();bad[field]=value
            with self.subTest(attribution_field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_rate_diagnostics(rate,bad,baseline,native)
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(native,baseline,required=True)

    def test_native_rate_partial_diagnostics_are_descriptive_and_non_native_is_zero(self):
        zero=read_prepared_pair.validate_rate_diagnostics([0]*16,[0]*12,None,None)
        self.assertFalse(any(zero['nativeRate'].values()));self.assertFalse(any(zero['vrsAttribution'].values()))
        partial=[1,2,2,0,0]+[0]*11
        attribution=[16,10,10,1,0]+[0]*7
        report=read_prepared_pair.validate_rate_diagnostics(partial,attribution,None,None)
        self.assertEqual(report['nativeRate']['recordedDraws'],2)
        self.assertEqual(report['nativeRate']['compared'],0)
        for rate,attr in (([0]*15,[0]*12),([0]*16,[0]*11),([True]*16,[0]*12),([0]*16,[-1]*12),
                          ([0]*16,[2**64]*12),([0]*13+[1,0,0],[0]*12),([0]*16,attribution)):
            with self.subTest(rate=rate,attribution=attr),self.assertRaises(ValueError):
                read_prepared_pair.validate_rate_diagnostics(rate,attr,None,None)

    def test_native_tail_requires_complete_exact_native_exit_planes(self):
        baseline=baseline_stats()
        good=[1,2,10,34000]+[0]*16+[1,1,0,1]
        report=read_prepared_pair.validate_native_exit(good,baseline,required=True)
        self.assertEqual(report['nativeEquivalent'],1)
        for field in range(24):
            bad=good.copy();bad[field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(bad,baseline,required=True)
        for field in (0,1,2,3,20,21,23):
            bad=good.copy();bad[field]=0
            with self.subTest(missing=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(bad,baseline,required=True)
        for values in (None,[],good[:-1],good+[0],[True]*24,[-1]*24,[2**64]*24):
            with self.subTest(values=values),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(values,baseline,required=True)
        for payload in (0,34001,128*1024*1024+1):
            bad=baseline.copy();bad[4]=payload
            with self.subTest(payload=payload),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(good,bad,required=True)
        for values in ([0]*24,[0]*23+[1]):read_prepared_pair.validate_native_exit(values,None)
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(good,baseline)

    def test_rejected_tail_native_differences_are_bounded_and_totals_exact(self):
        baseline=baseline_stats(rejected=True)
        good=[1,2,10,34000,3,4,3,4,5,6,5,6,7,8,7,8,15,18,15,18,1,0,1,1]
        report=read_prepared_pair.validate_native_exit(good,baseline,required=True,rejected=True)
        self.assertEqual(report['nativeEquivalent'],0)
        for field in (16,17,18,19,20,21,22,23):
            bad=good.copy();bad[field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(bad,baseline,required=True,rejected=True)
        for field in range(4,16):
            bad=good.copy();bad[field]=3001 if field<8 else 1001
            bad[16:20]=[sum(bad[offset+i] for offset in (4,8,12)) for i in range(4)]
            with self.subTest(bounds=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_native_exit(bad,baseline,required=True,rejected=True)
        equal=[1,2,10,34000]+[0]*16+[1,1,1,1]
        read_prepared_pair.validate_native_exit(equal,baseline,required=True,rejected=True)
        with self.assertRaises(ValueError):read_prepared_pair.validate_native_exit(good,baseline,required=True)

    def test_full_baseline_accepts_depth_or_stencil_only_contribution(self):
        gpu=[300,100,150,0,0,100,80,60,0,0,0,50]*3
        stats=[20,10,8192,1,2,1,0,0,0,0,0,4,1000,17,4,0]
        for plane in (10,14):
            baseline=baseline_stats();baseline[5:10]=[0,0,0,0,0];baseline[plane:plane+4]=[7,8,7,8]
            report=read_prepared_pair.validate_baseline(baseline,stats,gpu,[0]*12)
            self.assertEqual(report['fullBaselineCompared'],1)
            self.assertEqual(report['referenceColorDeltaLeft'],0)
            validate(5,stats);validate_gpu(gpu,[0]*12,[0]*13+[1,1,3])
            for field in (2,7,11,14,19,23,26,31,35):
                bad=gpu.copy();bad[field]=0
                with self.subTest(plane=plane,field=field),self.assertRaises(ValueError):
                    read_prepared_pair.validate_baseline(baseline,stats,bad,[0]*12)
            bad=baseline.copy();bad[plane]=bad[plane+2]=0
            with self.assertRaises(ValueError):read_prepared_pair.validate_baseline(bad,stats,gpu,[0]*12)

    def test_baseline_requires_all_planes_exact_deltas_and_true_execution_flags(self):
        baseline=baseline_stats()
        stats=[20,10,8192,1,2,1,0,0,250,240,0,4,1000,17,4,0]
        gpu=[300,100,150,0,0,100,80,60,0,0,0,50]*3
        colors=[0,50,50,0,40,40]*2
        read_prepared_pair.validate_baseline(baseline,stats,gpu,colors)
        for field,value in ((0,1),(1,4),(2,3),(3,9),(4,0),(4,34001),(4,128*1024*1024+1),(5,6001),
                            (6,3001),(8,251),(10,1),(12,1),(14,1),(16,1),(18,1),(19,1),(20,0),(21,0),(22,1),(23,0)):
            bad=baseline.copy();bad[field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                read_prepared_pair.validate_baseline(bad,stats,gpu,colors)
        for field in (6,7,8,9):
            bad=stats.copy();bad[field]+=1
            with self.subTest(stat=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_baseline(baseline,bad,gpu,colors)
        for bad in (None,[],[0]*23,[0]*25,[True]*24,[-1]*24,[2**64]*24):
            with self.subTest(baseline=bad),self.assertRaises(ValueError):
                read_prepared_pair.validate_baseline(bad,stats,gpu,colors)
        for bad in ([1001,0,0,0,0,0]*2,[0]*6+colors[6:]):
            with self.assertRaises(ValueError):read_prepared_pair.validate_baseline(baseline,stats,gpu,bad)

    def test_rejected_nonzero_baseline_requires_zero_deltas_and_all_query_words(self):
        baseline=baseline_stats(rejected=True)
        stats=[20,10,8192,0,0,0,0,0,0,0,29,4,1000,17,4,0]
        colors=[0,50,50,0,40,40]*2
        report=read_prepared_pair.validate_baseline(baseline,stats,[0]*36,colors,rejected=True)
        self.assertEqual(report['baselineNonzeroColorPixels'],180)
        self.assertEqual(report['rejectedUnchanged'],1)
        for field in range(6,24):
            bad=baseline.copy();bad[field]=1-bad[field]
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_baseline(bad,stats,[0]*36,colors,rejected=True)
        for field in range(36):
            gpu=[0]*36;gpu[field]=1
            with self.subTest(query=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_baseline(baseline,stats,gpu,colors,rejected=True)
        bad=baseline.copy();bad[5]+=1
        with self.assertRaises(ValueError):read_prepared_pair.validate_baseline(bad,stats,[0]*36,colors,rejected=True)

    def test_current_result_requires_owned_same_frame_completed_lifecycle(self):
        good=[1,77,77,77,1,1,1,1,2,0,0,0,0,4,0,0]
        self.assertEqual(read_prepared_pair.validate_current(good,2,77)['sourceFrame'],77)
        for field in (*range(13),14,15):
            bad=good.copy();bad[field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(bad,2,77)
        for field in (4,5,6,7):
            bad=good.copy();bad[field]=0
            with self.subTest(incomplete=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(bad,2,77)
        for frame in (0,78,True,2**64):
            with self.subTest(frame=frame),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(good,2,frame)
        for sources in (0,1,3,513,True):
            with self.subTest(sources=sources),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(good,sources,77)
        rejected=good.copy();rejected[12]=29
        read_prepared_pair.validate_current(rejected,2,77,rejected=True)
        with self.assertRaises(ValueError):read_prepared_pair.validate_current(rejected,2,77)
        with self.assertRaises(ValueError):read_prepared_pair.validate_current(good,2,77,rejected=True)
        for values in (None,[],[0]*15,[0]*17,[True]*16,[-1]*16,[2**64]*16):
            with self.subTest(values=values),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(values,2,77)
        for count in (0,4,65536):
            values=good.copy();values[13]=count
            self.assertEqual(read_prepared_pair.validate_current(values,2,77)['assetLeases'],count)
        for count in (-1,65537,True):
            values=good.copy();values[13]=count
            with self.subTest(asset_leases=count),self.assertRaises(ValueError):
                read_prepared_pair.validate_current(values,2,77)

    def test_current_residency_uses_sdk_leases_without_native_registration(self):
        residency=[4,2,5,1,1,1,0,0]
        self.assertEqual(read_prepared_pair.validate_current_residency(residency,4,0)['sourceLeases'],4)
        read_prepared_pair.validate_current_residency([0]*8,0,0)
        for leases in (None,-1,0,3,5,65537,True):
            with self.subTest(leases=leases),self.assertRaises(ValueError):
                read_prepared_pair.validate_current_residency(residency,leases,0)
        for registered in (None,-1,1,4,True):
            with self.subTest(registered=registered),self.assertRaises(ValueError):
                read_prepared_pair.validate_current_residency(residency,4,registered)
        with self.assertRaises(ValueError):
            read_prepared_pair.validate_current_residency([0]*8,4,0)

    def test_current_source_evidence_precedes_final_submit_and_readback(self):
        good=[1,77,2,1,0,0,0,0,0,1,1,1,123,4,5,0]
        for submitted in ([0,0],[1,0],[0,1]):
            values=good.copy();values[4:6]=submitted
            read_prepared_pair.validate_current_source(values,77,2)
        for field,value in ((0,0),(1,78),(2,3),(3,0),(4,2),(5,2),(6,1),(7,1),(8,1),
                            (9,0),(10,0),(11,0),(12,0),(13,0),(14,0),(15,1)):
            bad=good.copy();bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_current_source(bad,77,2)
        bad=good.copy();bad[4:6]=[1,1]
        with self.assertRaises(ValueError):read_prepared_pair.validate_current_source(bad,77,2)
        for values in (None,[],[0]*15,[0]*17,[True]*16,[-1]*16,[2**64]*16):
            with self.subTest(values=values),self.assertRaises(ValueError):
                read_prepared_pair.validate_current_source(values,77,2)

    def test_current_observer_options_fail_before_process_access(self):
        for flags in (['--observe-current'],['--source-frame','77'],
                      ['--allow-vrs-refinement'],['--allow-vrs-refinement','--observe-current','--sequence'],
                      ['--native-tail'],['--native-tail','--sequence'],
                      ['--observe-current','--sequence','--all-pairs'],
                      ['--observe-current','--sequence','--source-frame','0']):
            with self.subTest(flags=flags),mock.patch('sys.argv',['read_prepared_pair','--pid','123','--out','unused.json',*flags]),\
                    mock.patch('sys.stderr',io.StringIO()),mock.patch.object(read_prepared_pair,'resolve') as resolve:
                with self.assertRaises(SystemExit) as error:read_prepared_pair.main()
                self.assertEqual(error.exception.code,2);resolve.assert_not_called()

    def test_current_observation_never_writes_requests_faults_or_cleanup_controls(self):
        for rejected,tail,native_error,allow in ((False,False,0,False),(True,False,0,False),(False,True,0,False),
                                                (True,True,0,False),(False,True,43,False),(False,True,0,True),(True,True,0,True)):
            values=[20,10,8192,1,2,1,0,0,250,240,0,4,1000,17,0,0]
            inputs=[2,4,518,5,520,1,0,16]
            mesh=[1,3,4096,4,1,0,0,16,0,0,1,2]
            gpu=[300,100,150,0,0,100,80,60,0,0,0,50]*3
            colors=[0,50,50,0,40,40]*2
            if rejected:
                values[3:11]=[0,0,0,0,0,0,0,29];inputs[5:7]=[0,1];mesh[4:7]=[0,0,1]
                gpu=[0]*36
            native_exit=([1,2,10,34000]+[0]*16+[1,1,int(rejected),1]) if tail else [0]*24
            if tail and (rejected or native_error or allow):
                native_exit[4:8]=[3,4,3,4];native_exit[16:20]=[3,4,3,4];native_exit[21]=0
            native_rate=([1,2,2,10,34000]+[0]*8+[1,1,int(rejected)]) if tail else [0]*16
            attribution=([16,10,10,1,1,native_exit[4],native_exit[5],0,0,1,1,1]) if tail else [0]*12
            if tail and rejected:
                native_rate[5:7]=[3,4];native_rate[11:13]=[3,4];native_rate[14]=0
            acceptance=[int(allow),native_rate[14],1,0,0,0,0,int(not rejected and not native_error)] if tail else [0]*8
            if native_error:values[10]=native_error
            arrays={
                'CyberpunkVR_PreparedPairStats':values,
                'CyberpunkVR_PreparedCurrent':[1,77,77,77,1,1,1,1,2,0,0,0,29 if rejected else native_error,4,0,0],
                'CyberpunkVR_PreparedPairResidency':[4,2,5,1,1,1,0,0],
                'CyberpunkVR_PreparedPairCandidates':[0]*16,'CyberpunkVR_PreparedPairStateMismatch':[0]*24,
                'CyberpunkVR_PreparedPairVrs':[0]*8,'CyberpunkVR_PreparedPairVrsGpu':[0]*4,
                'CyberpunkVR_PreparedPairGpu':gpu,'CyberpunkVR_PreparedPairColor':colors,
                'CyberpunkVR_PreparedBaseline':baseline_stats(rejected),
                'CyberpunkVR_PreparedNativeExit':native_exit,
                'CyberpunkVR_PreparedNativeRate':native_rate,
                'CyberpunkVR_PreparedVrsAttribution':attribution,
                'CyberpunkVR_PreparedNativeAcceptance':acceptance,
                'CyberpunkVR_PreparedNativeExitSamples':[0]*160,
                'CyberpunkVR_PreparedPairDepthState':[0]*13+[1,1,3],
                'CyberpunkVR_PreparedBatchDescriptors':[100,66,1,0],
                'CyberpunkVR_PreparedPairMeshStats':mesh,
                'CyberpunkVR_PreparedPairMeshResults':[0,0] if rejected else [1,0],
                'CyberpunkVR_PreparedPairInputStats':inputs,
                'CyberpunkVR_PreparedBatchStats':[1,1,1,1,0,0,1,2,3,0,0,1,4,1,0,1],
                'CyberpunkVR_PreparedBatchSources':[20,0,10,1],'CyberpunkVR_PreparedBatchPlan':[3,20,10,0],
            }
            blocks={name:struct.pack(f'<{len(data)}Q',*data) for name,data in arrays.items()}
            counters={'CyberpunkVR_PreparedPairState':6 if rejected or native_error else 5,'CyberpunkVR_PreparedPairRequest':0,
                      'CyberpunkVR_PreparedPairSelectionCount':1,'CyberpunkVR_PreparedBatchSourceCount':2,'CyberpunkVR_PreparedBatchPlanCount':1}
            blocks.update({name:struct.pack('<I',value) for name,value in counters.items()})
            symbols={name:hex(index+1) for index,name in enumerate(blocks)};symbols['sha256']='build'
            memory={int(symbols[name],16):block for name,block in blocks.items()}
            reader=mock.Mock()
            def read(address,size):
                self.assertEqual(len(memory[address]),size)
                return memory[address]
            reader.read.side_effect=read
            kernel=mock.Mock();kernel.OpenProcess.return_value=123;kernel.WaitForSingleObject.return_value=258
            argv=['read_prepared_pair','--pid','123','--game','C:/fixture','--out','unused.json',
                  '--observe-current','--sequence','--source-frame','77']+(['--native-tail'] if tail else [])+(['--reject-inputs'] if rejected else [])+(['--allow-vrs-refinement'] if allow else [])
            with self.subTest(rejected=rejected,tail=tail,error=native_error,allow=allow),mock.patch('sys.argv',argv),mock.patch('sys.stdout',io.StringIO()),\
                    mock.patch.object(read_prepared_pair,'resolve',return_value=symbols),\
                    mock.patch.object(read_prepared_pair,'Reader',return_value=reader),\
                    mock.patch.object(read_prepared_pair.c,'WinDLL',return_value=kernel),\
                    mock.patch.object(Path,'write_text') as write_text:
                self.assertEqual(read_prepared_pair.main(),int(bool(native_error)))
                saved=json.loads(write_text.call_args.args[0])
                self.assertEqual(saved['passed'],not native_error);self.assertEqual(saved['cleanup'],'observation-only')
                self.assertEqual(saved['current']['sourceFrame'],77)
                self.assertEqual(saved['current']['assetLeases'],4)
                self.assertEqual(saved['stats']['registeredAssets'],0)
                self.assertEqual(saved['protectionProvenance'],'sdk_residency')
                self.assertEqual(saved['baseline']['fullBaselineCompared'],1)
                self.assertIn('complete final stage-9 tail' if tail else 'not full native pass equivalence',saved['referenceBaselinePolicy'])
                if tail:
                    self.assertEqual(len(saved['nativeExitSamples']),10)
                    self.assertEqual(saved['nativeExit']['referenceTotalLeft'],3 if rejected or native_error or allow else 0)
                    self.assertEqual(saved['nativeRateRaw'],native_rate)
                    self.assertEqual(saved['vrsAttributionRaw'],attribution)
                    self.assertEqual(saved['nativeRate']['equivalent'],int(not rejected))
                    self.assertEqual(saved['vrsAttribution']['referenceColorDiffOutsideLeft'],0)
                    self.assertEqual(saved['nativeAcceptanceRaw'],acceptance)
                    self.assertEqual(saved['acceptedUnderVrsRefinement'],allow and not rejected)
                    if allow:self.assertEqual(saved['nativeExit']['nativeEquivalent'],0)
                    if native_error:
                        self.assertEqual(saved['current']['error'],43)
                        self.assertEqual(saved['vrsAttribution']['referenceColorDiffInsideLeft'],3)
                else:
                    self.assertFalse(any(saved['nativeRate'].values()))
                    self.assertFalse(any(saved['vrsAttribution'].values()))
                    self.assertFalse(any(saved['nativeAcceptance'].values()))
                self.assertEqual(saved.get('inputRejectionVerified',False),rejected)
                kernel.WriteProcessMemory.assert_not_called()
                kernel.OpenProcess.assert_called_once_with(0x100400,False,123)

    def test_residency_requires_exact_registered_leases_and_retired_pageables(self):
        report=read_prepared_pair.validate_residency([4,2,5,1,1,1,0,0],4)
        self.assertEqual(report,dict(sourceLeases=4,physicalPageables=2,state=5,markedSubmitted=1,
                                     associatedFence=1,retired=1,lastHRESULT=0,processQuarantineCount=0))
        read_prepared_pair.validate_residency([4,4,5,1,1,1,0,0],4)
        for field,value in ((0,0),(0,3),(0,5),(1,0),(1,5),(3,0),(3,2),(4,0),(4,2),(5,0),(5,2),
                            (6,1),(6,0x80004005),(6,2**32),(7,1)):
            values=[4,2,5,1,1,1,0,0];values[field]=value
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                read_prepared_pair.validate_residency(values,4)
        for state in (0,1,2,3,4,6,7,2**64-1):
            with self.subTest(state=state),self.assertRaises(ValueError):
                read_prepared_pair.validate_residency([4,2,state,1,1,1,0,0],4)

    def test_asset_free_pair_requires_all_zero_residency(self):
        report=read_prepared_pair.validate_residency([0]*8,0)
        self.assertTrue(all(value==0 for value in report.values()))
        for field in range(8):
            values=[0]*8;values[field]=1
            with self.subTest(field=field),self.assertRaises(ValueError):
                read_prepared_pair.validate_residency(values,0)
        with self.assertRaises(ValueError):
            read_prepared_pair.validate_residency([0]*8,4)
        with self.assertRaises(ValueError):
            read_prepared_pair.validate_residency([4,2,5,1,1,1,0,0],0)

    def test_residency_rejects_malformed_counter_abi(self):
        for values in (None,[],[0]*7,[0]*9):
            with self.subTest(values=values),self.assertRaises(ValueError):
                read_prepared_pair.validate_residency(values,4)
        for field in range(8):
            for value in (-1,2**64,True,1.0,'1'):
                values=[4,2,5,1,1,1,0,0];values[field]=value
                with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                    read_prepared_pair.validate_residency(values,4)
        for registered in (-1,2**64,True,4.0,'4',3,5):
            with self.subTest(registered=registered),self.assertRaises(ValueError):
                read_prepared_pair.validate_residency([4,2,5,1,1,1,0,0],registered)

    def test_gpu_vrs_requires_actual_dispatch_and_post_render_exact_pixels(self):
        values=[93,93,8649,227,95,132,2,0]
        gpu=[248,2,24000,0]
        read_prepared_pair.validate_vrs(values,gpu,248)
        read_prepared_pair.validate_vrs([0]*8,[0]*4,2)
        for field,value in ((0,0),(1,513),(2,8648),(3,8650),(6,1),(7,1)):
            bad=values.copy();bad[field]=value
            with self.assertRaises(ValueError):read_prepared_pair.validate_vrs(bad,gpu,248)
        for field,value in ((0,0),(0,249),(1,0),(1,1),(2,8649),(3,1)):
            bad=gpu.copy();bad[field]=value
            with self.assertRaises(ValueError):read_prepared_pair.validate_vrs(values,bad,248)

    def test_rejected_gpu_inputs_never_reach_either_reference_or_paired_shader(self):
        values=[20,10,8192,0,0,0,0,0,0,0,29,4,1000,17,4,0]
        inputs=[2,4,518,5,520,0,1,16]
        mesh=[1,3,4096,4,0,0,1,16,0,0,1,2]
        gpu=[0]*36;colors=[0,50,50,0,40,40]*2;depth=[0]*13+[1,1,3]
        baseline=baseline_stats(rejected=True)
        validate=lambda *args:read_prepared_pair.validate_input_rejection(*args,baseline)
        validate(6,values,inputs,mesh,[[0,0]],gpu,colors,depth,2)
        for index in range(36):
            bad=gpu.copy();bad[index]=1
            with self.assertRaises(ValueError):validate(6,values,inputs,mesh,[[0,0]],bad,colors,depth,2)
        for row in ([1,0],[0,1]):
            with self.assertRaises(ValueError):validate(6,values,inputs,mesh,[row],gpu,colors,depth,2)
        for index in (3,4,5,6,8,10,15):
            bad=values.copy();bad[index]+=1
            with self.assertRaises(ValueError):validate(6,bad,inputs,mesh,[[0,0]],gpu,colors,depth,2)
        bad=inputs.copy();bad[5:7]=[0,0]
        with self.assertRaises(ValueError):validate(6,values,bad,mesh,[[0,0]],gpu,colors,depth,2)
        bad=colors.copy();bad[0]=1
        with self.assertRaises(ValueError):validate(6,values,inputs,mesh,[[0,0]],gpu,bad,depth,2)

    def test_gpu_inputs_must_accept_before_material_consumption(self):
        good=[2,4,518,5,520,1,0,16]
        self.assertEqual(read_prepared_pair.validate_inputs(good,2)['accepted'],1)
        batched=good.copy();batched[3]=2
        self.assertEqual(read_prepared_pair.validate_inputs(batched,2)['dispatches'],2)
        self.assertEqual(read_prepared_pair.validate_inputs([0]*8,2)['ranges'],0)
        with self.assertRaises(ValueError):read_prepared_pair.validate_inputs([0]*8,2,required=True)
        for field,value in ((0,3),(1,1),(1,8193),(2,0),(2,521),(3,1),(3,6),(4,521),(4,532),(5,0),(5,2),(6,1),(7,0)):
            bad=good.copy();bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):read_prepared_pair.validate_inputs(bad,2)

    def test_structural_pair_fallback_is_not_shared_success_or_mismatch(self):
        values=[20,10,8192,0,2,2,0,0,250,240,17,4,1000,17,4,0]
        mesh=[1,3,4096,4,0,1,0,16,0,0,1,2]
        depth=[0]*13+[1,1,3]
        self.assertTrue(read_prepared_pair.retry_fallback(6,values,mesh,[[0,1]],depth))
        with self.assertRaises(ValueError):read_prepared_pair.validate_mesh(mesh,[[0,1]],1)
        for field,value in ((3,1),(4,0),(5,1),(6,1),(7,1),(10,20),(15,1)):
            bad=values.copy()
            bad[field]=value
            self.assertFalse(read_prepared_pair.retry_fallback(6,bad,mesh,[[0,1]],depth))
        for rows in ([[0,0]],[[1,0]],[[1,1]]):
            self.assertFalse(read_prepared_pair.retry_fallback(6,values,mesh,rows,depth))

    def test_gpu_mesh_predicates_are_complementary_and_never_cpu_gated(self):
        good=[3,9,4096,10,2,1,0,48,0,0,3,6]
        rows=[[1,0],[0,1],[1,0]]
        self.assertEqual(read_prepared_pair.validate_mesh(good,rows,3)['fallback'],1)
        batched=good.copy();batched[3]=2
        self.assertEqual(read_prepared_pair.validate_mesh(batched,rows,3)['dispatches'],2)
        for field,value in ((0,2),(1,2),(2,0),(3,1),(3,11),(4,0),(4,3),(5,0),(6,1),(7,4096),(8,4096),(9,1),(10,2),(11,0)):
            bad=good.copy()
            bad[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):read_prepared_pair.validate_mesh(bad,rows,3)
        for rows in ([[1,0]]*3,[[1,0],[1,1],[1,0]],[[1,0],[0,0],[1,0]],[[1,0],[0,1]],[[True,0],[0,1],[1,0]]):
            with self.assertRaises(ValueError):read_prepared_pair.validate_mesh(good,rows,3)

    def test_gpu_fallback_preserves_order_and_distinguishes_recorded_from_executed(self):
        maximum=2**64-1
        values=[20,10,8192,1,6,5,0,0,250,240,0,4,1000,17,4,0]
        batch=[3,3,3,2,1,1,4,6,8,1,0,1,4,1,0,1]
        sources=[[10,1],[11,1],[12,1],[20,0],[21,0],[22,0]]
        plan=[[3,20,10,0],[1,21,maximum,maximum],[2,maximum,11,maximum],[3,22,12,1]]
        mesh=[2,6,4096,7,1,1,0,32,0,0,2,4]
        read_prepared_pair.validate_mesh(mesh,[[1,0],[0,1]],2)
        self.assertEqual(validate_sequence(5,values,batch,sources,plan,mesh)['optimizedRecorded'],8)
        for executed in (4,6,8):
            bad=values.copy()
            bad[5]=executed
            with self.assertRaises(ValueError):validate_sequence(5,bad,batch,sources,plan,mesh)
        bad=batch.copy()
        bad[8]=4
        with self.assertRaises(ValueError):validate_sequence(5,values,bad,sources,plan,mesh)

    def test_shared_bank_has_exact_draws_without_hidden_snapshots(self):
        validate_descriptors([100000,66000,100,0],100)
        for values in ([0,66000,100,0],[100000,0,100,0],[100000,66000,99,0],[100000,66000,100,1],[100000,1048577,100,0]):
            with self.assertRaises(ValueError):validate_descriptors(values,100)

    def test_sequence_projects_to_both_original_eye_orders(self):
        maximum=2**64-1
        values=[20,10,8192,1,6,4,0,0,250,240,0,4,1000,17,4,0]
        batch=[3,3,3,2,1,1,4,6,4,0,0,1,4,1,0,1]
        sources=[[10,1],[11,1],[12,1],[20,0],[21,0],[22,0]]
        plan=[[3,20,10,0],[1,21,maximum,maximum],[2,maximum,11,maximum],[3,22,12,1]]
        self.assertEqual(validate_sequence(5,values,batch,sources,plan)['sharedDraws'],2)
        for index,value in ((3,0),(4,5),(5,5),(6,1),(7,1),(10,1),(12,0),(13,0),(15,1)):
            bad=values.copy()
            bad[index]=value
            with self.subTest(stat=index),self.assertRaises(ValueError):
                validate_sequence(5,bad,batch,sources,plan)
        for index,value in ((0,4),(1,4),(2,1),(3,0),(4,0),(5,0),(6,3),(7,5),(8,3),(9,4),(10,4),(11,0),(12,8),(13,2),(14,1),(15,0)):
            bad=batch.copy()
            bad[index]=value
            with self.subTest(batch=index),self.assertRaises(ValueError):
                validate_sequence(5,values,bad,sources,plan)
        for index,column,value in ((0,1,21),(0,2,11),(1,1,20),(1,2,10),(1,3,0),(2,0,3),(3,3,0)):
            bad=[row.copy() for row in plan]
            bad[index][column]=value
            with self.subTest(row=index,column=column),self.assertRaises(ValueError):
                validate_sequence(5,values,batch,sources,bad)
        for bad in (sources[:-1],sources[:-1]+[sources[0]],list(reversed(sources))):
            with self.assertRaises(ValueError):
                validate_sequence(5,values,batch,bad,plan)

    def test_exhaustion_requires_visible_success_and_no_creation_failure(self):
        values=[0]*16
        values[10]=24
        self.assertTrue(exhausted(6,values,9,9,1))
        self.assertFalse(exhausted(6,values,9,9,0))
        self.assertFalse(exhausted(6,values,9,10,1))
        self.assertFalse(exhausted(5,values,9,9,1))
        values[10]=27
        self.assertFalse(exhausted(6,values,9,9,1))
        values[10]=24
        values[15]=1
        self.assertFalse(exhausted(6,values,9,9,1))

    def test_retry_only_completed_empty_comparisons(self):
        empty=[176,0,150000,1,2,1,0,0,0,0,21,4,1000,17,4,0]
        self.assertTrue(retry_empty(6,empty))
        for state in range(6):
            self.assertFalse(retry_empty(state,empty))
        for field,value in ((3,0),(4,0),(5,0),(6,1),(7,1),(10,20),(10,24),(15,1)):
            bad=empty.copy()
            bad[field]=value
            self.assertFalse(retry_empty(6,bad))

    def test_gpu_execution_and_all_color_targets_are_required(self):
        gpu=[300,100,150,0,0,100,80,60,0,0,0,50]*3
        colors=[0,50,50,0,40,40]*2
        depth=[0]*13+[1,1,3]
        validate_gpu(gpu,colors,depth)
        for field in (2,7,11,14,19,23,26,31,35):
            bad=gpu.copy()
            bad[field]=0
            with self.subTest(field=field),self.assertRaises(ValueError):
                validate_gpu(bad,colors,depth)
        for field in (13,14,15):
            bad=depth.copy()
            bad[field]=0
            with self.subTest(field=field),self.assertRaises(ValueError):
                validate_gpu(gpu,colors,bad)
        validate_gpu(gpu,[0]*12,depth)
        for bad in ([0,50,50,0,40,40]+[0]*6,):
            with self.assertRaises(ValueError):
                validate_gpu(gpu,bad,depth)

    def test_cli_uses_the_process_reader_contract(self):
        with mock.patch('sys.argv', ['read_prepared_pair', '--pid', '123', '--game', 'C:/fixture', '--out', 'unused.json']), \
                mock.patch.object(read_prepared_pair, 'resolve', return_value={}), \
                mock.patch.object(read_prepared_pair, 'Reader', autospec=True, side_effect=RuntimeError('constructor-bound')) as reader:
            with self.assertRaisesRegex(RuntimeError, 'constructor-bound'):
                read_prepared_pair.main()
            reader.assert_called_once_with(123, Path('C:/fixture/bin/x64/Cyberpunk2077.exe'), 0, 0)

    def test_only_equal_contributing_actual_draws_pass(self):
        good = [176, 0, 150000, 1, 2, 1, 0, 0, 250, 240, 0, 4, 1000, 17, 4, 0]
        self.assertEqual(validate(5, good)['pairedDraws'], 1)
        for field, value in ((2, 0), (3, 0), (4, 1), (5, 0), (6, 1), (7, 1), (10, 1), (12, 0), (13, 0), (15, 1)):
            bad = good.copy()
            bad[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate(5, bad)
        for state in range(7):
            if state != 5:
                with self.assertRaises(ValueError):
                    validate(state, good)


if __name__ == '__main__':
    unittest.main()
