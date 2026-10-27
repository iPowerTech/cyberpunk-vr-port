import struct
import unittest
from graph_plan import decode_graph,dependency_path


class GraphPlanTests(unittest.TestCase):
    def fixture(self):
        base=0x140000000
        def node(slot,order,view,work):
            raw=struct.pack('<4IQ6I',order,0,3,0,0x1000,view,0x0102,*([0x80000000]*4))
            return struct.pack('<II48sQQ32s',slot,order,raw,0x2000,base+work,bytes(32))
        nodes=node(4,0,0,0x1EC1D0)+node(1,1,1,0xB2BEFC)
        edge=struct.pack('<II6I',3,0,0,1,0x20000004,0x20000001,0x80000000,0x80000000)
        return nodes,edge,base

    def test_sparse_handles_and_direction(self):
        graph=decode_graph(*self.fixture())
        self.assertEqual(graph['summary']['invalidEndpointEdges'],[])
        self.assertEqual(graph['nodes'][0]['phaseOrder'],[2,1])
        self.assertEqual(graph['summary']['culling'][0]['view'],1)
        self.assertEqual(dependency_path(graph,4,1,1),[4,1])
        self.assertEqual(dependency_path(graph,1,4,1),[])
        self.assertEqual(dependency_path(graph,4,1,0),[])

    def test_invalid_data_is_not_accepted(self):
        nodes,edges,base=self.fixture()
        with self.assertRaises(ValueError):decode_graph(nodes[:-1],edges,base)
        with self.assertRaises(ValueError):decode_graph(nodes+nodes[:104],edges,base)


if __name__=='__main__':unittest.main()
