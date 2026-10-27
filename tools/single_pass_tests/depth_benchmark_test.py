import copy
import unittest

from depth_benchmark import summarize
from early_depth_report import FIELDS,NATIVE_FIELDS,OPTIMIZED_FIELDS,decode_gpu_times


def sample(mode,main=100,peer=100):
    stats=dict.fromkeys(FIELDS,0);stats.update(mainSeen=140,peerSeen=141,mainEnded=1,peerEnded=1)
    d=dict(state=3,result=0,stats=stats,comparison=None,nativeDifferences=None,nativeDrawsReplaced=0,
           descriptorFailure=0,descriptorStoreStats=[0,2,20,2,20,0],gpuInstances=[0,0,0,0],
           nativeTiming=mode=='native',targetSize=[1485,1485],gpuTimes=decode_gpu_times([10,10+main,100000,20,20+peer,100000]),
           sources=[],coverage=[],masks=[],extraCoverage=[])
    if mode=='native':
        d['nativeTargets']=dict(zip(NATIVE_FIELDS,(3,140,141,0,0,0,0,0)))
        d['optimized']=dict(zip(OPTIMIZED_FIELDS,(0,2**64-1,0,0,0,0,0,0)))
    else:
        stats.update(recorded=128,matched=128)
        d.update(netDrawCommandReduction=128,nativeDrawsReplaced=128,coverage=[[1,1]]*128,masks=[[1,0]]*128,extraCoverage=[[0,0]]*128)
        d['nativeTargets']=dict(zip(NATIVE_FIELDS,(2,140,141,12,13,1,1,2)))
        d['optimized']=dict(zip(OPTIMIZED_FIELDS,(1,1,140,13,0,128,0,1)))
        d['maskStats']=dict(mainPackets=140,peerPackets=141,mainInstances=140,peerInstances=141,both=128,mainOnly=0,
                            pairedRuns=128,originFailure=0,peerOnly=0,peerOnlyRuns=0,knownPeerOnly=0,unrecordedPeerOnly=0)
    return dict(mode=mode,capture=dict(pid=1,dll='same',passed=True,cleanup='restored',earlyDepthGpu=d))


class DepthBenchmarkTests(unittest.TestCase):
    def test_balanced_order_and_signed_regression(self):
        r=summarize([sample('native'),sample('optimized',200,100),sample('optimized',200,100),sample('native')])
        self.assertEqual(r['medianPairedSavingMs'],-1.)
        self.assertEqual(r['distributions']['optimized']['medianMs'],3.)
        self.assertEqual([p['order'] for p in r['pairs']],[['native','optimized'],['optimized','native']])

    def test_partial_or_incomparable_captures_fail(self):
        for cases in ([],[sample('native')],[sample('native'),sample('native')]):
            with self.assertRaises(ValueError):summarize(cases)
        for key,value in (('pid',2),('dll','changed'),('passed',False),('cleanup','process_exited')):
            a,b=sample('native'),sample('optimized');b['capture'][key]=value
            with self.assertRaises(ValueError):summarize([a,b])
        a,b=sample('native'),sample('optimized');b['capture']['earlyDepthGpu']['targetSize']=[100,100]
        with self.assertRaises(ValueError):summarize([a,b])

    def test_gpu_fallback_or_fabricated_duration_cannot_be_speedup(self):
        a,b=sample('native'),sample('optimized')
        invalid=copy.deepcopy(b);invalid['capture']['earlyDepthGpu']['optimized']['gpuDecision']=0
        with self.assertRaises(RuntimeError):summarize([a,invalid])
        b['capture']['earlyDepthGpu']['gpuTimes']['sumMs']=0
        with self.assertRaises(ValueError):summarize([a,b])


if __name__=='__main__':unittest.main()
