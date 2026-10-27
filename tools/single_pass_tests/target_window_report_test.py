import struct
import unittest
from target_window_report import read_report


class TargetWindowReportTests(unittest.TestCase):
    def fixture(self):
        prefix='CyberpunkVR_TargetAccess'
        return {prefix+'Seq':struct.pack('<I',4),prefix+'Stats':struct.pack('<16Q',1,*([0]*15)),
                prefix+'Sources':bytes(256),prefix+'Checks':bytes(256),'CyberpunkVR_RetainedTargetLifetime':bytes(1024)}

    def test_observation_not_permission(self):
        exports=self.fixture();report=read_report(lambda name,size:exports[name])
        self.assertEqual(len(report['sources']),2);self.assertEqual(len(report['lifetime']),8)
        self.assertFalse(report['exportPermission']);self.assertFalse(report['nativeSuppressionPermission'])

    def test_malformed_arrays_and_permission(self):
        exports=self.fixture()
        for name in exports:
            bad=dict(exports);bad[name]=bad[name][:-1]
            with self.subTest(name=name),self.assertRaises(ValueError):read_report(lambda key,size:bad[key])
        for slot,value in ((0,9),(4,257),(5,2049),(6,32769),(7,8193),(15,1)):
            values=[1]+[0]*15;values[slot]=value;bad=dict(exports)
            bad['CyberpunkVR_TargetAccessStats']=struct.pack('<16Q',*values)
            with self.subTest(slot=slot),self.assertRaises(ValueError):read_report(lambda key,size:bad[key])

    def test_odd_publication(self):
        exports=self.fixture();exports['CyberpunkVR_TargetAccessSeq']=struct.pack('<I',3)
        with self.assertRaises(ValueError):read_report(lambda key,size:exports[key],attempts=2)

    def test_version_two_failure_event_is_required_and_kept_observational(self):
        exports=self.fixture();exports['CyberpunkVR_TargetAccessStats']=struct.pack('<16Q',2,*([0]*15))
        values=(1,123,456,78,6,13,900,0)
        exports['CyberpunkVR_TargetAccessFailureEvent']=struct.pack('<8Q',*values)
        report=read_report(lambda key,size:exports[key])
        self.assertEqual(report['failureEvent']['tag'],13)
        self.assertEqual(report['raw']['failureEvent'],list(values))
        self.assertFalse(report['exportPermission'])
        exports['CyberpunkVR_TargetAccessFailureEvent']=bytes(63)
        with self.assertRaises(ValueError):read_report(lambda key,size:exports[key])

    def test_version_three_breadcrumb_counts_are_required(self):
        exports=self.fixture();exports['CyberpunkVR_TargetAccessStats']=struct.pack('<16Q',3,*([0]*15))
        exports['CyberpunkVR_TargetAccessFailureEvent']=bytes(64)
        exports['CyberpunkVR_TargetAccessBreadcrumbStats']=struct.pack('<8Q',4,3,1,0,0,9,128,256)
        report=read_report(lambda key,size:exports[key])
        self.assertEqual(report['breadcrumbs']['resolved'],3)
        self.assertEqual(report['breadcrumbs']['fallbackUnknown'],1)
        self.assertFalse(report['exportPermission'])
        exports['CyberpunkVR_TargetAccessBreadcrumbStats']=bytes(63)
        with self.assertRaises(ValueError):read_report(lambda key,size:exports[key])


if __name__=='__main__':unittest.main()
