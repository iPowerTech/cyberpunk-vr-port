import unittest
from particle_trace import RECORD,decode


def row(seq,kind=0,epoch=100,stamp=99,delta=0.016,side=1,onscreen=1,valid=1):
    return RECORD.pack(seq,seq*1000,0x10,0x20,0x30,0x40,kind,7,0x123,0x456,epoch,stamp,77,0,side,onscreen,valid,1,delta,3,0,0)


class ParticleTraceTests(unittest.TestCase):
    def test_repeated_instance_records_keep_epoch_and_time_separate(self):
        report=decode(row(1)+row(2,kind=1,stamp=100,delta=0)+row(3,epoch=101,stamp=100,delta=0,side=0))
        self.assertEqual(report['summary']['repeatedInstances'],1)
        self.assertEqual(report['summary']['epochs'],[100,101])
        self.assertEqual(report['summary']['entryAlreadyStamped'],0)
        self.assertEqual(report['summary']['entryAccumulatedPositive'],1)
        self.assertIsNone(report['records'][1]['onscreen'])

    def test_incomplete_or_invalid_observations_fail(self):
        for data,lost in ((row(1),1),(row(1)[:-1],0),(row(2),0),(row(1,valid=0),0),(row(1,onscreen=255),0),(row(1,delta=float('nan')),0)):
            with self.assertRaises(ValueError):decode(data,lost)
