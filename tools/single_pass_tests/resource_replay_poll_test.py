import itertools
import unittest
from read_preparation import read_resource_replay_snapshot


class Clock:
    now=0.
    def monotonic(self):return self.now
    def sleep(self,seconds):self.now+=seconds


class Telemetry:
    def __init__(self,samples):self.samples=iter(samples);self.current=None;self.row_reads=0
    def read32(self,name):
        if name.endswith('Seq'):
            if self.current is None:
                self.current=next(self.samples);seq=self.current[0]
                if seq&1:self.current=None
                return seq
            value=self.current[1];self.current=None;return value
        return self.current[2]
    def words(self,name,count):
        out=[0]*count
        if name.endswith('Proof'):out[3]=self.current[3]
        return out
    def rows(self,count):self.row_reads+=1;return bytes(count*32)


class ResourceReplayPollTests(unittest.TestCase):
    def read(self,samples):
        clock=Clock();telemetry=Telemetry(samples)
        result=read_resource_replay_snapshot(telemetry.read32,telemetry.words,telemetry.rows,
            timeout=.02,monotonic=clock.monotonic,sleep=clock.sleep)
        return result,telemetry
    def test_stable_full_rows(self):
        result,t=self.read([(2,2,32,155)])
        self.assertEqual(len(result['raw']),155*32);self.assertEqual(t.row_reads,1)
    def test_torn_then_complete(self):
        result,t=self.read([(1,1,32,0),(2,4,32,155),(6,6,32,155)])
        self.assertEqual(result['sequence'],6);self.assertEqual(t.row_reads,2)
    def test_even_sequence_wrap(self):
        result,_=self.read([(0xfffffffe,0,32,155),(0,0,32,155)])
        self.assertEqual(result['sequence'],0)
    def test_torn_invalid_extent_is_not_published(self):
        result,t=self.read([(2,4,99,999),(6,6,32,155)])
        self.assertEqual(result['proof'][3],155);self.assertEqual(t.row_reads,1)
    def test_stable_bad_extent_fails(self):
        for sample in ((2,2,99,155),(2,2,32,221)):
            with self.assertRaises(ValueError):self.read([sample])
    def test_permanently_torn_times_out(self):
        with self.assertRaises(TimeoutError):self.read(itertools.repeat((2,4,32,155)))
    def test_permanently_odd_times_out(self):
        with self.assertRaises(TimeoutError):self.read(itertools.repeat((1,1,32,155)))


if __name__=='__main__':unittest.main()
