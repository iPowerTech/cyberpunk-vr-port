import itertools
import unittest

from read_preparation import wait_owned_execution


def snapshot(status=4, epoch=2, pair=3, seq=2, after=None, closing=0):
    stats=[0]*24;stats[:4]=[status,99,epoch,pair]
    return dict(stats=stats,resources=[0]*12,compiler=[0]*16,retirement=[0]*8,admission=[0]*6,
                seq=seq,after=seq if after is None else after,closing=closing)


class Clock:
    def __init__(self):self.now=0.
    def monotonic(self):return self.now
    def sleep(self, seconds):self.now+=seconds


class Telemetry:
    def __init__(self, samples):self.samples=iter(samples);self.current=None
    def read32(self, name):
        if name.endswith('Seq'):
            if self.current is None:
                self.current=next(self.samples);value=self.current['seq']
                if value&1:self.current=None
                return value
            value=self.current['after'];self.current=None;return value
        return self.current['closing']
    def read_words(self, name, count):
        key='stats' if name.endswith('Stats') else 'resources' if name.endswith('Resources') else 'compiler' if name.endswith('Compiler') else 'retirement' if name.endswith('Retirement') else 'admission'
        return self.current[key]


class OwnedExecutionPollTest(unittest.TestCase):
    def poll(self, samples, **kwargs):
        telemetry=Telemetry(samples);clock=Clock()
        return wait_owned_execution(telemetry.read32,telemetry.read_words,2,3,
            monotonic=clock.monotonic,sleep=clock.sleep,timeout=.1,**kwargs)

    def test_complete_current_epoch(self):
        result=self.poll([snapshot()])
        self.assertEqual(result['stats'][:4],[4,99,2,3])

    def test_old_complete_result_does_not_end_repeat(self):
        for field in ('epoch','pair'):
            seen=[]
            result=self.poll([snapshot(**{field:1}),snapshot()],on_snapshot=seen.append)
            self.assertEqual(len(seen),2);self.assertEqual(result['stats'][2:4],[2,3])

    def test_old_rejection_does_not_end_new_request(self):
        for status in (5,6):
            self.assertEqual(self.poll([snapshot(status,epoch=1),snapshot()])['stats'][0],4)

    def test_current_rejection_is_returned_for_diagnostics(self):
        for status in (5,6):
            self.assertEqual(self.poll([snapshot(status)])['stats'][0],status)

    def test_nonterminal_status_keeps_waiting(self):
        for status in (0,1,2,3):
            self.assertEqual(self.poll([snapshot(status),snapshot()])['stats'][0],4)

    def test_odd_and_torn_publications_are_not_observed(self):
        seen=[]
        result=self.poll([snapshot(seq=1),snapshot(seq=2,after=4),snapshot(seq=6)],on_snapshot=seen.append)
        self.assertEqual(result['sequence'],6);self.assertEqual(len(seen),1)

    def test_even_sequence_wrap_is_accepted(self):
        self.assertEqual(self.poll([snapshot(seq=0xfffffffe,after=0),snapshot(seq=0)])['sequence'],0)

    def test_closing_is_returned_immediately_even_with_stale_receipt(self):
        result=self.poll([snapshot(epoch=1,closing=1)])
        self.assertEqual(result['closing'],1)

    def test_stale_result_times_out_with_identity(self):
        seen=[]
        with self.assertRaisesRegex(TimeoutError,"'epoch': 1"):
            self.poll(itertools.repeat(snapshot(epoch=1)),on_snapshot=seen.append)
        self.assertTrue(seen)

    def test_permanently_torn_publication_times_out(self):
        seen=[]
        with self.assertRaises(TimeoutError):
            self.poll(itertools.repeat(snapshot(seq=2,after=4)),on_snapshot=seen.append)
        self.assertFalse(seen)

    def test_permanently_odd_publication_times_out(self):
        with self.assertRaises(TimeoutError):self.poll(itertools.repeat(snapshot(seq=1)))

    def test_abi_lengths_are_checked(self):
        for key in ('stats','resources','compiler','retirement','admission'):
            bad=snapshot();bad[key].pop()
            with self.assertRaises(ValueError):self.poll([bad])

    def test_callback_receives_independent_words(self):
        sample=snapshot();result=self.poll([sample])
        sample['stats'][1]=0;sample['resources'][0]=10
        self.assertEqual(result['stats'][1],99);self.assertEqual(result['resources'][0],0)

    def test_pair_does_not_truncate_to_uint32(self):
        pair=(1<<40)+3;sample=snapshot(pair=pair);telemetry=Telemetry([sample]);clock=Clock()
        result=wait_owned_execution(telemetry.read32,telemetry.read_words,2,pair,
            monotonic=clock.monotonic,sleep=clock.sleep)
        self.assertEqual(result['stats'][3],pair)

    def test_invalid_expected_identity_is_rejected_before_read(self):
        def forbidden(*args):raise AssertionError('Read reached with invalid expected identity')
        for epoch,pair in ((0,3),(1<<32,3),(True,3),(2,0),(2,1<<64),(2,True)):
            with self.assertRaises(ValueError):wait_owned_execution(forbidden,forbidden,epoch,pair)


if __name__=='__main__':unittest.main()
