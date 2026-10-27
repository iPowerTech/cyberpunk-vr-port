import unittest
from analyze_descriptor_audit import compare_range


class DescriptorComparisonTests(unittest.TestCase):
    def row(self,**changes):
        result=dict(key=(0,0,0,0,2,0,1),status=2,count=2,ids=[1,2],gpu=0x10000,heap=3,version=12)
        return dict(result,**changes)

    def test_same_range_same_version(self):
        self.assertEqual(compare_range(self.row(),self.row()),'unchanged_range')
        self.assertEqual(compare_range(self.row(status=1,ids=[0,0]),self.row(status=1,ids=[0,0])),'unchanged_range')

    def test_different_addresses_can_have_same_descriptors(self):
        self.assertEqual(compare_range(self.row(),self.row(gpu=0x20000,version=21)),'equal_descriptions')
        self.assertEqual(compare_range(self.row(),self.row(gpu=0x20000,ids=[1,3])),'compare_slots')

    def test_unknown_is_never_equal_by_zero(self):
        for changes in (dict(status=0,heap=0,gpu=0),dict(status=2,ids=[]),dict(status=2,ids=[0,0])):
            self.assertEqual(compare_range(self.row(**changes),self.row(**dict(changes,version=14))),'compare_slots')

    def test_heap_reuse_and_layout_changes(self):
        self.assertEqual(compare_range(self.row(status=1),self.row(status=1,heap=4)),'compare_slots')
        self.assertEqual(compare_range(self.row(),self.row(key=(0,0,0,0,2,0,5))),'layout_mismatch')


if __name__=='__main__':unittest.main()
