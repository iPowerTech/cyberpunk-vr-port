import copy
import unittest
from paired_geometry_report import decode,validate


class PairedGeometryReportTest(unittest.TestCase):
    def report(self):
        camera=[0]*16;camera[0]=1;camera[8]=0x10000;camera[9]=2048;camera[15]=1
        return decode(3,[24,26,1,1,1,1,1,3,128,64,174,1753,647,2,0,1],
                      [0]*12,[128*64*17]*2,camera,[15,12,10,0,0,0]*2)

    def test_complete_reference(self):
        validate(self.report())

    def test_every_required_operation(self):
        for name in ('mainCaptured','peerCaptured','pairedDraws','mainSubmitted','peerSubmitted'):
            for value in (0,2):
                report=self.report();report['stats'][name]=value
                with self.subTest(name=name,value=value),self.assertRaises(RuntimeError):validate(report)
        for value in (0,1,2,4):
            report=self.report();report['stats']['queueProof']=value
            with self.subTest(queue=value),self.assertRaises(RuntimeError):validate(report)

    def test_each_plane_difference(self):
        for plane in range(12):
            report=self.report();report['different'][plane]=1
            with self.subTest(plane=plane),self.assertRaises(RuntimeError):validate(report)

    def test_full_image_bytes(self):
        for eye in range(2):
            report=self.report();report['bytes'][eye]-=1
            with self.subTest(eye=eye),self.assertRaises(RuntimeError):validate(report)

    def test_invalid_state_or_shape(self):
        for key,value in (('reason',1),('mode',2),('width',0),('height',4097),
                          ('mainSequence',1753),('peerSequence',0),('familyId',0),('serializedBatches',0)):
            report=self.report();report['stats'][key]=value
            with self.subTest(key=key),self.assertRaises(RuntimeError):validate(report)
        for value in (0,1,2,4,True):
            report=self.report();report['state']=value
            with self.subTest(state=value),self.assertRaises(RuntimeError):validate(report)

    def test_invalid_abi(self):
        with self.assertRaises(ValueError):decode(3,[0]*15,[0]*12,[0]*2,[0]*16,[0]*12)
        for name in ('different','bytes','cameraDiagnostics','changed'):
            report=self.report();report[name].pop()
            with self.subTest(name=name),self.assertRaises(RuntimeError):validate(report)
        for bad in (-1,2**64,True,1.0):
            report=self.report();report['bytes'][0]=bad
            with self.subTest(value=bad),self.assertRaises(RuntimeError):validate(report)

    def test_no_production_claim(self):
        report=copy.deepcopy(self.report());report['purpose']='production-admitted'
        with self.assertRaises(RuntimeError):validate(report)

    def test_no_blank_equivalence(self):
        for eye in range(2):
            report=self.report();report['changed'][eye*6:eye*6+6]=[0]*6
            with self.subTest(eye=eye),self.assertRaises(RuntimeError):validate(report)
            validate(report,require_contribution=False)
        report=self.report();report['changed'][0]=128*64+1
        with self.assertRaises(RuntimeError):validate(report)
        with self.assertRaises(RuntimeError):validate(report,require_contribution=False)

    def test_pixel_camera_stage_proof(self):
        report=self.report();report['family']=70
        with self.assertRaises(RuntimeError):validate(report)
        report['cameraDiagnostics'][0]=3;validate(report)
        for field,value in ((8,0),(9,1024),(15,0)):
            report=self.report();report['cameraDiagnostics'][field]=value
            with self.subTest(field=field),self.assertRaises(RuntimeError):validate(report)
        for family in (0,81,'70',True):
            report=self.report();report['family']=family
            with self.subTest(family=family),self.assertRaises(RuntimeError):validate(report)


if __name__=='__main__':unittest.main()
