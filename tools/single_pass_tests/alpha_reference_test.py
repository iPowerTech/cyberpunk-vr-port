import unittest
from early_depth_report import validate_alpha_reference,validate_skinning,validate_skinned_alpha,validate_skinned_alpha_options


class AlphaReferenceTests(unittest.TestCase):
    def test_nonzero_paired_coverage(self):
        report=dict(alphaReference=dict(enabled=True,stats=[8,8,8,0,0,0]))
        validate_alpha_reference(report,False)
        with self.assertRaises(RuntimeError):validate_alpha_reference(report,True)
        for stats in ([8,0,0,0,0,0],[0,0,0,0,0,0],[8,8,0,0,0,0],[8,8,8,1,0,0],[8,8,8,0,1,0],[8,8,8,0,0,5]):
            with self.assertRaises(RuntimeError):validate_alpha_reference(dict(alphaReference=dict(enabled=True,stats=stats)),False)

    def test_disabled_and_bad_shape(self):
        validate_alpha_reference({},False)
        validate_alpha_reference(dict(alphaReference=dict(enabled=False,stats=[0]*6)),True)
        for stats in ([1,0,0,0,0,0],[0]*5,[-1]*6):
            with self.assertRaises(RuntimeError):validate_alpha_reference(dict(alphaReference=dict(enabled=False,stats=stats)),False)

    def test_optimized_snapshots_and_shared_skinning_counters(self):
        alpha=dict(enabled=True,proofs=[8,8,78,2336,3750000,0],recording=[1,1,1,1,0,1])
        report=dict(alphaReference=dict(enabled=False,stats=[8,8,8,0,0,0]),alphaOptimized=alpha)
        validate_alpha_reference(report,True)
        with self.assertRaises(RuntimeError):validate_alpha_reference(report,False)
        skin=dict(enabled=True,stats=[5,5,5,0,0,0],proofs=[13,96,63776,3766128],bindingChecks=[5,5,0,0])
        validate_skinning(skin,True,alpha['proofs'])
        with self.assertRaises(RuntimeError):validate_skinning(skin,True)
        alpha['proofs'][5]=4001
        with self.assertRaises(RuntimeError):validate_alpha_reference(report,True)

    @staticmethod
    def optimized_report():
        return dict(alphaReference=dict(enabled=False,stats=[8,8,8,0,0,0]),
                    alphaOptimized=dict(enabled=True,proofs=[8,8,78,2336,3750000,0],recording=[1,1,1,1,0,1]))

    def test_recording_receipt_required_when_enabled(self):
        report=self.optimized_report()
        del report['alphaOptimized']['recording']
        with self.assertRaisesRegex(RuntimeError,'recording receipt'):validate_alpha_reference(report,True)
        for receipt in ([0]*6,[1,1,0,0,0,1],[1,0,1,0,0,1],[1,1,1,0,0,1]):
            with self.subTest(receipt=receipt):
                report=self.optimized_report();report['alphaOptimized']['recording']=receipt
                with self.assertRaises(RuntimeError):validate_alpha_reference(report,True)

    def test_recording_receipt_must_match_every_counter(self):
        expected=[1,1,1,1,0,1]
        for index in range(6):
            for value in (0,1,2):
                if value==expected[index]:continue
                with self.subTest(index=index,value=value):
                    report=self.optimized_report();report['alphaOptimized']['recording'][index]=value
                    with self.assertRaises(RuntimeError):validate_alpha_reference(report,True)

    def test_recording_receipt_numeric_shape(self):
        malformed=[None,{},'',0,[0]*5,[0]*7,tuple([1,1,1,1,0,1])]
        for value in (-1,2**64,1.0,True,'1',None):
            receipt=[1,1,1,1,0,1];receipt[0]=value;malformed.append(receipt)
        for receipt in malformed:
            with self.subTest(receipt=receipt):
                report=self.optimized_report();report['alphaOptimized']['recording']=receipt
                with self.assertRaisesRegex(RuntimeError,'recording receipt'):validate_alpha_reference(report,True)

    def test_disabled_recording_receipt_is_zero_or_missing(self):
        for reference in (False,True):
            report=dict(alphaReference=dict(enabled=reference,stats=[8,8,8,0,0,0] if reference else [0]*6),
                        alphaOptimized=dict(enabled=False,proofs=[0]*6))
            validate_alpha_reference(report,False)
            report['alphaOptimized']['recording']=[0]*6
            validate_alpha_reference(report,False)
            for index in range(6):
                receipt=[0]*6;receipt[index]=1;report['alphaOptimized']['recording']=receipt
                with self.subTest(reference=reference,index=index):
                    with self.assertRaises(RuntimeError):validate_alpha_reference(report,False)
            for receipt in (None,[False]*6,[0]*5,[0]*7):
                report['alphaOptimized']['recording']=receipt
                with self.assertRaises(RuntimeError):validate_alpha_reference(report,False)

    @staticmethod
    def skinned_report(optimized=False):
        return dict(skinnedAlpha=dict(enabled=True,stats=[2,2,2,0,0,0]),
                    alphaReference=dict(enabled=not optimized,stats=[10,10,10,0,0,0]),
                    alphaOptimized=dict(enabled=optimized),skinning=dict(enabled=True,stats=[7,7,7,0,0,0]))

    def test_skinned_alpha_requires_complete_bounded_coverage(self):
        for optimized in (False,True):validate_skinned_alpha(self.skinned_report(optimized))
        for stats in ([0]*6,[2,0,2,0,0,0],[2,2,0,0,0,0],[1,2,2,0,0,0],
                      [2,2,2,1,0,0],[2,2,2,0,1,0],[2,2,2,0,0,1]):
            report=self.skinned_report();report['skinnedAlpha']['stats']=stats
            with self.subTest(stats=stats):
                with self.assertRaises(RuntimeError):validate_skinned_alpha(report)
        for domain in ('alphaReference','skinning'):
            for index in range(3):
                report=self.skinned_report();report[domain]['stats'][index]=1
                with self.subTest(domain=domain,index=index):
                    with self.assertRaises(RuntimeError):validate_skinned_alpha(report)

    def test_skinned_alpha_disabled_and_malformed(self):
        validate_skinned_alpha({})
        validate_skinned_alpha(dict(skinnedAlpha=dict(enabled=False,stats=[0]*6)))
        for index in range(6):
            stats=[0]*6;stats[index]=1
            with self.assertRaises(RuntimeError):validate_skinned_alpha(dict(skinnedAlpha=dict(enabled=False,stats=stats)))
        for stats in (None,[],[0]*5,[0]*7,[False]*6,[1.0]*6,[-1]*6,[2**64]*6,['0']*6):
            report=self.skinned_report();report['skinnedAlpha']['stats']=stats
            with self.subTest(stats=stats):
                with self.assertRaises(RuntimeError):validate_skinned_alpha(report)
        for value in (None,{},dict(enabled=True),dict(enabled=1,stats=[0]*6)):
            with self.assertRaises(RuntimeError):validate_skinned_alpha(dict(skinnedAlpha=value))

    def test_skinned_alpha_report_requires_explicit_dependencies(self):
        for domain in ('skinning','alphaReference'):
            report=self.skinned_report();report[domain]['enabled']=False
            with self.assertRaises(RuntimeError):validate_skinned_alpha(report)
        report=self.skinned_report();report['alphaOptimized']['enabled']=True
        with self.assertRaises(RuntimeError):validate_skinned_alpha(report)
        report=self.skinned_report();report['nativeTiming']=True
        with self.assertRaises(RuntimeError):validate_skinned_alpha(report)
        report=self.skinned_report();del report['skinning']['stats']
        with self.assertRaises(RuntimeError):validate_skinned_alpha(report)

    def test_skinned_alpha_option_dependencies(self):
        for optimized in (False,True):validate_skinned_alpha_options(True,True,not optimized,optimized)
        for skinning,reference,optimized in ((False,True,False),(False,False,True),(True,False,False),(True,True,True)):
            with self.assertRaises(ValueError):validate_skinned_alpha_options(True,skinning,reference,optimized)
        for mode in ('continuous','native_timing'):
            with self.assertRaises(ValueError):validate_skinned_alpha_options(True,True,True,False,**{mode:True})
        validate_skinned_alpha_options(False,False,False,False,continuous=True,native_timing=True)


if __name__=='__main__':unittest.main()
