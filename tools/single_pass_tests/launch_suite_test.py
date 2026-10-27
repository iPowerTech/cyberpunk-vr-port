import io
import struct
import sys
import unittest
from unittest.mock import patch
import run_launch_tests as suite

from run_launch_tests import check_both_eyes, run_logged
from startup_guard import validate_windows
from hud_readiness import StableHud


def bitmap(left, right, top_down=False, bpp=32):
    width, height = 64, 32
    stride = ((width * bpp + 31) // 32) * 4
    result = bytearray(54 + stride * height)
    result[:2] = b'BM'
    struct.pack_into('<I', result, 10, 54)
    struct.pack_into('<IiiHHI', result, 14, 40, width, -height if top_down else height, 1, bpp, 0)
    for y in range(height):
        for x in range(width):
            value = left if x < width // 2 else right
            at = 54 + y * stride + x * (bpp // 8)
            result[at:at + 3] = bytes([value] * 3)
    return result


class LaunchSuiteTests(unittest.TestCase):
    def handoff_fixture(self,schema=1):
        arrays=dict(sources=[],targets=[],states=[])
        for side in range(2):
            arrays['sources'] += [77,side,0x1000+side,3,0x2000,0x3000,side*4,4,side*5,5,159,1,1,0,0x4000,0]
            for offset,slot in enumerate((0,1,2,4)):
                count=2 if slot==4 else 1;seed=16 if slot==4 else 4
                arrays['targets'] += [side,slot,0x5000+side*4+offset,0x6000,0x3000,0x7000,2,0x8000,
                                      offset*4096,4096,1024,768,19,20,2,0,1,1,count,side*5+offset,count,1,0,seed]
                for subresource in range(count):
                    arrays['states'] += [side,side*4+offset,subresource,seed,seed,seed,3,0]
        if schema==2:
            arrays['sourceLocal']=[159,0,0,14,0x9000,9,3,1]*2
            arrays['owners']=[value for index in range(8) for value in (0x70000000+index,0xaaaaaaaaaaaaaaaa)]
            for side in range(2):
                arrays['sources'][side*16+10]=(arrays['sources'][side*16+10]|32)&~8
                arrays['sources'][side*16+15]=14
        return [schema,2,8,10,2,0,5,10,0,2,2,2,0,0,2,0],arrays

    def test_handoff_preserves_raw_and_named_rows_without_permission(self):
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture();report=decode_target_handoff(4,stats,arrays)
        self.assertEqual(report['classification'],'OBSERVATION')
        self.assertEqual(report['raw'],dict(stats=stats,**arrays))
        self.assertEqual(report['populatedSources'],[0,1])
        self.assertEqual(report['sources'][1]['stateFirst'],5)
        self.assertEqual(report['targets'][7]['slot'],4)
        self.assertEqual(report['targets'][7]['stateCount'],2)
        self.assertEqual(report['states'][9]['subresource'],1)
        self.assertEqual(report['stats']['pendingValidated'],2)
        self.assertNotIn('sourceLocal',report);self.assertNotIn('owners',report)
        for key in ('executionPermission','exportPermission','nativeExportPermission','placedHeapOwnershipProved',
                    'prefixNoninterferenceProved','gpuCompletionProved','residencyPermission','ownershipPermission'):
            self.assertIs(report[key],False)

    def test_handoff_refusals_close_failures_and_unknown_states_remain_observations(self):
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture()
        stats[5]=2;stats[12]=1;stats[13]=1;stats[15]=1
        arrays['sources'][10]|=32|64;arrays['sources'][13]=0x80004005;arrays['sources'][15]=17
        arrays['targets'][21]|=32;arrays['targets'][22]=5
        arrays['states'][4]=0;arrays['states'][6]=4
        report=decode_target_handoff(6,stats,arrays)
        self.assertEqual(report['sources'][0]['failure'],17)
        self.assertEqual(report['sources'][0]['closeHRESULT'],0x80004005)
        self.assertEqual(report['targets'][0]['failure'],5)
        self.assertEqual(report['states'][0]['flags'],4)
        self.assertEqual(report['stats']['submitRejected'],1)
        self.assertFalse(report['exportPermission'])

    def test_handoff_accepts_empty_and_partial_failed_begins_in_fixed_side_slots(self):
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture()
        arrays['sources'][:16]=[0]*16;arrays['targets'][:96]=[0]*96;arrays['states'][:40]=[0]*40
        stats[1:4]=[1,4,5]
        self.assertEqual(decode_target_handoff(2,stats,arrays)['populatedSources'],[1])
        arrays['sources'][16+7]=2;arrays['sources'][16+9]=2;arrays['sources'][16+10]|=32
        arrays['sources'][16+15]=3;arrays['targets'][144:]=[0]*48;arrays['states'][56:]=[0]*24
        stats[2:4]=[2,2];stats[5]=1
        self.assertEqual(decode_target_handoff(4,stats,arrays)['stats']['targetCount'],2)
        stats=[1,0,0,0,1,1]+[0]*10
        arrays={name:[0]*len(values) for name,values in arrays.items()}
        self.assertEqual(decode_target_handoff(6,stats,arrays)['populatedSources'],[])

    def test_handoff_rejects_abi_bounds_and_cross_row_reference_errors(self):
        import copy
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture()
        for sequence in (-1,1,2**32,True):
            with self.subTest(sequence=sequence),self.assertRaises(ValueError):decode_target_handoff(sequence,stats,arrays)
        for field,value in ((0,0),(0,3),(1,3),(2,9),(3,11),(1,1),(2,7),(3,9),(5,-1),(5,True)):
            bad=stats.copy();bad[field]=value
            with self.subTest(stat=field,value=value),self.assertRaises(ValueError):decode_target_handoff(4,bad,arrays)
        for name,values in arrays.items():
            for bad in (values[:-1],values+[0],[-1]+values[1:],[2**64]+values[1:],[True]+values[1:]):
                with self.subTest(array=name),self.assertRaises(ValueError):
                    decode_target_handoff(4,stats,dict(arrays,**{name:bad}))
        for name,index,value in (('sources',1,1),('sources',6,1),('sources',7,5),('sources',8,1),('sources',9,6),
                                 ('targets',0,1),('targets',1,4),('targets',19,10),('targets',20,6),
                                 ('targets',24+19,0),('states',0,1),('states',1,1),('states',2,1)):
            bad=copy.deepcopy(arrays);bad[name][index]=value
            with self.subTest(array=name,index=index),self.assertRaises(ValueError):decode_target_handoff(4,stats,bad)

    def test_handoff_reader_retries_odd_or_changed_sequence_with_fixed_reads(self):
        from read_preparation import HANDOFF_ARRAYS,read_target_handoff
        stats,arrays=self.handoff_fixture();prefix='CyberpunkVR_TargetHandoff'
        exports={prefix+'Stats':struct.pack('<16Q',*stats)}
        for name,suffix,fields,limit in HANDOFF_ARRAYS:
            exports[prefix+suffix]=struct.pack(f'<{len(fields)*limit}Q',*arrays[name])
        sequences=iter((1,2,4,4,4));reads=[]
        def read_export(name,size):
            reads.append((name,size))
            value=struct.pack('<I',next(sequences)) if name==prefix+'Seq' else exports[name]
            self.assertEqual(len(value),size)
            return value
        self.assertEqual(read_target_handoff(read_export)['sequence'],4)
        self.assertEqual(max(size for _,size in reads),1536)
        with self.assertRaises(ValueError):
            read_target_handoff(lambda name,size:struct.pack('<I',1) if name==prefix+'Seq' else self.fail('Odd sequence read rows'),attempts=2)
        sequences=iter((2,4,6,8))
        with self.assertRaises(ValueError):read_target_handoff(read_export,attempts=2)
        exports[prefix+'Seq']=struct.pack('<I',4)
        exports[prefix+'Targets']=exports[prefix+'Targets'][:-8]
        with self.assertRaises(ValueError):read_target_handoff(lambda name,size:exports[name])

    def test_handoff_schema2_preserves_local_completion_and_foreign_uncertainty(self):
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture(2);report=decode_target_handoff(4,stats,arrays)
        self.assertEqual(report['raw'],dict(stats=stats,**arrays))
        self.assertEqual(report['sources'][0]['failure'],14)
        self.assertTrue(report['sources'][0]['flags']&32)
        self.assertFalse(report['sources'][0]['flags']&8)
        self.assertEqual(report['sourceLocal'][0],dict(flags=159,failure=0,closeHRESULT=0,foreignFailure=14,
                                                     foreignList=0x9000,foreignKind=9,foreignEvents=3,submittedObserved=1))
        self.assertTrue(report['states'][0]['flags']&2)
        self.assertEqual(report['owners'][7],dict(nativeRecordTag=0x70000007,ownerObservation=0xaaaaaaaaaaaaaaaa))
        self.assertIs(type(report['owners'][7]['ownerObservation']),int)
        for key in ('exportPermission','nativeExportPermission','executionPermission','residencyPermission','ownershipPermission',
                    'placedHeapOwnershipProved','prefixNoninterferenceProved','gpuCompletionProved'):
            self.assertIs(report[key],False)

    def test_handoff_schema2_requires_both_extra_array_abis(self):
        from read_preparation import decode_target_handoff
        stats,arrays=self.handoff_fixture(2)
        for name in ('sourceLocal','owners'):
            missing=dict(arrays);missing.pop(name)
            with self.subTest(missing=name),self.assertRaises(ValueError):decode_target_handoff(4,stats,missing)
            for values in (None,arrays[name][:-1],arrays[name]+[0],[-1]*16,[2**64]*16,[True]*16):
                with self.subTest(array=name,values=values),self.assertRaises(ValueError):
                    decode_target_handoff(4,stats,dict(arrays,**{name:values}))

    def test_handoff_schema2_reader_brackets_mandatory_extra_exports(self):
        from read_preparation import HANDOFF_ARRAYS,HANDOFF_V2_ARRAYS,read_target_handoff
        stats,arrays=self.handoff_fixture(2);prefix='CyberpunkVR_TargetHandoff'
        exports={prefix+'Stats':struct.pack('<16Q',*stats)}
        for name,suffix,fields,limit in HANDOFF_ARRAYS+HANDOFF_V2_ARRAYS:
            exports[prefix+suffix]=struct.pack(f'<{len(fields)*limit}Q',*arrays[name])
        sequences=iter((2,4,4,4));reads=[]
        def read_export(name,size):
            reads.append(name.removeprefix(prefix))
            value=struct.pack('<I',next(sequences)) if name==prefix+'Seq' else exports[name]
            self.assertEqual(len(value),size)
            return value
        report=read_target_handoff(read_export)
        self.assertEqual(report['sequence'],4);self.assertEqual(report['raw']['owners'],arrays['owners'])
        self.assertEqual(reads,['Seq','Stats','Sources','Targets','States','SourceLocal','Owners','Seq']*2)
        exports[prefix+'Seq']=struct.pack('<I',4)
        for suffix in ('SourceLocal','Owners'):
            saved=exports.pop(prefix+suffix)
            with self.subTest(missing=suffix),self.assertRaises(KeyError):read_target_handoff(lambda name,size:exports[name])
            exports[prefix+suffix]=saved[:-8]
            with self.subTest(incomplete=suffix),self.assertRaises(ValueError):read_target_handoff(lambda name,size:exports[name])
            exports[prefix+suffix]=saved

    def frontier_fixture(self):
        maximum=2**64-1
        rows={
            'ranges':[[1,10,7,0xAAA,0x1000,0x2000,1,1,0,0,0,0,0,1,0,1,0,3,0,0,0,0,maximum,0]],
            'slots':[[1,10,0,1,0,1,0x3000,0x4000,0x5000,1,0,9,0x6000,1,0,maximum,maximum,0,0,0x7000]],
            'submits':[[1,4,7,0x8000,1,0,0x4000,2,10,0,9,0,0x3000,0,maximum,maximum]],
        }
        stats=[1,1,0,1,1,1,0,0,1,1,1,0,0,0,0,0]
        return stats,rows

    def frontier_blocks(self,rows):
        return {name:b''.join(struct.pack(f'<{words}Q',*row) for row in rows[name])
                for name,words in (('ranges',24),('slots',20),('submits',16))}

    def test_frontier_keeps_graph_and_code_refusals_as_observations(self):
        from read_preparation import decode_submission_frontier
        stats,rows=self.frontier_fixture()
        report=decode_submission_frontier(4,1,2,0,stats,[0]*16,self.frontier_blocks(rows))
        self.assertEqual(report['ranges'][0][17],3)
        self.assertEqual(report['submits'][0][7],2)
        self.assertEqual(report['stats'][15],0)
        empty={name:b'' for name in rows}
        report=decode_submission_frontier(0,2,3,11,[0]*16,[0]*16,empty)
        self.assertEqual((report['installed'],report['state'],report['failure']),(2,3,11))
        self.assertFalse(report['ranges'])
        decode_submission_frontier(0,0,0,0,[0]*16,[0]*16,empty)
        rows['ranges'][0][15]=0
        decode_submission_frontier(4,1,1,0,stats,[0]*16,self.frontier_blocks(rows))

    def test_frontier_rejects_permission_bounds_and_incoherent_rows(self):
        from read_preparation import decode_submission_frontier
        stats,rows=self.frontier_fixture()
        for field in (1,3,4,5,6,7,9,10,11,12,13,15):
            bad=stats.copy();bad[field]+=1
            with self.subTest(stat=field),self.assertRaises(ValueError):
                decode_submission_frontier(4,1,2,0,bad,[0]*16,self.frontier_blocks(rows))
        for sequence,installed,state,failure in ((3,1,2,0),(2**32,1,2,0),(4,4,2,0),(4,1,5,0),(4,1,2,15),(True,1,2,0)):
            with self.subTest(sequence=sequence,installed=installed,state=state,failure=failure),self.assertRaises(ValueError):
                decode_submission_frontier(sequence,installed,state,failure,stats,[0]*16,self.frontier_blocks(rows))
        for name,field,value in (('ranges',12,1),('ranges',13,257),('ranges',17,9),('slots',2,1),('slots',3,2),
                                 ('slots',11,10),('slots',13,0),('submits',4,257),('submits',5,1),('submits',7,7),
                                 ('submits',9,1),('submits',10,10),('submits',12,0)):
            _,bad=self.frontier_fixture();bad[name][0][field]=value
            with self.subTest(name=name,field=field),self.assertRaises(ValueError):
                decode_submission_frontier(4,1,2,0,stats,[0]*16,self.frontier_blocks(bad))
        for name,words,limit in (('ranges',24,64),('slots',20,4096),('submits',16,4096)):
            for raw in (b'\0',bytes((limit+1)*words*8)):
                blocks=self.frontier_blocks(rows);blocks[name]=raw
                with self.subTest(name=name,size=len(raw)),self.assertRaises(ValueError):
                    decode_submission_frontier(4,1,2,0,stats,[0]*16,blocks)

    def test_frontier_reader_retries_sequence_and_bounds_memory_reads(self):
        from read_preparation import FRONTIER_ROWS,read_submission_frontier
        stats,rows=self.frontier_fixture();blocks=self.frontier_blocks(rows)
        prefix='CyberpunkVR_SubmissionFrontier'
        exports={prefix+'Installed':struct.pack('<I',1),prefix+'State':struct.pack('<I',2),prefix+'Failure':struct.pack('<I',0),
                 prefix+'Stats':struct.pack('<16Q',*stats),prefix+'Details':bytes(128)}
        memory={}
        for index,(name,singular,plural,words,_) in enumerate(FRONTIER_ROWS):
            exports[prefix+singular+'Count']=struct.pack('<I',1)
            exports[prefix+singular+'Bytes']=struct.pack('<I',words*8)
            exports[prefix+plural]=struct.pack('<Q',0x10000+index*0x1000)
            memory[0x10000+index*0x1000]=blocks[name]
        sequences=iter((1,2,4,4,4))
        def read_export(name,size):
            value=struct.pack('<I',next(sequences)) if name==prefix+'Seq' else exports[name]
            self.assertEqual(len(value),size)
            return value
        def read_memory(address,size):
            self.assertEqual(len(memory[address]),size)
            return memory[address]
        self.assertEqual(read_submission_frontier(read_export,read_memory)['sequence'],4)
        with self.assertRaises(ValueError):
            read_submission_frontier(lambda *_:struct.pack('<I',1),lambda *_:self.fail('Odd sequence read rows'),attempts=2)
        exports[prefix+'Seq']=struct.pack('<I',4)
        for suffix,data in (('RangeCount',struct.pack('<I',65)),('RangeBytes',struct.pack('<I',191)),
                            ('Ranges',struct.pack('<Q',0)),('Ranges',struct.pack('<Q',2**64-100))):
            saved=exports[prefix+suffix];exports[prefix+suffix]=data
            with self.subTest(suffix=suffix),self.assertRaises(ValueError):
                read_submission_frontier(lambda name,size:exports[name],lambda *_:self.fail('Unbounded row read'))
            exports[prefix+suffix]=saved

    def test_current_mode_requires_full_source_inputs_before_launch(self):
        class Parsed(Exception):pass
        base=['run_launch_tests.py','--out','unused','--owned-coupled','--retained-geometry','reference','--pass-targets',
              '--retained-source-stream','--retained-source-cbvs','--retained-source-tables','--retained-source-details',
              '--retained-cbv-snapshots','--retained-ia-snapshots','--source-selector-manifest','manifest.json','--prepared-current']
        for extra in ([],['--prepared-infix'],['--prepared-infix','--prepared-tail'],['--prepared-infix','--prepared-tail','--allow-vrs-refinement'],
                      ['--prepared-infix','--prepared-tail','--full-t5']):
            with self.subTest(extra=extra),patch.object(sys,'argv',base+extra),patch.object(suite.Path,'is_file',return_value=True),patch.object(suite.Path,'mkdir',side_effect=Parsed):
                with self.assertRaises(Parsed):suite.main()
        cases=[base+['--prepared-pair'],base+['--owned-retirement'],base+['--retained-stage','6'],base+['--prepared-tail'],base+['--allow-vrs-refinement']]
        for flag in ('--prepared-current','--owned-coupled'):
            bad=base+['--prepared-infix'];bad.remove(flag)
            if flag=='--owned-coupled':bad.append('--whole-gbuffer-plan')
            cases.append(bad)
        for flag in ('--retained-ia-snapshots','--retained-cbv-snapshots'):
            bad=base.copy();bad.remove(flag);cases.append(bad)
        for argv in cases:
            with self.subTest(argv=argv),patch.object(sys,'argv',argv),patch('sys.stderr',io.StringIO()),\
                    patch.object(suite.Path,'is_file',return_value=True),patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_current_capture_reader_prerequisites_and_exports(self):
        import read_preparation
        import live_symbols
        class Parsed(Exception):pass
        base=['--pid','1','--out','unused','--owned-coupled','--retained-geometry','reference','--pass-targets',
              '--merge-root','--schedule-preparation','apply','--retained-source-stream','--retained-source-cbvs',
              '--retained-source-tables','--retained-source-details','--retained-cbv-snapshots',
              '--retained-source-selectors','--retained-ia-snapshots','--prepared-current']
        for extra in ([],['--prepared-infix'],['--prepared-infix','--prepared-tail'],['--prepared-infix','--prepared-tail','--allow-vrs-refinement']):
            with self.subTest(extra=extra),patch.object(live_symbols,'resolve',side_effect=Parsed) as resolve:
                with self.assertRaises(Parsed):read_preparation.main(base+extra)
                for name in ('CyberpunkVR_PreparedCurrent*','CyberpunkVR_RetainedCurrent*','CyberpunkVR_SubmissionFrontier*',
                             'CyberpunkVR_PreparedInfixEnable','CyberpunkVR_RetainedInfix','CyberpunkVR_PreparedTailEnable','CyberpunkVR_RetainedTail','CyberpunkVR_PreparedVrsRefinementEnable','CyberpunkVR_RetainedFullT5*'):
                    self.assertIn(name,resolve.call_args.args[2])
        for flag in ('--retained-ia-snapshots','--retained-source-selectors','--prepared-current','--owned-coupled'):
            bad=base+['--prepared-infix'];bad.remove(flag)
            if flag=='--owned-coupled':bad.append('--whole-gbuffer-plan')
            with self.subTest(flag=flag),patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
                with self.assertRaises(SystemExit) as result:read_preparation.main(bad)
                self.assertEqual(result.exception.code,2);resolve.assert_not_called()
        with patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
            with self.assertRaises(SystemExit) as result:read_preparation.main(base+['--prepared-tail'])
            self.assertEqual(result.exception.code,2);resolve.assert_not_called()
        with patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
            with self.assertRaises(SystemExit) as result:read_preparation.main(base+['--allow-vrs-refinement'])
            self.assertEqual(result.exception.code,2);resolve.assert_not_called()

    def test_full_t5_requires_current_infix_tail_before_process_or_launch_access(self):
        import read_preparation
        import live_symbols
        flags=['--prepared-current','--prepared-infix','--prepared-tail','--full-t5']
        for missing in flags[:-1]:
            extra=[flag for flag in flags if flag!=missing]
            with self.subTest(missing=missing),patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
                with self.assertRaises(SystemExit) as result:read_preparation.main(['--pid','1','--out','unused',*extra])
                self.assertEqual(result.exception.code,2);resolve.assert_not_called()
            with self.subTest(launch_missing=missing),patch('sys.stderr',io.StringIO()),\
                    patch.object(sys,'argv',['run_launch_tests.py','--out','unused',*extra]),patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_full_t5_stays_on_preparation_not_the_read_only_observer(self):
        from full_t5_report_test import fixture
        from full_t5_report import validate,validate_capture
        stats,rows,source,tail=fixture()
        capture=dict(fullT5Requested=True,fullT5Available=True,sourceStream=source,preparedTail=tail,
                     fullT5=validate(stats,rows,requested=True,source=source,tail=tail))
        for rejected in (False,True):
            self.assertNotIn('--full-t5',suite.current_observer_flags(capture,rejected=rejected))
            self.assertTrue(validate_capture(capture,True)['verified'])
        with self.assertRaises(ValueError):validate_capture(capture,False)

    def test_borrowed_tables_require_full_t5_and_current_tail_before_process_access(self):
        import read_preparation
        import live_symbols
        flags=['--prepared-current','--prepared-infix','--prepared-tail','--full-t5','--borrowed-tables']
        for missing in flags[:-1]:
            extra=[flag for flag in flags if flag!=missing]
            with self.subTest(missing=missing),patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
                with self.assertRaises(SystemExit) as result:read_preparation.main(['--pid','1','--out','unused',*extra])
                self.assertEqual(result.exception.code,2);resolve.assert_not_called()
            with self.subTest(launch_missing=missing),patch('sys.stderr',io.StringIO()),\
                    patch.object(sys,'argv',['run_launch_tests.py','--out','unused',*extra]),patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_borrowed_tables_remain_preparation_only_and_do_not_grant_material_permission(self):
        from borrowed_tables_report_test import fixture
        from borrowed_tables_report import validate,validate_capture
        value,source,tail=fixture()
        capture=dict(borrowedTablesRequested=True,borrowedTablesAvailable=True,sourceStream=source,preparedTail=tail,
            borrowedTables=validate(value,requested=True,source=source,tail=tail))
        for rejected in (False,True):
            self.assertNotIn('--borrowed-tables',suite.current_observer_flags(capture,rejected=rejected))
            verified=validate_capture(capture,True)
            self.assertTrue(verified['verified']);self.assertFalse(verified['resourceReadPermission'])
            self.assertFalse(verified['fullDynamicBankValid'])
        with self.assertRaises(ValueError):validate_capture(capture,False)

    def test_fixed_assets_require_all_borrowed_source_prerequisites_before_process_access(self):
        import read_preparation
        import live_symbols
        flags=['--prepared-current','--prepared-infix','--prepared-tail','--full-t5','--borrowed-tables','--fixed-assets']
        for missing in flags[:-1]:
            extra=[flag for flag in flags if flag!=missing]
            with self.subTest(missing=missing),patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
                with self.assertRaises(SystemExit) as result:read_preparation.main(['--pid','1','--out','unused',*extra])
                self.assertEqual(result.exception.code,2);resolve.assert_not_called()
            with self.subTest(launch_missing=missing),patch('sys.stderr',io.StringIO()),\
                    patch.object(sys,'argv',['run_launch_tests.py','--out','unused',*extra]),patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_fixed_assets_are_preparation_observation_without_read_or_residency_permission(self):
        from fixed_assets_report_test import fixture
        from fixed_assets_report import validate,validate_capture
        value,source,tail,borrowed=fixture()
        capture=dict(fixedAssetsRequested=True,fixedAssetsAvailable=True,sourceStream=source,preparedTail=tail,
            borrowedTables=borrowed,fixedAssets=validate(value,requested=True,source=source,tail=tail,borrowed=borrowed))
        for rejected in (False,True):
            self.assertNotIn('--fixed-assets',suite.current_observer_flags(capture,rejected=rejected))
            checked=validate_capture(capture,True)
            self.assertTrue(checked['bootstrapExercised']);self.assertFalse(checked['allSlotsQualified'])
            self.assertFalse(checked['resourceReadPermission']);self.assertFalse(checked['residencyPermission'])
        with self.assertRaises(ValueError):validate_capture(capture,False)

    def infix_fixture(self):
        from read_prepared_pair import validate_current_source
        values=[1,77,1,4,1,3,7,3,90,123,5,678,4,1,3,0]
        source=validate_current_source([1,77,2,1,0,0,0,0,0,1,1,1,123,4,5,0],77,2)
        return values,source

    def test_infix_requires_complete_source_prefix_consumer_and_native_suffix(self):
        from read_preparation import validate_prepared_infix
        values,source=self.infix_fixture()
        result=validate_prepared_infix(values,77,source,2)
        self.assertEqual((result['frame'],result['boundary'],result['suffixExecuted']),(77,4,3))
        for main,peer,boundary,count in ((3,1,4,7),(1,0,2,3),(253,254,255,256)):
            good=values.copy();good[3:8]=[boundary,main,peer,count,count-boundary]
            good[12]=boundary;good[14]=count-boundary
            with self.subTest(main=main,peer=peer,count=count):validate_prepared_infix(good,77,source,2)
        for index,value in ((0,0),(1,78),(2,0),(2,2),(3,0),(3,3),(3,7),(4,3),(4,4),(5,2),
                            (6,4),(6,257),(7,2),(8,0),(9,0),(10,0),(11,0),(12,3),(13,0),(13,2),(14,2),(15,1)):
            bad=values.copy();bad[index]=value
            with self.subTest(index=index,value=value),self.assertRaises(ValueError):validate_prepared_infix(bad,77,source,2)
        for key,value in (('frame',78),('packets',1),('submitted',0),('mainSubmitted',0),('peerSubmitted',0),
                          ('payloadsProcessed',1),('error',1)):
            with self.subTest(source=key),self.assertRaises(ValueError):
                validate_prepared_infix(values,77,dict(source,**{key:value}),2)
        for frame,packets in ((78,2),(0,2),(True,2),(77,1),(77,0),(77,True)):
            with self.subTest(frame=frame,packets=packets),self.assertRaises(ValueError):
                validate_prepared_infix(values,frame,source,packets)
        with self.assertRaises(ValueError):validate_prepared_infix(values,77,None,2)

    def test_infix_failure_telemetry_remains_decodable_without_proving_success(self):
        from read_preparation import decode_prepared_infix,validate_prepared_infix
        values,source=self.infix_fixture();values[13:]=[0,0,8]
        result=decode_prepared_infix(values)
        self.assertEqual((result['consumerSubmitted'],result['suffixExecuted'],result['error']),(0,0,8))
        self.assertIn('does not replace native output or suppress native lists',result['limitation'])
        with self.assertRaises(ValueError):validate_prepared_infix(values,77,source,2)
        for bad in (None,{},values[:-1],values+[0]):
            with self.subTest(bad=bad),self.assertRaises(ValueError):decode_prepared_infix(bad)
        for index in range(16):
            for value in (-1,2**64,True,1.0):
                bad=values.copy();bad[index]=value
                with self.subTest(index=index,value=value),self.assertRaises(ValueError):decode_prepared_infix(bad)

    def tail_fixture(self):
        from staged_sources_report import analyze
        rows=[];staged=[]
        for ordinal,(eye,stage) in enumerate(((0,7),(1,7),(0,9),(1,8),(0,9),(1,9),(0,9),(1,9))):
            row=[0]*48;row[:5]=[ordinal,eye,0,77,stage];row[6:13]=[1]*7;row[43]=31;rows.append(row)
            staged.append([ordinal,eye,1,0,1,1,ordinal+1,0])
        source=dict(stats=[3,77,8,8,0,4,4,8,8,8,8,8,0,0,0,0],rows=rows,commands=8,direct=8,indirect=0)
        staged=analyze([2,77,8,8,0,0,8,0,0,0,8,8],b''.join(struct.pack('<8Q',*row) for row in staged),source,complete=True)
        values=[1,77,2,6,3,1,4096,9,1,0,1,77,5,7,2,1,4096,9,1,0]
        return values,source,staged

    def test_tail_proves_maximal_per_eye_suffix_despite_interleaved_ordinals(self):
        import copy
        from read_preparation import validate_prepared_tail,decode_prepared_tail
        values,source,staged=self.tail_fixture()
        tail=validate_prepared_tail(values,source,staged)
        self.assertEqual(tail['sourceCount'],5);self.assertEqual(staged['ready'],8)
        self.assertEqual(tail['sources'],[[2,1],[4,1],[5,0],[6,1],[7,0]])
        self.assertEqual(tail['nativeSources'],[[2,0],[4,0],[5,1],[6,0],[7,1]])
        self.assertEqual([eye['ordinals'] for eye in tail['eyes']],[[2,4,6],[5,7]])
        for field,value in ((0,0),(1,78),(2,4),(3,4),(4,0),(5,0),(6,0),(6,64*1024*1024+1),(7,8),(8,0),(9,1)):
            for eye in range(2):
                bad=values.copy();bad[eye*10+field]=value
                with self.subTest(eye=eye,field=field),self.assertRaises(ValueError):validate_prepared_tail(bad,source,staged)
        for ordinal in (4,6,7):
            bad=copy.deepcopy(source);bad['rows'][ordinal][4]=10
            with self.subTest(trailing_or_interior=ordinal),self.assertRaises(ValueError):validate_prepared_tail(values,bad,staged)
        bad=copy.deepcopy(source);bad['rows'][4][2]=1;bad['stats'][3:5]=[7,1];bad.update(direct=7,indirect=1)
        with self.assertRaises(ValueError):validate_prepared_tail(values,bad,staged)
        bad=copy.deepcopy(staged);bad['rows'][4][2:4]=[0,1];bad['stats'][3:5]=[7,1];bad['stats'][6]=7;bad['stats'][10:]=[7,7]
        with self.assertRaises(ValueError):validate_prepared_tail(values,source,bad)
        bad=copy.deepcopy(staged);bad['stats'][10]=7
        with self.assertRaises(ValueError):validate_prepared_tail(values,source,bad)
        bad=copy.deepcopy(source);bad['rows'].pop()
        with self.assertRaises(ValueError):validate_prepared_tail(values,bad,staged)
        bad=values.copy();bad[9]=5
        self.assertEqual(decode_prepared_tail(bad)['eyes'][0]['error'],5)
        for bad in (None,[],values[:-1],values+[0],[True]*20,[-1]*20,[2**64]*20):
            with self.subTest(abi=bad),self.assertRaises(ValueError):decode_prepared_tail(bad)

    def test_current_launch_saves_initial_then_repeat_turn_and_fresh_rejection(self):
        plan=['retained_reference','repeat_owned_reference','prepared_sequence','prepared_input_rejection',
              'retained_source_bank','turned_prepared_sequence','both_eye_images']
        suite.insert_current_checks(plan)
        self.assertEqual(plan,['retained_reference','prepared_current_initial','repeat_owned_reference','prepared_sequence',
                               'retained_source_bank','turned_prepared_sequence','prepared_input_rejection','both_eye_images'])
        capture={'sourceStream':{'stats':[3,77]+[0]*14}}
        self.assertEqual(suite.current_observer_flags(capture),['--observe-current','--sequence','--source-frame','77'])
        self.assertEqual(suite.current_observer_flags(capture,rejected=True)[-1],'--reject-inputs')
        with self.assertRaises(ValueError):suite.current_observer_flags({})

    def test_current_input_fault_is_set_before_capture_and_always_restored(self):
        for failure in (None,'capture','observe'):
            events=[]
            def capture():
                events.append('capture')
                if failure=='capture':raise RuntimeError('capture failed')
                return {'frame':77}
            def observe(source):
                events.append(('observe',source['frame']))
                if failure=='observe':raise RuntimeError('observe failed')
                return {'rejected':True}
            with self.subTest(failure=failure):
                action=lambda:suite.run_current_rejection(lambda:0,lambda value:events.append(('fault',value)),capture,observe)
                if failure:
                    with self.assertRaises(RuntimeError):action()
                else:
                    self.assertEqual(action(),({'frame':77},{'rejected':True}))
                self.assertEqual(events[:2],[('fault',1),'capture'])
                self.assertEqual(events[-1],('fault',0))

    def test_current_result_matches_exact_capture_and_all_staged_packets(self):
        import copy
        from read_prepared_pair import validate_current,validate_current_source
        capture=dict(passed=True,pid=1,dll='build',sourceStream=dict(stats=[3,77]+[0]*14),stagedSources=dict(ready=2),
                     preparedCurrentSource=validate_current_source([1,77,2,1,0,0,0,0,0,1,1,1,123,4,5,0],77,2))
        comparison=dict(passed=True,pid=1,dll='build',observedCurrent=True,cleanup='observation-only',sources=[[0,0],[1,1]],
                        current=validate_current([1,77,77,77,1,1,1,1,2,0,0,0,0,0,0,0],2,77))
        suite.validate_current_capture(capture,comparison)
        for key,value in (('pid',2),('dll','other'),('passed',False),('observedCurrent',False),('cleanup','restored')):
            bad=copy.deepcopy(comparison);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):suite.validate_current_capture(capture,bad)
        bad=copy.deepcopy(comparison);bad['current']['sourceFrame']=78
        with self.assertRaises(ValueError):suite.validate_current_capture(capture,bad)
        bad=copy.deepcopy(comparison);bad['sources']=bad['sources'][:1];bad['current']['sourceCount']=1
        with self.assertRaises(ValueError):suite.validate_current_capture(capture,bad)
        bad=copy.deepcopy(capture);bad['preparedCurrentSource']['payloadsProcessed']=1
        with self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison)
        rejected=copy.deepcopy(comparison);rejected['current']['error']=29;rejected['inputRejectionVerified']=True
        suite.validate_current_capture(capture,rejected,rejected=True)
        rejected['inputRejectionVerified']=False
        with self.assertRaises(ValueError):suite.validate_current_capture(capture,rejected,rejected=True)

    def test_infix_current_observation_requires_the_exact_completed_capture(self):
        import copy
        from read_preparation import decode_prepared_infix
        from read_prepared_pair import validate_current
        values,source=self.infix_fixture()
        capture=dict(passed=True,pid=1,dll='build',sourceStream=dict(stats=[3,77]+[0]*14),stagedSources=dict(ready=2),
                     preparedCurrentSource=source,preparedInfixRequested=True,preparedInfix=decode_prepared_infix(values))
        comparison=dict(passed=True,pid=1,dll='build',observedCurrent=True,cleanup='observation-only',sources=[[0,0],[1,1]],
                        current=validate_current([1,77,77,77,1,1,1,1,2,0,0,0,0,0,0,0],2,77))
        suite.validate_current_capture(capture,comparison,infix=True)
        for field in ('preparedInfix','preparedCurrentSource'):
            bad=copy.deepcopy(capture);del bad[field]
            with self.subTest(missing=field),self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison,infix=True)
        for key,value in (('frame',78),('consumerSubmitted',0),('error',1)):
            bad=copy.deepcopy(capture);bad['preparedInfix'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison)
        bad=copy.deepcopy(capture);del bad['preparedInfix'];del bad['preparedInfixRequested']
        with self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison,infix=True)
        rejected=copy.deepcopy(comparison);rejected['current']['error']=29;rejected['inputRejectionVerified']=True
        suite.validate_current_capture(capture,rejected,rejected=True,infix=True)

    def test_tail_current_observer_matches_exact_filtered_sources_and_native_exit(self):
        import copy
        from read_preparation import validate_prepared_tail,decode_prepared_infix
        from read_prepared_pair import validate_current,validate_current_source,BASELINE_FIELDS,validate_native_exit
        from read_prepared_pair import NATIVE_RATE_FIELDS,VRS_ATTRIBUTION_FIELDS,NATIVE_ACCEPTANCE_FIELDS,FIELDS
        values,source,staged=self.tail_fixture();tail=validate_prepared_tail(values,source,staged)
        infix,_=self.infix_fixture()
        current_source=validate_current_source([1,77,5,1,0,0,0,0,0,1,1,1,123,4,5,0],77,5)
        capture=dict(passed=True,pid=1,dll='build',sourceStream=source,stagedSources=staged,preparedTailRequested=True,
                     preparedTail=tail,preparedCurrentSource=current_source,preparedInfix=decode_prepared_infix(infix))
        baseline=[2,3,4,10,34000,0]+[0]*18
        native=[1,2,10,34000]+[0]*16+[1,1,0,1]
        comparison=dict(passed=True,pid=1,dll='build',observedCurrent=True,cleanup='observation-only',sources=[[2,1],[4,1],[5,0],[6,1],[7,0]],
                        current=validate_current([1,77,77,77,1,1,1,1,5,0,0,0,0,0,0,0],5,77),
                        baseline=dict(zip(BASELINE_FIELDS,baseline)),nativeExit=validate_native_exit(native,baseline,required=True),
                        nativeRate=dict(zip(NATIVE_RATE_FIELDS,[1,2,5,10,34000]+[0]*8+[1,1,0])),
                        vrsAttribution=dict(zip(VRS_ATTRIBUTION_FIELDS,[16,10,10,1,1,0,0,0,0,1,1,1])),
                        nativeAcceptance=dict(zip(NATIVE_ACCEPTANCE_FIELDS,[0,1,1,0,0,0,0,1])),stats=dict(zip(FIELDS,[0]*16)))
        suite.validate_current_capture(capture,comparison,tail=True)
        self.assertIn('--native-tail',suite.current_observer_flags(capture))
        self.assertIn('--native-tail',suite.current_observer_flags(dict(sourceStream=source),tail=True))
        for sources in (tail['nativeSources'],tail['sources'][:-1],tail['sources'][1:]+[[0,0]],list(reversed(tail['sources'])),[[2,0]]*5):
            bad=copy.deepcopy(comparison);bad['sources']=sources
            with self.subTest(sources=sources),self.assertRaises(ValueError):suite.validate_current_capture(capture,bad,tail=True)
        for field in ('preparedTail','preparedInfix'):
            bad=copy.deepcopy(capture);del bad[field]
            with self.subTest(missing=field),self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison,tail=True)
        bad=copy.deepcopy(comparison);del bad['nativeExit']
        with self.assertRaises(ValueError):suite.validate_current_capture(capture,bad,tail=True)
        bad=copy.deepcopy(capture);bad['preparedCurrentSource']['packets']=staged['ready']
        with self.assertRaises(ValueError):suite.validate_current_capture(bad,comparison,tail=True)
        rejected=copy.deepcopy(comparison);rejected['current']['error']=29;rejected['inputRejectionVerified']=True
        rejected['nativeExit']['inputRejected']=1
        rejected['nativeRate'].update(inputRejected=1,equivalent=0)
        rejected['nativeAcceptance'].update(nativeRateEquivalent=0,accepted=0)
        suite.validate_current_capture(capture,rejected,rejected=True,tail=True)
        approved_capture=copy.deepcopy(capture);approved_capture['allowVrsRefinement']=True
        approved=copy.deepcopy(comparison);approved['allowVrsRefinement']=True;approved['nativeAcceptance']['policy']=1
        suite.validate_current_capture(approved_capture,approved,tail=True,allow_vrs_refinement=True)
        self.assertIn('--allow-vrs-refinement',suite.current_observer_flags(approved_capture))
        for left,right,allow in ((capture,approved,True),(approved_capture,comparison,True),(approved_capture,approved,False)):
            with self.assertRaises(ValueError):suite.validate_current_capture(left,right,tail=True,allow_vrs_refinement=allow)

        from native_export_report import FIELDS as EXPORT_FIELDS
        exported_capture=copy.deepcopy(approved_capture);exported=copy.deepcopy(approved)
        export_values=[1,1,1,1,10,8192,0,0,1,0,0,0,77,0x90000,8,1,1,1,1,0,1,1,0,0]
        for item in (exported_capture,exported):
            item.update(nativeExportRequested=True,nativeExportAvailable=True,nativeExportRaw=export_values.copy(),
                        nativeExport=dict(zip(EXPORT_FIELDS,export_values)))
        exported['nativeExportVerified']=True
        suite.validate_current_capture(exported_capture,exported,tail=True,allow_vrs_refinement=True,native_export=True)
        self.assertIn('--native-export',suite.current_observer_flags(exported_capture))
        self.assertIn('--native-export',suite.current_observer_flags(capture,native_export=True))
        for key,value in (('nativeExportRequested',False),('nativeExportVerified',False),('nativeExportRaw',None)):
            bad=copy.deepcopy(exported);bad[key]=value
            with self.subTest(exportField=key),self.assertRaises(ValueError):
                suite.validate_current_capture(exported_capture,bad,tail=True,allow_vrs_refinement=True,native_export=True)
        for index,value in ((1,0),(3,0),(6,1),(11,1),(12,78),(18,0),(20,0),(21,0),(22,1),(23,1)):
            bad=copy.deepcopy(exported);bad['nativeExportRaw'][index]=value
            bad['nativeExport']=dict(zip(EXPORT_FIELDS,bad['nativeExportRaw']))
            with self.subTest(exportIndex=index),self.assertRaises(ValueError):
                suite.validate_current_capture(exported_capture,bad,tail=True,allow_vrs_refinement=True,native_export=True)
        bad_capture=copy.deepcopy(exported_capture);bad_capture['nativeExportRaw'][12]=78
        bad_capture['nativeExport']=dict(zip(EXPORT_FIELDS,bad_capture['nativeExportRaw']))
        with self.assertRaises(ValueError):
            suite.validate_current_capture(bad_capture,exported,tail=True,allow_vrs_refinement=True,native_export=True)
        with self.assertRaises(ValueError):
            suite.validate_current_capture(exported_capture,exported,tail=True,allow_vrs_refinement=True)
        rejected_export=copy.deepcopy(exported)
        rejected_export['current']['error']=29;rejected_export['inputRejectionVerified']=True
        rejected_export['nativeExit']['inputRejected']=1
        rejected_export['nativeRate'].update(inputRejected=1,equivalent=0)
        rejected_export['nativeAcceptance'].update(nativeRateEquivalent=0,accepted=0)
        rejected_export['nativeExportRaw'][9:11]=[1,1]
        rejected_export['nativeExport']=dict(zip(EXPORT_FIELDS,rejected_export['nativeExportRaw']))
        suite.validate_current_capture(exported_capture,rejected_export,rejected=True,tail=True,allow_vrs_refinement=True,native_export=True)
        disabled_capture=copy.deepcopy(approved_capture);disabled=copy.deepcopy(approved)
        idle=[0]*24;idle[12]=77
        for item in (disabled_capture,disabled):
            item.update(nativeExportRequested=False,nativeExportAvailable=True,nativeExportRaw=idle.copy(),nativeExport=dict(zip(EXPORT_FIELDS,idle)))
        suite.validate_current_capture(disabled_capture,disabled,tail=True,allow_vrs_refinement=True)
        disabled['nativeExportRaw'][1]=1;disabled['nativeExport']['prepared']=1
        with self.assertRaises(ValueError):suite.validate_current_capture(disabled_capture,disabled,tail=True,allow_vrs_refinement=True)

    def test_coupled_source_capture_reaches_only_workspace_boundary(self):
        class Parsed(Exception):pass
        base=['run_launch_tests.py','--out','unused','--owned-coupled','--retained-geometry','reference','--pass-targets','--retained-source-stream']
        with patch.object(sys,'argv',base),patch.object(suite.Path,'mkdir',side_effect=Parsed):
            with self.assertRaises(Parsed):suite.main()
        for extra in (['--whole-gbuffer-plan'],['--owned-retirement'],['--retained-stage','6']):
            with self.subTest(extra=extra),patch.object(sys,'argv',base+extra),patch('sys.stderr',io.StringIO()),patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_coupled_source_reader_does_not_enable_a_legacy_graph_plan(self):
        import read_preparation
        import live_symbols
        class Parsed(Exception):pass
        base=['--pid','1','--out','unused','--owned-coupled','--retained-geometry','reference','--pass-targets',
              '--merge-root','--schedule-preparation','apply','--retained-source-stream']
        with patch.object(live_symbols,'resolve',side_effect=Parsed) as resolve:
            with self.assertRaises(Parsed):read_preparation.main(base)
            self.assertIn('CyberpunkVR_RetainedSourceBoundary*',resolve.call_args.args[2])
        for extra in (['--whole-gbuffer-plan'],['--owned-graph-sort'],['--retained-stage','6']):
            with self.subTest(extra=extra),patch('sys.stderr',io.StringIO()),patch.object(live_symbols,'resolve') as resolve:
                with self.assertRaises(SystemExit) as result:read_preparation.main(base+extra)
                self.assertEqual(result.exception.code,2);resolve.assert_not_called()

    def test_prepared_native_modes_require_owned_inputs_before_launch(self):
        for flag in ('--prepared-pair','--prepared-sequence','--prepared-sequence-turn','--prepared-current','--prepared-infix','--prepared-tail','--allow-vrs-refinement','--native-export','--owned-coupled'):
            argv=['run_launch_tests.py','--out','unused',flag]
            with self.subTest(flag=flag),patch.object(sys,'argv',argv),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir,\
                 patch.object(suite.boot,'process_ids',side_effect=AssertionError('Unowned comparison reached process access')):
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2)
                mkdir.assert_not_called()

    def test_whole_gbuffer_invalid_options_do_not_start_work(self):
        base=['run_launch_tests.py','--out','unused','--whole-gbuffer-plan']
        for flags in ([],['--retained-geometry','replace','--pass-targets'],
                      ['--retained-geometry','reference','--retained-stage','6','--pass-targets'],
                      ['--retained-geometry','reference','--pass-targets','--owned-graph-sort']):
            with self.subTest(flags=flags),patch.object(sys,'argv',base+flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_whole_gbuffer_options_reach_only_workspace_boundary(self):
        class Parsed(Exception):pass
        argv=['run_launch_tests.py','--out','unused','--whole-gbuffer-plan','--retained-geometry','reference','--pass-targets']
        with patch.object(sys,'argv',argv),patch.object(suite.Path,'mkdir',side_effect=Parsed):
            with self.assertRaises(Parsed):suite.main()

    def test_owned_retirement_invalid_launch_does_not_install_or_start(self):
        base=['run_launch_tests.py','--out','unused','--owned-retirement']
        for flags in ([],['--retained-geometry','replace','--pass-targets'],
                      ['--retained-geometry','reference','--pass-targets','--owned-graph-sort']):
            with self.subTest(flags=flags),patch.object(sys,'argv',base+flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir,\
                 patch.object(suite.boot,'process_ids',side_effect=AssertionError('Invalid retirement test reached process access')):
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_retirement_cycles_require_owned_retirement_before_launch(self):
        argv=['run_launch_tests.py','--out','unused','--retirement-cycles','4']
        with patch.object(sys,'argv',argv),patch('sys.stderr',io.StringIO()),patch.object(suite.Path,'mkdir') as mkdir:
            with self.assertRaises(SystemExit) as result:suite.main()
            self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_owned_retirement_reaches_only_workspace_boundary(self):
        class Parsed(Exception):pass
        argv=['run_launch_tests.py','--out','unused','--owned-retirement','--retained-geometry','reference','--pass-targets']
        with patch.object(sys,'argv',argv),patch.object(suite.Path,'mkdir',side_effect=Parsed),\
             patch.object(suite.boot,'process_ids',side_effect=AssertionError('Test reached process access')):
            with self.assertRaises(Parsed):suite.main()

    def test_owned_reference_invalid_launch_options_do_not_start_work(self):
        base=['run_launch_tests.py','--out','unused','--owned-reference']
        for flags in ([],['--retained-geometry','reference'],['--retained-geometry','replace','--pass-targets'],
                      ['--retained-geometry','reference','--pass-targets','--owned-graph-sort']):
            with self.subTest(flags=flags),patch.object(sys,'argv',base+flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir,\
                 patch.object(suite.boot,'process_ids',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_owned_reference_arguments_reach_only_workspace_boundary(self):
        class Parsed(Exception):pass
        argv=['run_launch_tests.py','--out','unused','--owned-reference','--retained-geometry','reference','--pass-targets']
        with patch.object(sys,'argv',argv),patch.object(suite.Path,'mkdir',side_effect=Parsed),\
             patch.object(suite.boot,'process_ids',side_effect=AssertionError('Test reached process access')):
            with self.assertRaises(Parsed):suite.main()

    def test_owned_reference_invalid_capture_options_do_not_access_process(self):
        import read_preparation
        import live_symbols
        class Parsed(Exception):pass
        base=['--pid','1','--out','unused','--owned-reference','--retained-geometry','reference','--pass-targets',
              '--merge-root','--schedule-preparation','apply']
        with patch.object(live_symbols,'resolve',side_effect=Parsed):
            with self.assertRaises(Parsed):read_preparation.main(base)
        for args in (base+['--owned-graph-sort'],base[:-1]+['check'],base[:5]+base[11:],
                     ['--pid','1','--out','unused','--owned-reference']):
            with self.subTest(args=args),patch('sys.stderr',io.StringIO()),\
                 patch.object(live_symbols,'resolve',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:read_preparation.main(args)
                self.assertEqual(result.exception.code,2)

    def test_owned_graph_repeat_requires_fresh_source_epoch_and_same_process(self):
        import copy
        owned=[0]*16;owned[2]=155;owned[15]=123
        resources=[0]*12;resources[0]=3;resources[3]=123
        first=dict(passed=True,pid=1,dll='build',graphJobLease=dict(epoch=1,stats=[3,155,10,20,30,1,0,123]),
                   ownedGraphSort=dict(stats=owned),resourceUses=dict(stats=resources),
                   definitionGenerations=dict(ticket=[11,1,2,7],coverageLost=0,binding=dict(consumerId=11,
                       rootAdmissionProved=True,executionBindingsProved=True,terminalCompletionProved=True)))
        second=copy.deepcopy(first);second['graphJobLease']['epoch']=2
        second['definitionGenerations']['ticket']=[12,2,3,7];second['definitionGenerations']['binding']['consumerId']=12
        suite.validate_owned_graph_repeat(first,second)
        for key,value in (('pid',2),('dll','different'),('passed',False)):
            bad=copy.deepcopy(second);bad[key]=value
            with self.assertRaises(ValueError):suite.validate_owned_graph_repeat(first,bad)
        for index,value in ((0,2),(1,154),(5,0),(6,1),(7,124)):
            bad=copy.deepcopy(second);bad['graphJobLease']['stats'][index]=value
            with self.assertRaises(ValueError):suite.validate_owned_graph_repeat(first,bad)
        bad=copy.deepcopy(second);bad['graphJobLease']['epoch']=1
        with self.assertRaises(ValueError):suite.validate_owned_graph_repeat(first,bad)
        for key,value in (('ticket',[11,2,3,7]),('coverageLost',1),('binding',{})):
            bad=copy.deepcopy(second);bad['definitionGenerations'][key]=value
            with self.assertRaises(ValueError):suite.validate_owned_graph_repeat(first,bad)

    def test_retained_invalid_launch_options_do_not_start_work(self):
        base=['run_launch_tests.py','--out','unused','--retained-geometry','reference']
        for flags in ([],['--pass-targets','--schedule-preparation','check'],['--pass-targets','--early-depth-gpu','all'],
                      ['--pass-targets','--skinning-depth'],['--pass-targets','--paired-interval'],
                      ['--pass-targets','--early-depth-interval','240'],['--pass-targets','--compare-depth'],
                      ['--pass-targets','--alpha-depth-optimized'],['--pass-targets','--native-camera-stages','3']):
            with self.subTest(flags=flags),patch.object(sys,'argv',base+flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir,\
                 patch.object(suite.boot,'process_ids',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_retained_launch_modes_reach_only_workspace_boundary(self):
        class Parsed(Exception):pass
        for mode in ('reference','replace'):
            for stage in (6,7):
                argv=['run_launch_tests.py','--out','unused','--retained-geometry',mode,'--retained-stage',str(stage),'--pass-targets']
                with self.subTest(mode=mode,stage=stage),patch.object(sys,'argv',argv),\
                     patch.object(suite.Path,'mkdir',side_effect=Parsed),\
                     patch.object(suite.boot,'process_ids',side_effect=AssertionError('Test reached process access')):
                    with self.assertRaises(Parsed):suite.main()

    def test_retained_capture_arguments_before_process_access(self):
        import read_preparation
        import live_symbols
        class Parsed(Exception):pass
        base=['--pid','1','--out','unused','--retained-geometry','reference']
        for flags in ([],['--pass-targets'],['--pass-targets','--merge-root','--schedule-preparation','check'],
                      ['--pass-targets','--merge-root','--schedule-preparation','apply','--early-depth-gpu','all'],
                      ['--pass-targets','--merge-root','--schedule-preparation','apply','--native-depth-timing']):
            with self.subTest(flags=flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(live_symbols,'resolve',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:read_preparation.main(base+flags)
                self.assertEqual(result.exception.code,2)
        for mode in ('reference','replace'):
            args=['--pid','1','--out','unused','--retained-geometry',mode,'--pass-targets','--merge-root','--schedule-preparation','apply']
            with patch.object(live_symbols,'resolve',side_effect=Parsed):
                with self.assertRaises(Parsed):read_preparation.main(args)

    def test_skinned_alpha_invalid_launch_options_do_not_start_work(self):
        base=['run_launch_tests.py','--out','unused','--family-manifest','manifest.json','--skinned-alpha-depth']
        for flags in ([],['--alpha-depth-reference'],['--skinning-depth'],
                      ['--skinning-depth','--alpha-depth-reference','--paired-interval'],
                      ['--skinning-depth','--alpha-depth-reference','--early-depth-interval','240'],
                      ['--skinning-depth','--alpha-depth-optimized','--depth-benchmark','2']):
            with self.subTest(flags=flags),patch.object(sys,'argv',base+flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(suite.Path,'mkdir') as mkdir,\
                 patch.object(suite.boot,'process_ids',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:suite.main()
                self.assertEqual(result.exception.code,2);mkdir.assert_not_called()

    def test_skinned_alpha_invalid_capture_options_do_not_access_process(self):
        import read_preparation
        import live_symbols
        base=['--pid','1','--out','unused','--skinned-alpha-depth']
        for flags in ([],['--alpha-depth-reference'],['--skinning-depth'],
                      ['--skinning-depth','--alpha-depth-reference','--native-depth-timing'],
                      ['--skinning-depth','--alpha-depth-reference','--alpha-depth-optimized']):
            with self.subTest(flags=flags),patch('sys.stderr',io.StringIO()),\
                 patch.object(live_symbols,'resolve',side_effect=AssertionError('CLI validation reached process access')):
                with self.assertRaises(SystemExit) as result:read_preparation.main(base+flags)
                self.assertEqual(result.exception.code,2)

    def test_skinned_alpha_propagation_matches_each_request_mode(self):
        self.assertEqual(suite.alpha_depth_flags(skinned=True),[])
        self.assertEqual(suite.alpha_depth_flags(reference=True),['--alpha-depth-reference'])
        self.assertEqual(suite.alpha_depth_flags(reference=True,skinned=True),
                         ['--alpha-depth-reference','--skinned-alpha-depth'])
        self.assertEqual(suite.alpha_depth_flags(optimized=True,skinned=True),
                         ['--alpha-depth-optimized','--skinned-alpha-depth'])
        # Reference variants turn alpha off for forced native fallback.
        for rejected in (False,True):
            flags=suite.alpha_depth_flags(reference=not rejected,skinned=True)
            self.assertEqual('--skinned-alpha-depth' in flags,not rejected)
        # Both optimized admission and its forced fallback retain the proof path.
        for rejected in (False,True):
            flags=suite.alpha_depth_flags(optimized=True,skinned=True)+(['--reject-depth-commit'] if rejected else [])
            self.assertIn('--skinned-alpha-depth',flags)
        # Material warmups pass neither alpha mode, even in a skinned-alpha suite.
        self.assertEqual(suite.alpha_depth_flags(reference=False,skinned=True),[])

    def test_delayed_shutdown_crash_is_detected_and_reporter_closed(self):
        now=0.;closed=[]
        def sleep(seconds):
            nonlocal now
            now+=seconds
        with patch.object(suite.time,'monotonic',side_effect=lambda:now),patch.object(suite.time,'sleep',side_effect=sleep),\
             patch.object(suite.boot,'crash_report',side_effect=lambda *_:'late report' if now>=1 else None),\
             patch.object(suite.boot,'new_reporters',side_effect=lambda baseline:{99} if now>=1.4 and not closed else set()),\
             patch.object(suite.boot,'close_reporters',side_effect=lambda pids:closed.extend(pids)):
            result=suite.observe_shutdown(77,100,{88},3)
        self.assertEqual(result,dict(crash='late report',reporters=[99]));self.assertEqual(closed,[99])
        self.assertGreaterEqual(now,3)

    def test_clean_shutdown_waits_without_closing_old_reporters(self):
        with patch.object(suite.time,'monotonic',side_effect=[0,1,2]),patch.object(suite.time,'sleep'),\
             patch.object(suite.boot,'crash_report',return_value=None),patch.object(suite.boot,'new_reporters',return_value=set()) as query,\
             patch.object(suite.boot,'close_reporters') as close:
            self.assertEqual(suite.observe_shutdown(77,100,{88},2),dict(crash=None,reporters=[]))
        close.assert_not_called();query.assert_called_with({88})

    def test_hud_requires_fresh_successful_submissions(self):
        def sample(i):
            return dict(now=1000+i*100,menu=0,tracked=1,layers=2,sourceTick=990+i*100,
                        serial=1+i*6,prepared=10+i*6,submitted=10+i*6,displayTime=100+i*100)
        gate=StableHud()
        for i in range(10):
            self.assertFalse(gate.observe(sample(i)))
        self.assertTrue(gate.observe(sample(10)))
        for key,value in (('menu',1),('layers',0),('tracked',0),('sourceTick',0),('serial',0)):
            gate=StableHud()
            for i in range(20):
                self.assertFalse(gate.observe(dict(sample(i),**{key:value})))
        for key in ('serial','submitted','prepared','displayTime'):
            gate=StableHud()
            for i in range(20):
                self.assertFalse(gate.observe(dict(sample(i),**{key:1})))

    def test_hud_gap_or_menu_resets_stability(self):
        gate=StableHud()
        value=dict(now=1000,menu=0,tracked=1,layers=1,sourceTick=1000,serial=1,prepared=1,submitted=1,displayTime=1)
        gate.observe(value)
        gate.observe(dict(value,now=2000,sourceTick=2000,serial=80,prepared=80,submitted=80,displayTime=80))
        self.assertEqual(gate.report()['stableMilliseconds'],0)
        gate.observe(dict(value,menu=1))
        self.assertEqual(gate.report()['stableSamples'],0)

    def test_window_validation_without_child_process(self):
        game = dict(width=2560, height=1440, minimized=False, visible=True, **{'class':'W2ViewportClass'})
        self.assertEqual(validate_windows([game]), [dict(client=[2560,1440], minimized=False)])
        for rows in ([], [dict(game, width=1024, height=768)],
                     [game, dict(game, **{'class':'CyberpunkVRPortLauncherClass'})]):
            with self.assertRaises(RuntimeError):
                validate_windows(rows)

    def test_both_eyes_required(self):
        for values in ((0, 0), (100, 0), (0, 100)):
            with self.assertRaises(ValueError):
                check_both_eyes(bitmap(*values))
        self.assertEqual(check_both_eyes(bitmap(100, 80))['nonblackFraction'], [1.0, 1.0])

    def test_layout_and_bounds(self):
        for bpp in (24, 32):
            for top_down in (False, True):
                self.assertEqual(check_both_eyes(bitmap(30, 40, top_down, bpp))['height'], 32)
        for data in (b'', bitmap(30, 40)[:-1]):
            with self.assertRaises(ValueError):
                check_both_eyes(data)
        data = bitmap(30, 40)
        struct.pack_into('<I', data, 30, 1)
        with self.assertRaises(ValueError):
            check_both_eyes(data)

    def test_child_progress_and_error(self):
        lines = []
        output = run_logged([sys.executable, '-c', 'print("ready", flush=True)'], 5, lines.append)
        self.assertEqual(output.strip(), 'ready')
        self.assertEqual(len(lines), 1)
        with self.assertRaises(RuntimeError):
            run_logged([sys.executable, '-c', 'raise SystemExit(2)'], 5)

    def test_localized_child_diagnostic_is_not_lost_to_console_encoding(self):
        output = run_logged([sys.executable, '-c', 'print(chr(0x041e) + chr(0x0448))'], 5)
        self.assertEqual(output.strip(), chr(0x041e) + chr(0x0448))

    def test_child_timeout_is_bounded(self):
        with self.assertRaises(TimeoutError):
            run_logged([sys.executable, '-c', 'import time; time.sleep(5)'], .1)


if __name__ == '__main__':
    unittest.main()
