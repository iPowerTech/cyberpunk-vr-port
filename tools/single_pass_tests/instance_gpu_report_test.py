import copy
import struct
import unittest
from instance_writer_report_test import fixture as cpu_fixture,check as cpu_check
from instance_gpu_report import analyze


def fixture():
    cpu,blob,sources=cpu_fixture();rows=[];offsets=[0,0];gpu=bytearray()
    for source,writer in zip(sources['rows'],cpu):
        row=[0]*24;row[:2]=writer[:2]
        if writer[4]:
            writer[15]=1;row[2:6]=[1,1,1,1]
            row[6:18]=[800,900,writer[10],writer[12]-writer[11]+source[24]*writer[14],offsets[source[1]],writer[7],source[0]*8,
                         source[6],source[7],source[0]+1,writer[5],writer[6]]
            offsets[source[1]]+=writer[7];row[18:22]=[len(gpu)+1,1000,1,1]
            gpu.extend(blob[writer[6]-1:writer[6]-1+writer[7]])
        rows.append(row)
    return rows,bytes(gpu),blob,cpu_check((cpu,blob,sources)),sources


def check(data):
    rows,*rest=data
    return analyze(b''.join(struct.pack('<24Q',*r) for r in rows),*rest)


class InstanceGpuTests(unittest.TestCase):
    def test_payload_eye_order_is_independent_of_source_draw_order(self):
        data=list(fixture());original=data[1];accepted=[r for r in data[0] if r[4]];blob=bytearray()
        for row in reversed(accepted):
            start=row[18]-1;row[18]=len(blob)+1;blob.extend(original[start:start+row[11]])
        data[1]=bytes(blob)
        self.assertEqual(check(data)['matched'],len(accepted))
        bad=copy.deepcopy(data);bad[0][accepted[0][0]][18]=1
        with self.assertRaises(ValueError):check(bad)
        bad=copy.deepcopy(data);bad[1]+=b'\0'
        with self.assertRaises(ValueError):check(bad)

    def test_exact_gpu_bytes_are_separate_from_replay_permission(self):
        result=check(fixture());self.assertGreater(result['matched'],0);self.assertEqual(result['mismatched'],0)
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_mismatch_is_reported_not_hidden(self):
        data=list(fixture());data[1]=bytes([data[1][0]^1])+data[1][1:];data[0][0][5]=2
        self.assertEqual(check(data)['mismatched'],1)
        data[0][0][5]=1
        with self.assertRaises(ValueError):check(data)

    def test_foreign_source_predicate_submission_and_fence_reject(self):
        for field,value in ((3,0),(4,0),(6,0),(8,0),(9,0),(10,9),(11,48),(12,8),(14,99),(16,0),(17,99),(18,99),(19,0),(20,0),(21,0),(22,1)):
            with self.subTest(field=field):
                data=copy.deepcopy(fixture());data[0][0][field]=value
                with self.assertRaises(ValueError):check(data)
