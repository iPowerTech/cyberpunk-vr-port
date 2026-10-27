import unittest

from owned_execution_failure import FIELDS, capture


class OwnedFailureTests(unittest.TestCase):
    def fixture(self, sequences=(2, 2)):
        sequence = iter(sequences)
        data = {name: [0] * count for _, name, count in FIELDS}
        data['CyberpunkVR_OwnedExecutionStats'][0] = 5
        data['CyberpunkVR_OwnedExecutionStats'][22:24] = [8, 19]
        data['CyberpunkVR_OwnedSceneSplitStats'] = [0] * 24
        def read32(name):
            return next(sequence) if name.endswith('Seq') else 0
        return data, read32, lambda name, count: data[name]

    def test_early_rejection_is_preserved_without_waiting_for_completion(self):
        data, read, words = self.fixture()
        result = capture(read, words)
        self.assertTrue(result['consistent'])
        self.assertEqual(result['stats'][22:24], [8, 19])
        data['CyberpunkVR_OwnedExecutionStats'][22] = 0
        self.assertEqual(result['stats'][22], 8)

    def test_inactive_and_incomplete_states_are_diagnostic_not_success(self):
        for state in (0, 1, 2, 3, 4, 6):
            data, read, words = self.fixture()
            data['CyberpunkVR_OwnedExecutionStats'][0] = state
            result = capture(read, words)
            self.assertTrue(result['consistent'])
            self.assertEqual(result['stats'][0], state)

    def test_odd_and_torn_reads_retry_with_a_bound(self):
        _, read, words = self.fixture((1, 2, 4, 6, 6))
        delays = []
        result = capture(read, words, sleep=delays.append)
        self.assertTrue(result['consistent'])
        self.assertEqual((result['attempt'], result['sequence']), (3, 6))
        self.assertEqual(delays, [.002, .002])

    def test_permanently_odd_snapshot_is_not_reported_coherent(self):
        _, read, words = self.fixture((1, 1, 1))
        result = capture(read, words, attempts=3, sleep=lambda _: None)
        self.assertFalse(result['consistent'])
        self.assertNotIn('stats', result)
        self.assertEqual(result['attempt'], 3)

    def test_no_optional_export_in_older_build(self):
        _, read, words = self.fixture()
        result = capture(read, words, available=lambda _: False)
        self.assertTrue(result['consistent'])
        self.assertNotIn('sceneSplit', result)

    def test_bad_word_abi_is_reported_without_replacing_original_failure(self):
        for name in [name for _, name, _ in FIELDS] + ['CyberpunkVR_OwnedSceneSplitStats']:
            for invalid in ([], [True], [-1]):
                data, read, words = self.fixture()
                data[name] = invalid
                result = capture(read, words)
                self.assertFalse(result['consistent'])
                self.assertIn('ABI mismatch', result['error'])

    def test_process_read_failure_is_evidence_error(self):
        def failed(*_):
            raise OSError('process exited')
        result = capture(failed, failed)
        self.assertFalse(result['consistent'])
        self.assertEqual(result['error'], 'process exited')

    def test_invalid_attempt_count_is_rejected(self):
        _, read, words = self.fixture()
        for count in (0, 33, True):
            with self.assertRaises(ValueError):
                capture(read, words, attempts=count)


if __name__ == '__main__':
    unittest.main()
