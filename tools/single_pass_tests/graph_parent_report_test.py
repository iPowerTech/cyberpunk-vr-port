import copy
import struct
import unittest
from graph_parent_report import analyze,fingerprint,frontier,name_hash


class GraphParentReportTest(unittest.TestCase):
    def fixture(self):
        graph=dict(nodes=[]);events=[];rows=[]
        for side in (0,1):
            for stage,name in ((3,'DepthPrepass'),(6,'GBuffer_Solid'),(7,'GBuffer_Discard'),(9,'GBuffer_Discard')):
                ordinal=len(rows);scope=ordinal+1;word=name_hash(name);object_=0x20000+ordinal*32;work=0x1401ec1d0
                graph['nodes'].append(dict(id=ordinal,ordinal=ordinal,view=side,phaseOrder=[ordinal,ordinal],object=hex(object_),
                                           work=hex(work),definitionWords=[0]*6+[word&0xffffffff,word>>32]))
                events.append(dict(kind=13,scope=scope,frame=1,side=side,stage=stage,context=0x30000+ordinal*64))
                rows.append([scope,events[-1]['context'],0x50000,0xabc,object_,work,word,ordinal,ordinal,side,ordinal,0,stage])
        raw=b''.join(struct.pack('<7Q6I',*row) for row in rows)
        return raw,graph,events,rows

    def test_exact_parent_join(self):
        raw,graph,events,_=self.fixture();result=analyze(raw,graph,events,0xabc)
        self.assertTrue(result['completeGeometryParents']);self.assertEqual(result['statuses'],{0:8})

    def test_each_identity_field(self):
        raw,graph,events,rows=self.fixture()
        for field in (1,3,4,5,6,7,8,9,10,12):
            changed=copy.deepcopy(rows);changed[0][field]+=1
            bad=b''.join(struct.pack('<7Q6I',*row) for row in changed)
            with self.subTest(field=field),self.assertRaises(ValueError):analyze(bad,graph,events,0xabc)

    def test_duplicates_and_abi(self):
        raw,graph,events,_=self.fixture()
        with self.assertRaises(ValueError):analyze(raw[:-1],graph,events,0xabc)
        with self.assertRaises(ValueError):analyze(raw+raw[:80],graph,events,0xabc)

    def test_unknown_is_not_parent_proof(self):
        _,graph,events,rows=self.fixture();rows[0][11]=3
        result=analyze(b''.join(struct.pack('<7Q6I',*row) for row in rows),graph,events,0xabc)
        self.assertFalse(result['completeGeometryParents']);self.assertEqual(len(result['unknown']),1)

    def test_frontier_not_executable(self):
        stats=[1,0,0,0xabc,155,809,119,809,119,6,23,21,5,3,3,255]
        report=frontier(stats,list(range(22)),[25,26,57,88,108,109],[25,88,108,26,57,109],0xabc)
        self.assertFalse(report['executionReady']);self.assertEqual(len(report['cuts']),3)
        for field,value in ((0,0),(1,2),(2,1),(3,0xabd),(13,2),(14,4),(15,511)):
            changed=stats.copy();changed[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):frontier(changed,list(range(22)),[0]*6,[0]*6,0xabc)

    def test_native_fingerprint_changes(self):
        raw=struct.pack('<II48sQQ32s',1,0,bytes(48),0x140001000,0x1401ec1d0,bytes(32))
        first=fingerprint(raw,b'',0x140000000)
        self.assertNotEqual(first,fingerprint(raw,b'',0x140000001))
        changed=bytearray(raw);changed[32]^=1
        self.assertNotEqual(first,fingerprint(bytes(changed),b'',0x140000000))
        self.assertEqual(name_hash('Lighting'),0xda843b18b409bfa9)


if __name__=='__main__':unittest.main()
