import copy
import struct
import unittest

from graph_parent_report import fingerprint,name_hash
from owned_execution_report import decode_schedule
from owned_execution_report_test import complete_receipt
from resource_order_report import graph
from whole_gbuffer_report import _prefix

BASE=0x140000000


def encode(cpu,ordered):
    nodes=[]
    for i in range(10):
        sync=i in (1,3,6,9)
        table=BASE+(0x312DD90 if i==5 else 0x2B3A038 if sync else 0x2B3A008)
        work=BASE+(0x79B03C if i==5 else 0x58DA9C if sync else 0x1EC1D0)
        tag=name_hash('GBuffer_Discard') if i in (2,7) else name_hash('RenderFinal2D') if i==4 else 2 if sync else 0
        data=bytearray(48)
        struct.pack_into('<I4xI4xQII',data,0,i,4,0x100000+i*64,int(i>=5),0)
        struct.pack_into('<4I',data,32,*([0x80000000]*4))
        nodes.append((data,table,work,struct.pack('<Q16xQ',table,tag)))
    edges=[]
    for phase,pairs in enumerate((cpu,ordered)):
        levels={i:0 for i in range(10)};degree={i:0 for i in levels};children={i:[] for i in levels}
        for a,b in pairs:degree[b]+=1;children[a].append(b)
        pending=[i for i in levels if not degree[i]]
        for at in pending:
            for child in children[at]:
                levels[child]=max(levels[child],levels[at]+1);degree[child]-=1
                if not degree[child]:pending.append(child)
        assert len(pending)==10
        for rank,i in enumerate(sorted(levels,key=lambda i:(levels[i],i))):nodes[i][0][28+phase]=rank
        for a,b in sorted(pairs):
            index=len(edges);handle=index|0x40000000
            outgoing=struct.unpack_from('<I',nodes[a][0],40+phase*4)[0]
            incoming=struct.unpack_from('<I',nodes[b][0],32+phase*4)[0]
            edges.append(struct.pack('<8I',handle,index,index,phase,a|0x20000000,b|0x20000000,outgoing,incoming))
            struct.pack_into('<I',nodes[a][0],40+phase*4,handle);struct.pack_into('<I',nodes[b][0],32+phase*4,handle)
    return b''.join(struct.pack('<II48sQQ32s',i,i,bytes(d),t,w,s) for i,(d,t,w,s) in enumerate(nodes)),b''.join(edges)


def fixture():
    original={(i,i+1) for i in range(9)}
    ordered=original-{(1,2),(4,5),(7,8)}|{(1,5),(7,2),(4,8)}
    cpu=ordered|{(1,2)}
    source_nodes,source_edges=encode(original,original);actual_nodes,actual_edges=encode(cpu,ordered)
    parsed,_,_=graph(actual_nodes,actual_edges,BASE)
    _,_,required,missing=_prefix(parsed['nodes'],cpu,{n['id']:n['phaseOrder'][1] for n in parsed['nodes']},BASE)
    assert not missing
    stats,resources=complete_receipt();stats[8:10]=[10,18]
    stats[13]=resources[5]=fingerprint(source_nodes,source_edges,BASE);resources[6]=fingerprint(actual_nodes,actual_edges,BASE)
    a=list(struct.iter_unpack('<II48sQQ32s',source_nodes));b=list(struct.iter_unpack('<II48sQQ32s',actual_nodes))
    changed=[sum(x[2][28+p]!=y[2][28+p] for x,y in zip(a,b)) for p in range(2)]
    schedule=[2,stats[1],stats[13],resources[6],10,18,19,*changed,required,required,0,0,0,1,1]
    return [schedule,stats,resources,source_nodes,source_edges,actual_nodes,actual_edges,BASE]


class OwnedScheduleTests(unittest.TestCase):
    def test_current_bindings_must_match_both_graphs_at_declaration(self):
        data=fixture();bindings=bytearray(data[3]);actual=bytearray(data[5])
        struct.pack_into('<Q',bindings,96,12345);struct.pack_into('<Q',actual,96,12345)
        data[5]=bytes(actual);data[0][3]=data[2][6]=fingerprint(data[5],data[6],BASE)
        self.assertEqual(decode_schedule(*data,bindings=bytes(bindings),coupled=True)['mode'],'coupled')
        with self.assertRaises(ValueError):decode_schedule(*data,bindings=data[3],coupled=True)
        with self.assertRaises(ValueError):decode_schedule(*data,bindings=b'',coupled=True)
        bindings[16]^=1
        with self.assertRaises(ValueError):decode_schedule(*data,bindings=bytes(bindings),coupled=True)

    def test_exact_coupled_frontier_keeps_every_draw_node(self):
        data=fixture();result=decode_schedule(*data,bindings=data[3],coupled=True)
        self.assertEqual(result['mode'],'coupled')
        self.assertEqual(result['nativeDrawSuppression'],0)
        self.assertFalse(result['sharedScenePreparationProved'])
        self.assertFalse(result['performanceGainProved'])

    def test_torn_or_unapplied_coupled_schedule_is_rejected(self):
        for field in range(16):
            data=fixture();data[0][field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):decode_schedule(*data,bindings=data[3],coupled=True)
        data=fixture()
        with self.assertRaises(ValueError):decode_schedule(*data,bindings=data[3],coupled=False)
        for offset in (0,8,16,64,72,96):
            bad=copy.deepcopy(data);raw=bytearray(bad[5]);raw[offset]^=1;bad[5]=bytes(raw)
            with self.subTest(offset=offset),self.assertRaises(ValueError):decode_schedule(*bad,bindings=data[3],coupled=True)


if __name__=='__main__':unittest.main()
