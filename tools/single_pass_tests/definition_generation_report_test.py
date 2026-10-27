import struct
import unittest
from definition_generation_report import validate


class DefinitionGenerationReportTests(unittest.TestCase):
    def fixture(self):
        stats = [20, 20, 2, 3, 2, 18, 18, 17, 1, 0, 0, 1, 2, 0, 0, 0]
        source = dict(consumerId=19, epoch=2, pairGeneration=3,
                      owners=[dict(header='0x50070', epoch=9, definitions=[0x90000, 0]),
                              dict(header='0x60070', epoch=0, definitions=[])])
        return [stats, [19, 2, 3, 7], struct.pack('<12Q', 8, 0x50070, 9, 0, 0, 6, 5, 1, 0, 0, 1, 0), source]

    def test_pending_other_consumers_do_not_invalidate_selected_completion(self):
        report = validate(*self.fixture())
        self.assertTrue(report['rootAdmissionProved'])
        self.assertTrue(report['terminalCompletionProved'])
        self.assertEqual(report['generations'][0]['pendingConsumers'], 1)
        for name in ('allPriorConsumersAccounted', 'definitionOwnershipProved', 'nativeExclusiveProved', 'executionRedirected'):
            self.assertFalse(report[name])

    def test_exact_selected_identity_and_all_three_stages_required(self):
        for at, value in ((0, 18), (1, 1), (2, 2), (3, 3)):
            args = self.fixture(); args[1][at] = value
            with self.subTest(at=at), self.assertRaises(ValueError): validate(*args)

    def test_sticky_failure_overflow_and_ownership_are_rejected(self):
        for at, value in ((0, 0), (1, 21), (9, 1), (10, 1), (11, 257), (12, 4097), (13, 1), (14, 1), (15, 1)):
            args = self.fixture(); args[0][at] = value
            with self.subTest(at=at), self.assertRaises(ValueError): validate(*args)

    def test_root_waiting_at_admission_gate_has_not_accessed_source_yet(self):
        args = self.fixture(); args[0][0] += 1
        self.assertTrue(validate(*args)['rootAdmissionProved'])

    def test_exact_header_epoch_and_complete_generation_binding_required(self):
        for at, value in ((0, 0), (1, 0x60070), (2, 8), (5, 7), (6, 0), (8, 1), (9, 2), (10, 2), (11, 1)):
            args = self.fixture(); row = list(struct.unpack('<12Q', args[2])); row[at] = value; args[2] = struct.pack('<12Q', *row)
            with self.subTest(at=at), self.assertRaises(ValueError): validate(*args)

    def test_source_close_includes_provisional_pre_constructor_root(self):
        args = self.fixture(); row = list(struct.unpack('<12Q', args[2])); row[3:5] = [10, 20]; row[9] = 1
        args[2] = struct.pack('<12Q', *row)
        self.assertEqual(validate(*args)['generations'][0]['closedEpoch'], 10)
        for at, value in ((3, 9), (4, 18)):
            bad = args.copy(); invalid = row.copy(); invalid[at] = value; bad[2] = struct.pack('<12Q', *invalid)
            with self.subTest(at=at), self.assertRaises(ValueError): validate(*bad)

    def test_missing_duplicate_and_torn_rows_rejected(self):
        for raw in (b'', self.fixture()[2] * 2, self.fixture()[2] + b'x'):
            args = self.fixture(); args[2] = raw
            with self.assertRaises(ValueError): validate(*args)


if __name__ == '__main__': unittest.main()
