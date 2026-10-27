import pathlib
import struct
import tempfile
import unittest

from failed_source_evidence import PREFIX, TABLES, capture


class FailureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.symbols = {PREFIX + 'GeometryState': struct.pack('<I', 4)}
        self.memory = {}
        for index, (_, stem, rows, count, stride, width, _) in enumerate(TABLES):
            prefix = PREFIX + stem
            self.symbols[prefix + count] = struct.pack('<I', 1)
            self.symbols[prefix + stride] = struct.pack('<I', width)
            self.symbols[prefix + rows] = struct.pack('<Q', 0x10000 * (index + 1))
            self.memory[0x10000 * (index + 1)] = bytes([index]) * width

    def run_capture(self, directory, read_memory=None):
        return capture(lambda name, size: self.symbols[name][:size],
                       read_memory or (lambda address, size: self.memory[address][:size]),
                       pathlib.Path(directory) / 'preparation.json')

    def test_failed_buffers_survive_without_becoming_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_capture(directory)
            self.assertEqual(len(result['files']), 4)
            self.assertEqual(result['errors'], {})
            self.assertFalse(result['verified'])
            self.assertFalse(result['nativeExecutionPermission'])
            for index, (label, *_) in enumerate(TABLES):
                self.assertEqual((pathlib.Path(directory) / result['files'][label]['file']).read_bytes(),
                                 self.memory[0x10000 * (index + 1)])

    def test_capacity_and_missing_symbol_preserve_other_buffers(self):
        self.symbols[PREFIX + 'SourceCbvCount'] = struct.pack('<I', 32769)
        del self.symbols[PREFIX + 'SourceDetailsRows']
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_capture(directory)
            self.assertEqual(set(result['errors']), {'source-cbvs', 'source-details'})
            self.assertEqual(len(result['files']), 2)

    def test_changed_bytes_are_not_saved(self):
        calls = {}
        def read(address, size):
            calls[address] = calls.get(address, 0) + 1
            return bytes([calls[address]]) * size
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_capture(directory, read)
            self.assertEqual(len(result['errors']), 4)
            self.assertEqual(result['files'], {})

    def test_running_state_is_not_read(self):
        self.symbols[PREFIX + 'GeometryState'] = struct.pack('<I', 2)
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'failed retained state'):
                self.run_capture(directory)


if __name__ == '__main__':
    unittest.main()
