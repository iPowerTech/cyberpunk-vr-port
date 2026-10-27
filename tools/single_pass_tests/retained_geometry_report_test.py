import unittest
from retained_geometry_report import FIELDS,decode,launch_modes,validate,validate_options


class RetainedGeometryReportTests(unittest.TestCase):
    def fixture(self,mode='reference'):
        values=[134,136,2,2,1,1,1,1,2,10,4,4,0,0,0,1]
        if mode=='replace':values[12:14]=[4,4];values[15]=2
        return decode(mode,7,3,1,values,[0]*12,[32768,32768] if mode=='reference' else [0,0])

    def test_reference_and_replacement(self):
        for mode in ('reference','replace'):validate(self.fixture(mode),mode,7)
        self.assertEqual(launch_modes('reference'),('reference',))
        self.assertEqual(launch_modes('replace'),('reference','replace'))
        with self.assertRaises(ValueError):launch_modes('unknown')

    def test_missing_or_invalid_counters(self):
        for key in FIELDS:
            report=self.fixture();del report['stats'][key]
            with self.subTest(key=key),self.assertRaises(RuntimeError):validate(report)
        for value in (-1,True,1.5,2**64):
            report=self.fixture();report['stats']['mainDraws']=value
            with self.subTest(value=value),self.assertRaises(RuntimeError):validate(report)
        for key in ('state','verified'):
            for value in (0,1,2,4,True):
                if value==1 and key=='verified' and type(value) is int:continue
                report=self.fixture();report[key]=value
                with self.subTest(key=key,value=value),self.assertRaises(RuntimeError):validate(report)

    def test_both_eyes_and_serialization_required(self):
        changes={key:0 for key in ('mainDraws','peerDraws','mainScopes','peerScopes','mainSealed','peerSealed',
                                   'mainSubmitted','peerSubmitted','serializedBatches','serializedLists')}
        changes.update(mainImported=3,peerImported=3,reason=1,mode=2)
        for key,value in changes.items():
            report=self.fixture();report['stats'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):validate(report)
        for key in ('mainSealed','peerSealed','mainSubmitted','peerSubmitted'):
            report=self.fixture();report['stats'][key]=2
            with self.subTest(key=key),self.assertRaises(RuntimeError):validate(report)
        report=self.fixture();report['stats']['serializedBatches']=11
        with self.assertRaises(RuntimeError):validate(report)

    def test_comparison_and_export_rules(self):
        for mode in ('reference','replace'):
            for plane in range(12):
                report=self.fixture(mode);report['different'][plane]=1
                with self.subTest(mode=mode,plane=plane),self.assertRaises(RuntimeError):validate(report)
            for eye in range(2):
                report=self.fixture(mode);report['bytes'][eye]=0 if mode=='reference' else 1
                with self.subTest(mode=mode,eye=eye),self.assertRaises(RuntimeError):validate(report)
            report=self.fixture(mode);report['stats']['mainExported']=4 if mode=='reference' else 0
            with self.assertRaises(RuntimeError):validate(report)
        for key in ('different','bytes'):
            report=self.fixture();report[key].pop()
            with self.assertRaises(RuntimeError):validate(report)

    def test_request_and_options(self):
        with self.assertRaises(RuntimeError):validate(self.fixture(),'replace',7)
        with self.assertRaises(RuntimeError):validate(self.fixture(),'reference',6)
        for mode in ('reference','replace'):
            for stage in (6,7):validate_options(mode,stage,pass_targets=True,paired_apply=True)
            for targets,paired,conflicts in ((False,True,()),(True,False,()),(True,True,(True,))):
                with self.assertRaises(ValueError):validate_options(mode,7,pass_targets=targets,paired_apply=paired,conflicts=conflicts)
        validate_options(None,7,pass_targets=False,paired_apply=False)
        with self.assertRaises(ValueError):validate_options(None,6,pass_targets=False,paired_apply=False)
        with self.assertRaises(ValueError):decode('reference',7,3,1,[0]*15,[0]*12,[1,1])

    def test_optional_boundary_diagnostics(self):
        validate(self.fixture())
        for stage in (6,7):
            report=self.fixture();report['stage']=stage
            report['boundaries']=[0,2**64-1,1<<stage,(1<<stage)|(1<<9)]
            validate(report)
            for eye in (2,3):
                changed=dict(report,boundaries=report['boundaries'].copy());changed['boundaries'][eye]=1<<9
                with self.subTest(stage=stage,eye=eye),self.assertRaises(RuntimeError):validate(changed)
        for invalid in (None,(),[],[1,2,128],[-1,2,128,128],[1,True,128,128],[1,2,128.0,128],[1,2,128,2**64]):
            report=self.fixture();report['boundaries']=invalid
            with self.subTest(invalid=invalid),self.assertRaises(RuntimeError):validate(report)
        report=self.fixture();ends=[3,4,128,640]
        decoded=decode(report['mode'],report['stage'],report['state'],report['verified'],list(report['stats'].values()),
                       report['different'],report['bytes'],ends)
        self.assertEqual(decoded['boundaries'],ends);validate(decoded)

    def test_optional_indirect_command_counts(self):
        validate(self.fixture())
        for mode in ('reference','replace'):
            report=self.fixture(mode)
            for counts in ([0,0],[1,2],[report['stats']['mainDraws'],report['stats']['peerDraws']]):
                report['indirect']=counts;validate(report)
            for counts in ([report['stats']['mainDraws']+1,0],[0,report['stats']['peerDraws']+1]):
                report['indirect']=counts
                with self.subTest(mode=mode,counts=counts),self.assertRaises(RuntimeError):validate(report)
        for counts in (None,(),[],[1],[1,2,3],[-1,0],[True,0],[0,1.5],[0,2**64]):
            report=self.fixture();report['indirect']=counts
            with self.subTest(counts=counts),self.assertRaises(RuntimeError):validate(report)
        report=self.fixture()
        decoded=decode(report['mode'],report['stage'],report['state'],report['verified'],list(report['stats'].values()),
                       report['different'],report['bytes'],indirect=[3,4])
        self.assertEqual(decoded['indirect'],[3,4]);validate(decoded)

    def test_optional_indirect_rejection_diagnostics(self):
        validate(self.fixture())
        for mode in ('reference','replace'):
            for values in ([0]*8,[20,1,24,0,9,1,1,4],[2**64-1]*8):
                report=self.fixture(mode);report['indirectRejected']=values
                validate(report)
        for values in (None,(),[],[0]*7,[0]*9,[-1]+[0]*7,[True]+[0]*7,[1.5]+[0]*7,[2**64]+[0]*7):
            report=self.fixture();report['indirectRejected']=values
            with self.subTest(values=values),self.assertRaises(RuntimeError):validate(report)
        report=self.fixture();values=[20,1,24,0,9,1,1,4]
        decoded=decode(report['mode'],report['stage'],report['state'],report['verified'],list(report['stats'].values()),
                       report['different'],report['bytes'],indirect_rejected=values)
        self.assertEqual(decoded['indirectRejected'],values);validate(decoded)


if __name__=='__main__':unittest.main()
