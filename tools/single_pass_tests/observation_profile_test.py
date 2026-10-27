import unittest
from observation_profile import decode


class ObservationProfileTests(unittest.TestCase):
    def test_nested_intervals_are_reported_separately(self):
        before=[2,10]*8;after=[5,110]*8
        result=decode(before,after,100000)
        self.assertEqual(result['uploadLookup'],dict(calls=3,milliseconds=1.))
        self.assertEqual(result['uploadStackUnwind'],dict(calls=3,milliseconds=1.))
        self.assertEqual(len(result),8)

    def test_invalid_publication_rejected(self):
        for before,after,frequency in (([0]*16,[0]*15,1),([0]*15,[0]*16,1),([1]*16,[0]*16,1),([0]*16,[0]*16,0)):
            with self.assertRaises(ValueError):decode(before,after,frequency)


if __name__=='__main__':unittest.main()
