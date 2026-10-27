import copy
import struct
import unittest
from ia_snapshot_report_test import fixture as ia_fixture,check as ia_check
from private_geometry_report import analyze


def fixture():
    original=ia_fixture();ia=ia_check(original);sources=original[3];rows=[]
    for index,(source,command) in enumerate(zip(sources['rows'],ia['commands'])):
        rows.append([index,source[1],1,source[19],source[6],source[7],index+1,command[13],command[8],0,0,0,source[24],source[21],0,0])
    gpu=dict(verified=True,rows=[[i,s[1],0,0,0,0] for i,s in enumerate(sources['rows'])])
    overlay=dict(verified=True,rows=[])
    return rows,sources,ia,gpu,overlay


def check(data):
    rows,*rest=data
    return analyze(b''.join(struct.pack('<16Q',*row) for row in rows),*rest)


class PrivateGeometryTests(unittest.TestCase):
    def test_whole_ia_has_no_native_draw_claim(self):
        report=check(fixture());self.assertEqual(report['ready'],4);self.assertEqual(report['nativeDrawsRecorded'],0)

    def test_compact_join_requires_same_source_and_completed_gpu_version(self):
        data=fixture();data[2]['selections'][0][2]=128;data[2]['selections'][0][4]=0
        data[3]['rows'][0][2]=data[3]['rows'][0][5]=1
        view=[0]*24;view[12:15]=[8000,8001,0x900000];view[18:21]=[1,1,1];data[4]['rows']=[view]
        data[0][0][9:12]=view[12:15]
        self.assertEqual(check(data)['compactInstanceJoins'],1)
        for group,field,value in ((0,7,0),(0,10,8002),(3,5,0),(4,19,0)):
            with self.subTest(group=group,field=field):
                wrong=copy.deepcopy(data)
                if group==0:wrong[0][0][field]=value
                else:wrong[group]['rows'][0][field]=value
                with self.assertRaises(ValueError):check(wrong)
        data[4]['rows']=[]
        with self.assertRaises(ValueError):check(data)
        data[0][0][2]=0;data[0][0][7:14]=[0]*7
        self.assertEqual(check(data)['ready'],3)
