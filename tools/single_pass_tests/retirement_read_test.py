import unittest

from read_retirement import snapshot


class RetirementReadTest(unittest.TestCase):
    def setUp(self):self.now=0
    def clock(self):return self.now
    def sleep(self,seconds):self.now+=seconds
    def words(self,name,count):return [0]*count

    def test_one_coherent_publication_captures_full_physical_rings(self):
        values=iter((4,4))
        result=snapshot(lambda _:next(values),lambda _:0,self.words,
                        monotonic=self.clock,sleep=self.sleep)
        self.assertEqual(result['sequence'],4)
        self.assertEqual(len(result['events']['rows']),32)
        self.assertEqual(len(result['cacheRows']),64)
        self.assertEqual(len(result['Allocator']),24)
        self.assertEqual(len(result['RetirementTotals']),16)

    def test_torn_and_odd_publications_are_retried(self):
        values=iter((1,2,4,6,6))
        result=snapshot(lambda _:next(values),lambda _:0,self.words,
                        monotonic=self.clock,sleep=self.sleep)
        self.assertEqual(result['sequence'],6)

    def test_persistent_writer_times_out(self):
        with self.assertRaises(TimeoutError):
            snapshot(lambda _:1,lambda _:0,self.words,timeout=.03,monotonic=self.clock,sleep=self.sleep)

    def test_event_ring_overflow_is_never_silently_truncated(self):
        with self.assertRaises(ValueError):
            snapshot(lambda _:2,lambda name:33 if name.endswith('Count') else 40,self.words,
                     monotonic=self.clock,sleep=self.sleep)

    def test_wrong_row_stride_is_rejected(self):
        for target in ('RetirementEvents','CacheRows'):
            def words(name,count):return [0]*(count-1 if name==target else count)
            with self.assertRaises(ValueError):
                snapshot(lambda _:2,lambda _:0,words,monotonic=self.clock,sleep=self.sleep)


if __name__=='__main__':unittest.main()
