import copy
import struct
import unittest
from graph_parent_report import fingerprint
from whole_gbuffer_report import analyze,_prefix
from resource_order_report import graph as decode_graph

BASE=0x140000000


def fixture():
    original={(i,i+1) for i in range(9)}
    owned=original-{(1,2),(4,5),(7,8)}|{(1,5),(7,2),(4,8)}
    def encode(cpu,ordered):
        nodes=[]
        for i in range(10):
            data=bytearray(48);sync=i in (1,3,6,9)
            struct.pack_into('<I',data,0,i);struct.pack_into('<I',data,8,4)
            struct.pack_into('<Q',data,16,0x100000+i*64);struct.pack_into('<I',data,24,int(i>=5))
            struct.pack_into('<4I',data,32,*([0x80000000]*4))
            table=BASE+(0x2b3a038 if sync else 0x2b3a008);work=BASE+(0x58da9c if sync else 0x1ec1d0)
            definition=struct.pack('<Q16xI4x',table,2 if sync else 7)
            nodes.append((data,table,work,definition))
        edges=[]
        for phase,pairs in enumerate((cpu,ordered)):
            level={i:0 for i in range(10)};degree={i:0 for i in level};next_={i:[] for i in level}
            for a,b in pairs:degree[b]+=1;next_[a].append(b)
            pending=[i for i in level if not degree[i]]
            for at in pending:
                for child in next_[at]:
                    level[child]=max(level[child],level[at]+1);degree[child]-=1
                    if not degree[child]:pending.append(child)
            for rank,i in enumerate(sorted(level,key=lambda i:(level[i],i))):nodes[i][0][28+phase]=rank
            for a,b in sorted(pairs):
                index=len(edges);handle=index|0x40000000
                outgoing=struct.unpack_from('<I',nodes[a][0],40+phase*4)[0]
                incoming=struct.unpack_from('<I',nodes[b][0],32+phase*4)[0]
                edges.append(struct.pack('<8I',handle,index,index,phase,a|0x20000000,b|0x20000000,outgoing,incoming))
                struct.pack_into('<I',nodes[a][0],40+phase*4,handle);struct.pack_into('<I',nodes[b][0],32+phase*4,handle)
        raw_nodes=b''.join(struct.pack('<II48sQQ32s',i,i,bytes(data),table,work,definition) for i,(data,table,work,definition) in enumerate(nodes))
        return raw_nodes,b''.join(edges)
    source_nodes,source_edges=encode(original,original);owned_nodes,owned_edges=encode(owned,owned)
    phase_nodes,phase_edges=encode(original,owned)
    stats=[3,1,fingerprint(source_nodes,source_edges,BASE),10,9,9,9,9,4,2,1,3,3,7,8,0]
    contexts=[];witnesses=[]
    for scenario,(nodes,edges,cpu) in enumerate(((source_nodes,source_edges,original),(phase_nodes,phase_edges,original),(owned_nodes,owned_edges,owned))):
        parsed,_,_=decode_graph(nodes,edges,BASE);slots={n['id']:n['phaseOrder'][1] for n in parsed['nodes']}
        publishers,syncs,required,missing=_prefix(parsed['nodes'],cpu,slots,BASE)
        contexts.extend([6 if missing else 0,fingerprint(nodes,edges,BASE),publishers,syncs,required,required-len(missing),len(missing),0])
        witnesses.extend(struct.pack('<6I',scenario,*row) for row in missing)
    return [stats,contexts,b''.join(witnesses),source_nodes,source_edges,owned_nodes,owned_edges,BASE]


class WholeGBufferReportTests(unittest.TestCase):
    def test_ready_coupled_candidate_preserves_nonexecution_boundary(self):
        report=analyze(*fixture())
        self.assertEqual(report['stages'],[7,9]);self.assertGreater(report['orderOnly']['missing'],0)
        self.assertEqual(report['source']['missing'],0);self.assertEqual(report['coupled']['missing'],0)
        for name in ('executionRedirected','resourceAliasingProved','sharedScenePreparationProved','performanceGainProved'):
            self.assertIs(report[name],False)

    def test_stale_or_incomplete_stats_are_rejected(self):
        for index,value in ((0,1),(1,0),(2,1),(3,9),(4,10),(6,10),(11,0),(12,0),(13,6),(15,1)):
            args=fixture();args[0][index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):analyze(*args)

    def test_native_prefix_counts_are_independently_recomputed(self):
        for scenario in range(3):
            for field in (0,2,3,4,5,6,7):
                args=fixture();args[1][scenario*8+field]+=1
                with self.subTest(scenario=scenario,field=field),self.assertRaises(ValueError):analyze(*args)

    def test_each_missing_publisher_and_sync_witness_is_required(self):
        args=fixture();self.assertTrue(args[2])
        for raw in (b'',args[2][24:],args[2]+args[2][:24],args[2][:-1]):
            bad=copy.deepcopy(args);bad[2]=raw
            with self.subTest(bytes=len(raw)),self.assertRaises(ValueError):analyze(*bad)

    def test_native_definition_identity_cannot_change(self):
        for offset in (8,16,64,72):
            args=fixture();raw=bytearray(args[5]);raw[offset]^=1;args[5]=bytes(raw)
            with self.subTest(offset=offset),self.assertRaises(ValueError):analyze(*args)


if __name__=='__main__':unittest.main()
