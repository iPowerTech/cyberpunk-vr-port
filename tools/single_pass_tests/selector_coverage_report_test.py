import struct
import unittest
from selector_coverage_report import analyze
from source_selector_capture_report_test import fixture


def data():
    source=fixture()[6];inputs=[];cbvs=[];rows=[]
    for index,s in enumerate(source['rows']):
        cbv=[0]*24;cbv[:7]=[index,s[1],5,4,19,0,228];cbv[10],cbv[12],cbv[13]=0x100000+512,300+index,0x100000
        cbvs.append(cbv)
        inp=[0]*24;inp[:7]=cbv[:7];inp[7]=1;inputs.append(inp)
        d=[0]*24;d[:14]=[0x47434F5630303031,cbv[12],1,1,1,240,512,228,1,1,228,2**64-1,1,1]
        d[19:24]=[3,1,123,456,1];rows.append([index,index,s[1],1,*d])
    return source,dict(verified=True,rows=cbvs),dict(verified=True,inputs=inputs),rows


def check(values):
    source,cbvs,selectors,rows=values
    return analyze(b''.join(struct.pack('<28Q',*row) for row in rows),source,cbvs,selectors)


class SelectorCoverageTests(unittest.TestCase):
    def test_separate_small_copy_capacity_is_bounded_metadata_only(self):
        values=data();values[3][0][8]=8704
        self.assertFalse(check(values)['copyPermission'])
        values[3][0][8]=8705
        with self.assertRaises(ValueError):check(values)

    def test_current_covering_copy_and_explicit_missing_hole(self):
        values=data();result=check(values)
        self.assertEqual(result['singleCovered'],4)
        self.assertFalse(result['copyPermission'])
        values[2]['inputs'][0][7]=0
        values[3][0][12:16]=[0,0,0,512]
        self.assertEqual(check(values)['uncovered'],1)

    def test_wrong_resource_span_counts_and_hidden_hole_reject(self):
        for field,value in ((0,99),(1,99),(3,2),(4,0),(5,0),(10,513),(11,229),(13,2),(14,229),(15,512),(23,9),(27,2)):
            with self.subTest(field=field):
                values=data();values[3][0][field]=value
                with self.assertRaises(ValueError):check(values)


if __name__=='__main__':unittest.main()
