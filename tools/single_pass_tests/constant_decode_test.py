import struct
import unittest
from read_constants import decode


def sample(logical=False):
    data = bytes([19] * (112 if logical else 256))
    h = 14695981039346656037
    for byte in data:
        h = ((h ^ byte) * 1099511628211) & 0xffffffffffffffff
    return dict(Draws=struct.pack('<Q6I', 1, 1, 0, 1, 7, 1, 1),
                Bindings=struct.pack('<4Q10I', 0x1000, 7, 0x10000, h, 1, 0, 0, 1, 1, 256, 112, 1, 1, int(logical)),
                Payloads=bytes(16) + struct.pack('<IIQ', 0, len(data), h), Blob=data)


class ConstantDecodeTests(unittest.TestCase):
    def test_logical_payload_does_not_require_cbv_alignment_padding(self):
        _, bindings, payloads = decode(sample(True))
        self.assertEqual(bindings[0]['bytes'], 256)
        self.assertEqual(payloads[1]['bytes'], 112)
        broken = sample(True)
        b = bytearray(broken['Bindings'])
        struct.pack_into('<I', b, 68, 0)
        broken['Bindings'] = b
        with self.assertRaisesRegex(ValueError, 'payload'):
            decode(broken)

    def test_owned_payload(self):
        draws, bindings, payloads = decode(sample())
        self.assertEqual(draws[0]['flags'], 7)
        self.assertEqual(bindings[0]['shaderRegister'], 1)
        self.assertEqual(payloads[1]['bytes'], 256)

    def test_command_position_payload_and_unknown_flags(self):
        for flags in (3, 7):
            blocks = sample(True)
            binding = bytearray(blocks['Bindings'])
            struct.pack_into('<I', binding, 68, flags)
            blocks['Bindings'] = binding
            self.assertEqual(decode(blocks)[1][0]['reserved'], flags)
        for flags in (2, 5, 15):
            blocks = sample(True)
            binding = bytearray(blocks['Bindings'])
            struct.pack_into('<I', binding, 68, flags)
            blocks['Bindings'] = binding
            with self.assertRaises(ValueError):
                decode(blocks)

    def test_tampered_payload_or_bounds(self):
        for name in ('Draws', 'Bindings', 'Payloads', 'Blob'):
            blocks = sample()
            blocks[name] = blocks[name][:-1]
            with self.assertRaises((ValueError, struct.error)):
                decode(blocks)
        blocks = sample()
        blocks['Blob'] = b'\x00' + blocks['Blob'][1:]
        with self.assertRaisesRegex(ValueError, 'hash'):
            decode(blocks)

    def test_stable_cannot_include_failed_binding(self):
        blocks = sample()
        binding = bytearray(blocks['Bindings'])
        struct.pack_into('<I', binding, 64, 32)
        blocks['Bindings'] = binding
        with self.assertRaisesRegex(ValueError, 'Stable draw'):
            decode(blocks)

    def test_duplicate_sequence_wrong_eye_and_unowned_payload(self):
        blocks = sample()
        blocks['Draws'] *= 2
        with self.assertRaisesRegex(ValueError, 'identity'):
            decode(blocks)
        blocks = sample()
        d = bytearray(blocks['Draws'])
        struct.pack_into('<I', d, 24, 2)
        blocks['Draws'] = d
        with self.assertRaisesRegex(ValueError, 'eye'):
            decode(blocks)
        blocks = sample()
        b = bytearray(blocks['Bindings'])
        struct.pack_into('<I', b, 60, 0)
        blocks['Bindings'] = b
        with self.assertRaisesRegex(ValueError, 'payload'):
            decode(blocks)


if __name__ == '__main__':
    unittest.main()
