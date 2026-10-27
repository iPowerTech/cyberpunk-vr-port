import unittest
from paired_interval import validate_stats,validate_eager,validate_schedule,schedule_profile,EAGER_FIELDS


class PairedIntervalTests(unittest.TestCase):
    def good(self):
        return dict(RunState=2,RunFailure=0,RunFrames=120,RootSkipped=120,PairMerged=120,
                    PairCompleted=120,DepthMain=120,DepthVrcam=120,RunEyeCopies=120,RunEyeSubmissions=120)

    def test_complete_interval(self):
        validate_stats(self.good(),120)

    def test_every_count_and_terminal_state_is_required(self):
        for key in self.good():
            with self.subTest(key=key):
                value=self.good();value[key]+=1
                with self.assertRaises(RuntimeError):validate_stats(value,120)
        value=self.good();value['RunEyeSubmissions']=119
        with self.assertRaises(RuntimeError):validate_stats(value,120)

    def test_eager_comparison_and_replacement_require_actual_matches(self):
        value=dict.fromkeys(EAGER_FIELDS,0)
        value.update(compatible=1,verified=1,main=120,vrcam=120,inputSame=80,outputSame=80,prepared=100)
        validate_eager(value,1,120)
        value.update(outputSame=0,replaced=80);validate_eager(value,2,120)
        for change in (dict(replaced=79),dict(verified=0),dict(outputDifferent=1),dict(failure=1),dict(main=119),dict(replaced=0,inputSame=0)):
            with self.assertRaises(RuntimeError):validate_eager(dict(value,**change),2,120)

    def test_schedule_requires_every_frame_and_no_fallback(self):
        before=[3,3,3,0]+[0]*12;after=[123,123,123,0]+[0]*12
        validate_schedule(before,after,120)
        for i in range(4):
            bad=after.copy();bad[i]+=1
            with self.assertRaises(RuntimeError):validate_schedule(before,bad,120)
        with self.assertRaises(RuntimeError):validate_schedule(before,after[:4],120)

    def test_schedule_cache_and_profile_evidence(self):
        before=[0]*16;after=[120,120,120,0]+[0]*12
        after[12]=119;after[13]=1;validate_schedule(before,after,120,True)
        with self.assertRaises(RuntimeError):validate_schedule(before,after,120,False)
        after[12]=0;after[13]=120;validate_schedule(before,after,120,False)
        with self.assertRaises(RuntimeError):validate_schedule(before,after,120,True)
        after[13]=119
        with self.assertRaises(RuntimeError):validate_schedule(before,after,120,False)
        p=schedule_profile([1,10]*3,[121,110]*3,1000)
        self.assertEqual(p['plan'],dict(calls=120,milliseconds=100))
        with self.assertRaises(ValueError):schedule_profile([0]*6,[0]*5,1)
        with self.assertRaises(ValueError):schedule_profile([0]*6,[0]*6,0)
        with self.assertRaises(ValueError):schedule_profile([1]*6,[0]*6,1)


if __name__=='__main__':unittest.main()
