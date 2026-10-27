import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

from retirement_settings import NAMES
from retirement_stress import run


class Probe:
    def __init__(self):self.baseline=dict.fromkeys(NAMES,False);self.applied=[];self.restored=False
    def begin(self):return dict(values=self.baseline)
    def apply(self,values):self.applied.append(values);return dict(values=values)
    def restore_live(self):self.restored=True;return dict(values=self.baseline)


class Telemetry:
    pid=77
    symbols={'sha256':'build'}
    def __init__(self):self.calls=0;self.closed=False
    def read(self):
        self.calls+=1
        allocator=[0]*24;allocator[0]=2 if self.closed else 1;allocator[5]=1
        cache=[0]*16;cache[0]=self.calls;cache[3]=5;cache[7]=123;cache[8]=2
        return dict(pid=self.pid,dll=self.symbols['sha256'],Allocator=allocator,Cache=cache,
                    RetirementTotals=[0]*16,Retirement=[1,0,2,0,0,0,0,0],events=dict(sequence=self.calls),sequence=self.calls*2)


class RetirementStressTest(unittest.TestCase):
    def capture(self):
        stats=[0]*24;stats[1:4]=[99,2,3]
        return dict(pid=77,dll='build',ownedExecution=dict(stats=stats,admission=[99,2,3,0,0x1000,0x2000]))
    def module(self,validate):
        return types.SimpleNamespace(decode=lambda raw:dict(events=[],calls=raw['Cache'][0]),
            merge_event_history=lambda history,events:history+events,validate_retirement=validate)

    def test_requires_actual_validator_success_and_restores(self):
        probe=Probe();telemetry=Telemetry();seen=[]
        def validate(report,*identity,**kwargs):
            seen.append((identity,kwargs))
            if report['calls']<3:raise ValueError('not yet recycled')
            return dict(recycled=True,cacheOwnershipCorrelationProved=True)
        with tempfile.TemporaryDirectory() as directory,patch.dict('sys.modules',retirement_report=self.module(validate)),\
             patch('retirement_stress.time.sleep'):
            path=Path(directory)/'result.json';result=run(probe,telemetry,self.capture(),path)
            self.assertTrue(result['passed']);self.assertTrue(probe.restored)
            self.assertEqual(len(probe.applied),2)
            self.assertEqual(seen[-1],((99,2,3),dict(require_recycled=True)))
            self.assertTrue(json.loads(path.read_text())['restoration'])

    def test_matching_cvar_acknowledgement_does_not_infer_retirement(self):
        probe=Probe();telemetry=Telemetry();now=0
        def clock():return now
        def sleep(seconds):
            nonlocal now
            now+=seconds
        def validate(*args,**kwargs):raise ValueError('native destroy absent')
        with tempfile.TemporaryDirectory() as directory,patch.dict('sys.modules',retirement_report=self.module(validate)),\
             patch('retirement_stress.time.monotonic',side_effect=clock),patch('retirement_stress.time.sleep',side_effect=sleep):
            path=Path(directory)/'result.json'
            with self.assertRaisesRegex(RuntimeError,'not exercised'):run(probe,telemetry,self.capture(),path,timeout=.2)
            self.assertTrue(probe.restored);self.assertFalse(json.loads(path.read_text())['passed'])

    def test_foreign_process_or_build_rejected_before_mutation(self):
        for field,value in (('pid',88),('dll','other')):
            capture=self.capture();capture[field]=value;probe=Probe()
            with patch.dict('sys.modules',retirement_report=self.module(lambda *args,**kwargs:{})):
                with self.assertRaises(ValueError):run(probe,Telemetry(),capture,'unused')
            self.assertFalse(probe.applied)

    def test_allocator_close_stops_loop_and_restores(self):
        telemetry=Telemetry();telemetry.closed=True;probe=Probe()
        with tempfile.TemporaryDirectory() as directory,patch.dict('sys.modules',retirement_report=self.module(lambda *args,**kwargs:dict(cacheOwnershipCorrelationProved=True))):
            with self.assertRaisesRegex(RuntimeError,'allocator closed'):
                run(probe,telemetry,self.capture(),Path(directory)/'result.json')
            self.assertTrue(probe.restored);self.assertFalse(probe.applied)

    def test_restore_failure_is_not_hidden_by_valid_retirement(self):
        probe=Probe();probe.restore_live=lambda:(_ for _ in ()).throw(RuntimeError('restore failed'))
        with tempfile.TemporaryDirectory() as directory,patch.dict('sys.modules',retirement_report=self.module(lambda *args,**kwargs:dict(cacheOwnershipCorrelationProved=True))):
            path=Path(directory)/'result.json'
            with self.assertRaisesRegex(RuntimeError,'restore failed'):run(probe,Telemetry(),self.capture(),path)
            self.assertFalse(json.loads(path.read_text())['passed'])


if __name__=='__main__':unittest.main()
