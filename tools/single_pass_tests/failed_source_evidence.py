"""Preserve bounded diagnostic buffers before a failed source validator exits."""

import struct


TABLES = (
    ('source-cbvs', 'SourceCbv', 'Rows', 'Count', 'RowBytes', 192, 32768),
    ('source-details', 'SourceDetails', 'Rows', 'Count', 'RowBytes', 1024, 32768),
    ('cbv-snapshots', 'SourceCbvSnapshots', 'Rows', 'Count', 'RowBytes', 256, 32768),
    ('cbv-snapshot-commands', 'SourceCbvSnapshots', 'Commands', 'CommandCount', 'CommandBytes', 192, 2048),
)
PREFIX = 'CyberpunkVR_Retained'


def capture(read_symbol, read_memory, output):
    """No completion or execution claim: only stable copies of failure metadata."""
    state = lambda: struct.unpack('<I', read_symbol(PREFIX + 'GeometryState', 4))[0]
    result = dict(verified=False, nativeExecutionPermission=False, files={}, errors={})
    if state() != 4:
        raise ValueError('Failure evidence requires the failed retained state')
    for label, stem, rows, count, stride, expected, limit in TABLES:
        prefix = PREFIX + stem
        try:
            def metadata():
                return (struct.unpack('<I', read_symbol(prefix + count, 4))[0],
                        struct.unpack('<I', read_symbol(prefix + stride, 4))[0],
                        struct.unpack('<Q', read_symbol(prefix + rows, 8))[0])
            before = metadata()
            size, width, pointer = before
            if width != expected or size > limit or (size and not pointer):
                raise ValueError('Invalid failure buffer extent or ABI')
            raw = read_memory(pointer, size * width) if size else b''
            again = read_memory(pointer, size * width) if size else b''
            if len(raw) != size * width or raw != again or metadata() != before or state() != 4:
                raise ValueError('Failure buffer changed while reading')
            path = output.with_name(output.stem + '-failed-' + label + '.bin')
            path.write_bytes(raw)
            result['files'][label] = dict(file=path.name, rows=size, rowBytes=width)
        except Exception as error:
            result['errors'][label] = str(error)
    return result
