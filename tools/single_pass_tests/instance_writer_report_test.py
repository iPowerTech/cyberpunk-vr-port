import copy
import struct
import unittest
from instance_writer_report import analyze
from ia_snapshot_report_test import fixture as ia_fixture


def fixture():
    sources=ia_fixture()[3]
    rows=[];blob=bytearray()
    for ordinal,source in enumerate(sources['rows']):
        row=[0]*48;row[:3]=[ordinal,source[1],source[2]];row[8:10]=source[10:13:2]
        if not source[2]:
            source[24]=7;source[21]=2;row[3:8]=[1,1,900+ordinal,len(blob)+1,96]
            row[10:15]=[1000+ordinal,0x100000,0x100000,6*1024*1024,48]
            d=[0]*32;d[:17]=[1,31,1,source[6],source[7],ordinal+1,source[1],2000+ordinal,1,row[10],17,5,48,6*1024*1024,0,7,2]
            d[17:23]=[7,1,0,128,0,0];d[25:32]=[96,source[6],source[7],100+ordinal,55,77,96]
            row[16:]=d;blob.extend(bytes([ordinal+1])*96)
        rows.append(row)
    return rows,bytes(blob),sources


def check(data):
    rows,blob,sources=data
    return analyze(b''.join(struct.pack('<48Q',*row) for row in rows),blob,sources)


class InstanceWriterReportTests(unittest.TestCase):
    def test_actual_cpu_payload_does_not_grant_gpu_permissions(self):
        result=check(fixture());self.assertGreater(result['captured'],0)
        self.assertFalse(result['copyPermission']);self.assertFalse(result['sourceGpuBytesMatched'])

    def test_wrong_scope_reservation_bytes_and_owner_are_rejected(self):
        for field,value in ((5,0),(6,99),(7,48),(11,0),(12,0),(11,2**64-1),(19,99),(27,4),(28,64),(31,8),(32,1),(33,6),
                            (36,8),(36,2**64-1),(37,2**64-1),(38,8),(40,999),(47,95)):
            with self.subTest(field=field):
                data=copy.deepcopy(fixture());data[0][0][field]=value
                with self.assertRaises(ValueError):check(data)

    def test_uncovered_queries_do_not_invent_payload(self):
        rows,_,sources=fixture()
        for row in rows:
            row[4:8]=[0]*4
            if row[3]:row[16]=13;row[17]=28
        result=check((rows,b'',sources));self.assertEqual(result['captured'],0)

