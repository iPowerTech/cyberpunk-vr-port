"""Read-only sampler for the exported native roomscale diagnostic block.

Addresses must be resolved afresh in x64dbg for the requested PID. This does
not inject code, change focus, send keys, or write game memory.
"""
import base64
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import struct
import time


def decode(data):
    values = struct.unpack('<9Q2I21f4x', data)
    result = dict(zip(('calls', 'injected', 'feedback_reads', 'resets', 'player',
                       'movement', 'backend', 'pose_sequence', 'origin', 'reason', 'gates'), values[:11]))
    result.update(dt=values[11], yaw=values[12])
    for name, start, length in (('native', 13, 3), ('requested', 16, 3), ('before', 19, 3),
                               ('after', 22, 3), ('velocity', 25, 3), ('consumed', 28, 2), ('feedback', 30, 2)):
        result[name] = values[start:start+length]
    result['raw_base64'] = base64.b64encode(data).decode('ascii')
    if result['gates'] > 15 or result['reason'] > 6:
        raise RuntimeError('Unexpected diagnostic ABI; do not interpret this snapshot')
    return result


class Reader:
    def __init__(self, pid, exe, address, sequence_address):
        self.address, self.sequence_address = address, sequence_address
        self.k = c.WinDLL('kernel32', use_last_error=True)
        self.k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.k.OpenProcess.restype = w.HANDLE
        self.k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
        self.k.ReadProcessMemory.restype = w.BOOL
        self.k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
        self.k.QueryFullProcessImageNameW.restype = w.BOOL
        self.k.CloseHandle.argtypes = [w.HANDLE]
        self.k.CloseHandle.restype = w.BOOL
        self.handle = self.k.OpenProcess(0x1010, False, pid) # limited query + VM_READ only
        if not self.handle:
            raise c.WinError(c.get_last_error())
        try:
            text = c.create_unicode_buffer(32768)
            length = w.DWORD(len(text))
            if not self.k.QueryFullProcessImageNameW(self.handle, 0, text, c.byref(length)):
                raise c.WinError(c.get_last_error())
            if Path(text.value).resolve() != Path(exe).resolve():
                raise RuntimeError('PID does not refer to the expected game executable')
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            self.k.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        buffer = c.create_string_buffer(size)
        count = c.c_size_t()
        if not self.k.ReadProcessMemory(self.handle, address, buffer, size, c.byref(count)) or count.value != size:
            raise c.WinError(c.get_last_error())
        return buffer.raw

    def sample(self):
        for _ in range(100):
            before = struct.unpack('<I', self.read(self.sequence_address, 4))[0]
            if before & 1:
                time.sleep(.001)
                continue
            data = self.read(self.address, 168)
            after = struct.unpack('<I', self.read(self.sequence_address, 4))[0]
            if before == after and not (after & 1):
                return dict(decode(data), publication_sequence=after, time_ns=time.time_ns())
        raise RuntimeError('No coherent roomscale snapshot within the bounded retries')
