import copy
import struct
import unittest
from instance_gpu_report_test import fixture as gpu_fixture,check as gpu_check
from instance_overlay_report import analyze


def fixture():
    original=gpu_fixture();report=gpu_check(original);sources=original[4];writers=original[3];rows=[];cursor=0;width=0
    for item in report['rows']:
        if item[5]!=1:continue
        source=sources['rows'][item[0]];cpu=writers['rows'][item[0]];stride=cpu[14];logical=source[24]*stride
        target=max(((cursor+stride-1)//stride)*stride,logical);view_offset=target-logical
        rows.append([item[0],source[1],len(rows),source[11],source[6],source[7],item[0]+1,source[10],source[12],
                     cpu[12],cpu[13],stride,9000,9001,0x900000+view_offset,target,item[11],0,1,1,1,item[18],9002,1])
        cursor=target+item[11];width=max(width,view_offset+cpu[13])
    for row in rows:row[17]=width
    stats=[2,len(rows),len(original[1]),width,1,1,0,0]
    return stats,rows,original[1],original[1],report,sources,writers


def check(data):
    stats,rows,*rest=data
    return analyze(stats,b''.join(struct.pack('<24Q',*r) for r in rows),*rest)


def native_fixture():
    original=gpu_fixture();report=gpu_check(original);sources=original[4];writers=original[3]
    rows=[];cursors=[0,0];widths=[0,0];versions=[0,0];copied=0
    for item in report['rows']:
        if not item[2]:continue
        source=sources['rows'][item[0]];cpu=writers['rows'][item[0]];eye=source[1];stride=cpu[14];logical=source[24]*stride
        target=max(((cursors[eye]+stride-1)//stride)*stride,logical);view_offset=target-logical
        rows.append([item[0],eye,versions[eye],source[11],source[6],source[7],item[0]+1,source[10],source[12],
                     cpu[12],cpu[13],stride,9000+eye,9100+eye,0x900000+eye*0x1000000+view_offset,target,item[11],0,
                     1,1,1,item[18],source[6],source[7]])
        cursors[eye]=target+item[11];widths[eye]=max(widths[eye],view_offset+cpu[13]);versions[eye]+=1;copied+=item[11]
    for row in rows:row[17]=widths[row[1]]
    stats=[2,len(rows),copied,sum(widths),1,1,0,0]
    return stats,rows,original[1],original[1],report,sources,writers


def native_check(data):
    stats,rows,*rest=data
    return analyze(stats,b''.join(struct.pack('<24Q',*r) for r in rows),*rest,mode=2)


class InstanceOverlayTests(unittest.TestCase):
    def test_original_native_recordings_supply_direct_gpu_versions(self):
        result=native_check(native_fixture())
        self.assertEqual(result['mode'],'native-original')
        self.assertEqual(result['extraCopySubmissions'],0)
        self.assertEqual(result['instanceCpuUploadBytes'],0)
        self.assertEqual(result['allocatedBytes'],64*1024*1024)
        self.assertLess(result['viewExtentBytes'],result['allocatedBytes'])
        self.assertTrue(result['gpuBytesMatched'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_native_copy_cannot_borrow_later_list_or_other_eye_arena(self):
        for field,value in ((2,99),(6,99),(8,99),(9,1),(10,32),(11,64),(12,0),(13,0),(14,1),(15,0),(16,48),
                            (17,0),(18,0),(19,0),(20,0),(21,99),(22,9002),(23,99)):
            with self.subTest(field=field):
                data=copy.deepcopy(native_fixture());data[1][0][field]=value
                with self.assertRaises(ValueError):native_check(data)
        for field,value in ((1,0),(2,0),(3,0),(4,0),(5,0),(6,1),(7,1)):
            data=copy.deepcopy(native_fixture());data[0][field]=value
            with self.subTest(stat=field),self.assertRaises(ValueError):native_check(data)
        data=list(native_fixture());data[2]=bytes([data[2][0]^1])+data[2][1:]
        with self.assertRaises(ValueError):native_check(data)

    def test_original_coordinates_and_gpu_versions_match(self):
        result=check(fixture());self.assertTrue(result['gpuBytesMatched']);self.assertFalse(result['privateIaConsumed'])

    def test_torn_mapping_owner_payload_and_fence_reject(self):
        for field,value in ((6,99),(8,99),(9,1),(10,32),(11,64),(12,0),(13,0),(14,1),(15,0),(16,48),(17,0),(18,0),(19,0),(20,0),(21,99),(22,0),(23,0)):
            with self.subTest(field=field):
                data=copy.deepcopy(fixture());data[1][0][field]=value
                with self.assertRaises(ValueError):check(data)
        data=list(fixture());data[2]=bytes([data[2][0]^1])+data[2][1:]
        with self.assertRaises(ValueError):check(data)
