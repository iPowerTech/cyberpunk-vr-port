import struct
import unittest
from graph_parent_report import fingerprint
from definition_source_report import validate


class DefinitionSourceReportTests(unittest.TestCase):
    def fixture(self):
        nodes=[]
        for i,obj in enumerate((0x90000,0x90000,0)):
            raw=bytearray(48);struct.pack_into('<I',raw,0,i);struct.pack_into('<Q',raw,16,obj)
            nodes.append(struct.pack('<II48sQQ32s',i,i,raw,0,0,bytes(32)))
        nodes=b''.join(nodes);base=0x140000000;fp=fingerprint(nodes,b'',base)
        stats=[3,1,2,3,2,fp,0x30000,0x10010,0x10000,0x10000,1,5,8,0x219e09,1,0]
        owner=struct.pack('<6Q4I',0x50000,0x50070,0x60000,0x70000,9,0,4,3,0,1)
        mapping=b''.join(struct.pack('<3I4xQ',i,0,0,0x90000) for i in range(2))
        entries=struct.pack('<3Q',0x90000,0,0);jobs=[3,3,0x30000,0x10010,0,1,0,fp]
        return [stats,owner,mapping,entries,nodes,b'',base,jobs,5,[1,0x80000,0x30000,0x10010,5,8,1,1]]

    def test_exact_mapping_keeps_shared_definitions_and_null_slots(self):
        report=validate(*self.fixture())
        self.assertEqual((report['mappedNodes'],report['uniqueMappedDefinitions'],report['sharedMappedDefinitions'],report['syntheticNullSlots']),(2,1,1,2))
        for field in ('allPriorConsumersAccounted','definitionOwnershipProved','nativeExclusiveProved','executionPermitted'):
            self.assertFalse(report[field])

    def test_epoch_process_owner_graph_and_source_identity_are_required(self):
        for index,value in ((0,2),(5,1),(6,0x30100),(7,0x10100),(9,0x10200),(11,4),(13,0x219e08),(14,0),(15,1)):
            args=self.fixture();args[0][index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):validate(*args)

    def test_binary_bounds_and_complete_mapping_are_required(self):
        for i in (1,2,3,4):
            args=self.fixture();args[i]+=b'x'
            with self.subTest(index=i),self.assertRaises(ValueError):validate(*args)
        args=self.fixture();args[0][2]=1;args[2]=args[2][:24]
        with self.assertRaises(ValueError):validate(*args)

    def test_generation_mutation_and_wrong_definition_index_are_rejected(self):
        for offset,value in ((8,0x50080),(32,0),(40,1),(56,1),(60,2)):
            args=self.fixture();owner=bytearray(args[1]);struct.pack_into('<Q' if offset<48 else '<I',owner,offset,value);args[1]=bytes(owner)
            with self.subTest(offset=offset),self.assertRaises(ValueError):validate(*args)
        args=self.fixture();mapping=bytearray(args[2]);struct.pack_into('<I',mapping,8,1);args[2]=bytes(mapping)
        with self.assertRaises(ValueError):validate(*args)

    def test_native_ticket_transfer_and_queue_match_required(self):
        for at,value in ((0,0),(1,0),(2,0x30100),(3,0x10100),(4,4),(5,7),(6,0),(7,0)):
            args=self.fixture();args[9][at]=value
            with self.subTest(index=at),self.assertRaises(ValueError):validate(*args)


if __name__=='__main__':unittest.main()
