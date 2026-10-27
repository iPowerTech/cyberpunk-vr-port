import struct
import unittest
from resource_replay_report import validate


def fixture():
    stats=[3,10,2,3,1000,3000,2000,4,4,0,2,2,0,0,0,0]
    proof=[10,1,1,2,2,3,1000,0]
    execution=[4,10,2,3,2000,3000]+[0]*18
    rows=struct.pack('<16I',1,0,0,0,3,3,1,1,2,1,1,1,1,1,1,1)
    return [stats,proof,rows,execution]


class ResourceReplayReportTests(unittest.TestCase):
    def test_actual_all_slot_closure_keeps_alias_permission_false(self):
        report=validate(*fixture());self.assertEqual(report['commands'],4)
        self.assertEqual(report['nodes'],2);self.assertIs(report['executionPermission'],False)
        self.assertIs(report['externalAliasingProved'],False)

    def test_stale_or_partial_native_replay_is_rejected(self):
        for index,value in ((0,2),(1,11),(2,3),(3,4),(4,0),(5,2000),(6,3000),(7,5),(8,3),(10,1),(11,1),(12,1),(15,1)):
            args=fixture();args[0][index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):validate(*args)

    def test_terminal_and_compiler_rows_are_both_required(self):
        for index,value in ((0,11),(1,0),(2,0),(3,3),(4,3),(5,4),(6,1001),(7,1)):
            args=fixture();args[1][index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):validate(*args)

    def test_counter_and_node_binding_rows_are_not_optional(self):
        for field,value in ((0,2),(1,1),(2,1),(3,2),(4,501),(5,2),(6,0),(7,0)):
            args=fixture();rows=bytearray(args[2]);struct.pack_into('<I',rows,field*4,value);args[2]=bytes(rows)
            with self.subTest(field=field),self.assertRaises(ValueError):validate(*args)


if __name__=='__main__':unittest.main()
