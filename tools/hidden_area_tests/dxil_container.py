"""DXIL container helpers for the hidden-area shader patches (docs/hidden-area-mask-20261009.md).

- dxil_hash: the container hash D3D12 checks (DXBC-style MD5 variant over everything after the
  20-byte magic + hash header).
- bitcode_range: where the LLVM bitcode of the DXIL part lies in the container.
- VBR6 placeholders: a float in [1, 2) has bit 29 as its highest set bit, so its 32-bit pattern
  always takes six VBR6 chunks (36 bits) in the bitcode's constants block. A placeholder can then be
  found by its bit pattern and overwritten in place with any other value in [1, 2).
"""
import struct

_S = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
_K = [int(abs(__import__('math').sin(i + 1)) * 2 ** 32) & 0xFFFFFFFF for i in range(64)]


def _rotl(x, c):
    x &= 0xFFFFFFFF
    return ((x << c) | (x >> (32 - c))) & 0xFFFFFFFF


def _transform(state, block):
    x = struct.unpack('<16I', block)
    a, b, c, d = state
    for i in range(64):
        if i < 16:
            f, g = (b & c) | (~b & d), i
        elif i < 32:
            f, g = (d & b) | (~d & c), (5 * i + 1) % 16
        elif i < 48:
            f, g = b ^ c ^ d, (3 * i + 5) % 16
        else:
            f, g = c ^ (b | (~d & 0xFFFFFFFF)), (7 * i) % 16
        f = (f + a + _K[i] + x[g]) & 0xFFFFFFFF
        a, d, c, b = d, c, b, (b + _rotl(f, _S[i])) & 0xFFFFFFFF
    return [(s + v) & 0xFFFFFFFF for s, v in zip(state, (a, b, c, d))]


def dxil_hash(container):
    """The 16-byte hash stored at offset 4 of a DXBC/DXIL container."""
    data = bytes(container[20:])
    size = len(data)
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    full = size & ~63
    for at in range(0, full, 64):
        state = _transform(state, data[at:at + 64])
    tail = data[full:]
    count = len(tail)
    bits = (size * 8) & 0xFFFFFFFF
    last = struct.pack('<I', (size * 2) | 1)
    if 64 - 1 - count < 8:
        state = _transform(state, tail + b'\x80' + bytes(63 - count))
        block = struct.pack('<I', bits) + bytes(56) + last
    else:
        block = struct.pack('<I', bits) + tail + b'\x80' + bytes(64 - 4 - count - 1 - 4) + last
    state = _transform(state, block)
    return struct.pack('<4I', *state)


def sign(container):
    out = bytearray(container)
    out[4:20] = dxil_hash(out)
    return bytes(out)


def parts(container):
    magic, = struct.unpack_from('<4s', container, 0)
    if magic != b'DXBC':
        raise ValueError('not a DXBC container')
    count, = struct.unpack_from('<I', container, 28)
    for offset in struct.unpack_from('<%dI' % count, container, 32):
        fourcc, size = struct.unpack_from('<4sI', container, offset)
        yield fourcc, offset + 8, size


def bitcode_range(container):
    """(start, size) of the LLVM bitcode inside the DXIL part."""
    for fourcc, start, size in parts(container):
        if fourcc == b'DXIL':
            magic, _, offset, length = struct.unpack_from('<4sIII', container, start + 8)
            if magic != b'DXIL':
                raise ValueError('bad DXIL program header')
            return start + 8 + offset, length
    raise ValueError('no DXIL part')


def vbr6_bits(value):
    bits = []
    while True:
        chunk = value & 31
        value >>= 5
        chunk |= 32 if value else 0
        bits += [(chunk >> i) & 1 for i in range(6)]
        if not value:
            return bits


def float_pattern(value):
    word, = struct.unpack('<I', struct.pack('<f', value))
    if not 0x3F800000 <= word < 0x40000000:
        raise ValueError('placeholder must be a float in [1, 2)')
    return vbr6_bits(word)


def find_bits(container, pattern):
    """Container bit offsets where `pattern` starts inside the bitcode."""
    start, size = bitcode_range(container)
    stream = int.from_bytes(container[start:start + size], 'little')
    want = sum(bit << i for i, bit in enumerate(pattern))
    mask = (1 << len(pattern)) - 1
    return [start * 8 + i for i in range(size * 8 - len(pattern) + 1) if (stream >> i) & mask == want]


def write_float(container, bit, value):
    """Overwrite the six-chunk VBR6 float at container bit offset `bit`; the hash is not updated."""
    out = bytearray(container)
    for i, b in enumerate(float_pattern(value)):
        byte, shift = divmod(bit + i, 8)
        out[byte] = (out[byte] & ~(1 << shift)) | (b << shift)
    return bytes(out)
