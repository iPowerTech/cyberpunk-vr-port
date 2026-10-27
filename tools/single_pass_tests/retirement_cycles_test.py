import copy
import unittest

from retirement_stress import validate_cycles


def cycles():
    result=[]
    for index in range(4):
        result.append(dict(passed=True,pid=77,dll='build',restoration=dict(dirty=False),state=[1,0,2,0,0,0,0,0],
            proof=dict(header='0x1000',generation=index+1,born=index+2,consumerId=100+index,
                cacheBase='0x3000',renderer='0x4000',nativeDestroyReturned=True,metadataRecycled=True,
                cacheOwnershipCorrelationProved=True,eventSequences=dict(destroyReturn=10+index*10,recycle=11+index*10)),
            last=dict(totals=dict(savedDestroyBegun=index+1,savedDestroyReturned=index+1,metadataRecycled=index+1))))
    return result


class RetirementCyclesTest(unittest.TestCase):
    def test_multiple_generations_can_reuse_same_physical_header(self):
        result=validate_cycles(cycles(),4)
        self.assertEqual(result['uniqueRetiredGenerations'],4)
        self.assertEqual(result['nativeDestroyReturned'],4)
        self.assertTrue(result['boundedOutstandingReservations'])
        self.assertFalse(result['wholeSceneSinglePassProved']);self.assertFalse(result['performanceGainProved'])

    def test_all_cycles_are_required(self):
        for count in (0,2,3,5,9,True):
            with self.assertRaises(ValueError):validate_cycles(cycles(),count)

    def test_process_build_or_cache_change_rejects(self):
        for name,value in (('pid',88),('dll','other')):
            bad=cycles();bad[-1][name]=value
            with self.assertRaises(ValueError):validate_cycles(bad,4)
        for name in ('cacheBase','renderer'):
            bad=cycles();bad[-1]['proof'][name]='0x5000'
            with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_unrestored_or_failed_cycle_rejects(self):
        for field in ('passed','restoration'):
            bad=cycles();bad[2][field]=False
            with self.assertRaises(ValueError):validate_cycles(bad,4)
        bad=cycles();bad[2]['restoration']['dirty']=True
        with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_old_generation_or_consumer_is_not_a_new_cycle(self):
        bad=cycles();bad[1]['proof'].update(generation=1,born=2)
        with self.assertRaises(ValueError):validate_cycles(bad,4)
        bad=cycles();bad[1]['proof']['consumerId']=100
        with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_old_destructor_or_recycling_receipt_rejects(self):
        for name in ('destroyReturn','recycle'):
            bad=cycles();bad[2]['proof']['eventSequences'][name]=bad[1]['proof']['eventSequences'][name]
            with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_each_cycle_requires_actual_native_proof(self):
        for name in ('nativeDestroyReturned','metadataRecycled','cacheOwnershipCorrelationProved'):
            bad=cycles();bad[2]['proof'][name]=False
            with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_pending_detached_or_completed_unrecycled_storage_rejects(self):
        for index,value in ((1,2),(2,129),(3,1),(4,1),(5,1),(6,1),(7,1),(1,-1),(1,True)):
            bad=cycles();bad[2]['state'][index]=value
            with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_actual_cumulative_totals_must_advance(self):
        for name in ('savedDestroyBegun','savedDestroyReturned','metadataRecycled'):
            bad=cycles();bad[2]['last']['totals'][name]=2
            with self.assertRaises(ValueError):validate_cycles(bad,4)

    def test_input_is_unchanged(self):
        value=cycles();before=copy.deepcopy(value);validate_cycles(value,4)
        self.assertEqual(value,before)


if __name__=='__main__':unittest.main()
