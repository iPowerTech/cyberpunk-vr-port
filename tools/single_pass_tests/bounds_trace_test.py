import unittest
from bounds_trace import RECORD,decode


def record(sequence,kind,span=0,key=100):
    return RECORD.pack(sequence,sequence*100,0x120,0x1000,span,kind,7,0x1E57E8,0,-1,2,key,1,*([0.0]*6),*([0x12345]*12),1,1)


class BoundsTraceTests(unittest.TestCase):
    def rows(self):
        return [record(i+1,k,1 if k in (1,2) else 0,101 if k in (5,6) else 100)
                for i,k in enumerate((0,3,4,1,5,6,7))]

    def test_write_attribution_and_native_counter_difference(self):
        result=decode(b''.join(self.rows()),changed_ids=('0x120','0x999'))['summary']
        self.assertEqual(result['callsBetweenSnapshots'],1)
        self.assertEqual(result['changedCallers'],{'0x1e57e8':1})
        self.assertEqual(result['changedIdsTraced'],['0x120'])
        self.assertEqual(result['untracedChangedIds'],['0x999'])
        self.assertEqual(result['nativeCounters'],dict(vrcam=100,main=101))

    def test_incomplete_spans_and_overflow_are_rejected(self):
        rows=self.rows()
        for data,lost in ((b''.join(rows),1),(b''.join(rows)[:-1],0),(b''.join(rows[:2]+rows[3:]),0)):
            with self.assertRaises(ValueError):decode(data,lost)

    def test_writes_overlapping_snapshot_are_visible(self):
        data=b''.join(record(i+1,k,1 if k==1 else 0) for i,k in enumerate((0,3,1,4,5,6,7)))
        result=decode(data)['summary']
        self.assertEqual(result['callsDuringSnapshot'],[1])
        self.assertEqual(result['callsBetweenSnapshots'],0)
