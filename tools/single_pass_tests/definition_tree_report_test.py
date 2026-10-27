import struct
import unittest
from definition_tree_report import decode, NO_INDEX


class DefinitionTreeReportTests(unittest.TestCase):
    def fixture(self):
        base = 0x140000000
        stats = [2, 1, 2, 7, 3, 3, 2, 3, 2, 0, 0, 0]
        nodes = [(0x10000, base+0x2b3a008, base+0x986590, base+0x1ec1d0, 0, 1, 0, 0, 1, 0, 0),
                 (0x11000, base+0x2b3a008, base+0x986590, base+0x1ec1d0, 0, 1, 1, 1, 1, 0, 0),
                 (0x20000, base+0x2b39f70, base+0x986600, base+0x23a938, 0x21000, 2, 2, NO_INDEX, 0, 1, 0)]
        allocator = base+0x2b39fa0
        arrays = [(0x10028, 0x30000, 0x30010, allocator, base+0x985abc, 1, 1, 16, 0),
                  (0x11028, 0x31000, 0x31010, allocator, base+0x985abc, 1, 1, 16, 1),
                  (0x20030, 0x32000, 0x32030, allocator, base+0x985abc, 1, 1, 48, 2)]
        blocks = dict(Roots=struct.pack('<Q2IQ2IQ2I', 0x10000, 0, 0, 0x11000, 1, 1, 0, 2, NO_INDEX),
                      Nodes=b''.join(struct.pack('<5Q6I', *row) for row in nodes),
                      Handles=b''.join(struct.pack('<3Q4I2i', address, 0x20000, 0x21000, parent, 2, 0, 0, 2, 1)
                                       for address, parent in ((0x30000, 0), (0x31000, 1))),
                      Arrays=b''.join(struct.pack('<5Q4I', *row) for row in arrays),
                      Allocators=struct.pack('<3Q2I3Q2I', base+0x3302610, 0x40001, 0x40000, 0, 0,
                                             base+0x32f66d8, 0x41002, 0x41000, 1, 0))
        return [stats, blocks, base, struct.pack('<3Q', 0x10000, 0x11000, 0)]

    def test_shared_child_and_null_root_preserve_observation_not_safety(self):
        report = decode(*self.fixture())
        self.assertEqual((len(report['nodes']), len(report['handles']), len(report['arrays'])), (3, 2, 3))
        self.assertEqual([h['child'] for h in report['handles']], [2, 2])
        self.assertFalse(report['destructorSafetyProved'])
        self.assertFalse(report['nativeOwnershipProved'])

    def test_binary_abi_counts_are_exact(self):
        for name in ('Roots', 'Nodes', 'Handles', 'Arrays', 'Allocators'):
            args = self.fixture(); args[1][name] += b'x'
            with self.subTest(name=name), self.assertRaises(ValueError): decode(*args)

    def test_root_inventory_and_child_pair_are_exact(self):
        args = self.fixture(); args[3] = struct.pack('<3Q', 0x11000, 0x10000, 0)
        with self.assertRaises(ValueError): decode(*args)
        for offset, value in ((8, 0x20008), (16, 0x21008), (24, 3), (28, 3), (40, 0)):
            args = self.fixture(); raw = bytearray(args[1]['Handles'])
            struct.pack_into('<Q' if offset < 24 else '<I', raw, offset, value); args[1]['Handles'] = bytes(raw)
            with self.subTest(offset=offset), self.assertRaises(ValueError): decode(*args)

    def test_unknown_child_type_is_not_silently_certified(self):
        args = self.fixture(); row = list(struct.unpack('<5Q6I', args[1]['Nodes'][128:])); row[2] += 8; row[5:9] = [0, NO_INDEX, NO_INDEX, 0]
        args[1]['Nodes'] = args[1]['Nodes'][:128] + struct.pack('<5Q6I', *row)
        args[1]['Arrays'] = args[1]['Arrays'][:112]; args[0][7] = 2
        report = decode(*args)
        self.assertEqual(report['nodes'][2]['kind'], 0)
        self.assertFalse(report['destructorSafetyProved'])

    def test_invalid_state_bounds_and_allocator_identity_rejected(self):
        for at, value in ((0, 1), (4, 8193), (5, 4097), (6, 8193), (7, 4097), (8, 1), (9, 1)):
            args = self.fixture(); args[0][at] = value
            with self.subTest(at=at), self.assertRaises(ValueError): decode(*args)
        args = self.fixture(); raw = bytearray(args[1]['Allocators']); struct.pack_into('<Q', raw, 16, 0x40008)
        args[1]['Allocators'] = bytes(raw)
        with self.assertRaises(ValueError): decode(*args)


if __name__ == '__main__': unittest.main()
