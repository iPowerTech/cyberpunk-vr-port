"""Install one prepared UploadReservation patch under an actual debugger stop.

No target function, remote thread, or DLL installer is called. Native preparation
must already have completed while the game was running. Unsafe failures retain
the outstanding event; only verified rollback or an explicitly owned-game stop
can release recovery. Microsoft DebugActiveProcess/ContinueDebugEvent semantics:
https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess
https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-continuedebugevent
"""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import time
from dataclasses import dataclass

PLAN_VERSION = 0x55504C414E303031
RECEIPT_VERSION = 0x5550524543303031
PLAN_BYTES, RECEIPT_BYTES = 368, 144
ALLOCATOR_RVA, SITE_RVA = 0x774558, 0x7745CE
ALLOCATOR_FNV = 0xBF311A1C73348164
ORIGINAL = bytes.fromhex('75 24 33 D2 48 8B C1')
EXCEPTION, CREATE_THREAD, CREATE_PROCESS, EXIT_THREAD, EXIT_PROCESS, LOAD_DLL = range(1, 7)
BREAKPOINT = 0x80000003
CONTINUE, NOT_HANDLED = 0x10002, 0x80010001
PREFIX = 'CyberpunkVR_UploadReservation'
SYMBOLS = tuple(PREFIX + name for name in (
    'ExternalPlanSeq', 'ExternalPlan', 'ExternalReceiptSeq', 'ExternalReceipt',
    'PrepareRequest', 'AcceptRequest', 'Enable', 'ExternalInstallState'))


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


@dataclass(frozen=True)
class Identity:
    pid: int
    creation: int
    game_base: int
    game_size: int
    dll_base: int
    dll_size: int
    exe_path: str
    exe_sha256: str
    dll_sha256: str


@dataclass(frozen=True)
class Event:
    code: int
    pid: int
    tid: int
    exception: int = 0
    first_chance: int = 0
    address: int = 0
    start_address: int = 0
    handles: tuple = ()


@dataclass(frozen=True)
class Plan:
    words: tuple
    raw: bytes
    guard: bytes
    original: bytes
    patch: bytes
    relay_code: bytes

    @property
    def site(self):
        return self.words[6]

    @property
    def protection(self):
        return self.words[14]


def decode_plan(raw, identity):
    if len(raw) != PLAN_BYTES:
        raise ValueError('Prepared plan ABI length mismatch')
    words = struct.unpack_from('<20Q', raw)
    unsigned = raw[:128] + bytes(8) + raw[136:]
    if words[0] != PLAN_VERSION or not words[1] or fnv(unsigned) != words[16]:
        raise ValueError('Prepared plan version/nonce/checksum mismatch')
    if words[17:20] != (0, 0, 0) or raw[365:] != bytes(3):
        raise ValueError('Prepared plan reserved bytes are nonzero')
    if words[2:5] != (identity.pid, identity.creation, identity.game_base):
        raise ValueError('Prepared plan refers to another PID/creation/image')
    if words[5:7] != (identity.game_base + ALLOCATOR_RVA, identity.game_base + SITE_RVA):
        raise ValueError('Prepared native allocator/site differs')
    if words[10:13] != (177, 7, 14) or not 0 < words[13] <= 4096:
        raise ValueError('Prepared native/thunk extent differs')
    if words[9] != identity.dll_base or not identity.dll_base <= words[8] < identity.dll_base + identity.dll_size:
        raise ValueError('Prepared thunk/pinned module identity differs')
    if words[13] > identity.dll_base + identity.dll_size - words[8]:
        raise ValueError('Prepared thunk exceeds pinned DLL')
    if not identity.game_base <= words[5] <= identity.game_base + identity.game_size - 177:
        raise ValueError('Native allocator exceeds game image')
    if words[14] not in (0x20, 0x40, 0x80):
        raise ValueError('Prepared code protection is unsupported')
    guard, original, patch, relay = raw[160:337], raw[337:344], raw[344:351], raw[351:365]
    offset = SITE_RVA - ALLOCATOR_RVA
    displacement = words[7] - (words[6] + 5)
    if not -(1 << 31) <= displacement < (1 << 31):
        raise ValueError('Prepared relay is outside CALL rel32 reach')
    expected_patch = b'\xe8' + struct.pack('<i', displacement) + b'\x75\x1f'
    if words[15] != ALLOCATOR_FNV or fnv(guard) != ALLOCATOR_FNV or original != ORIGINAL or guard[offset:offset + 7] != ORIGINAL:
        raise ValueError('Full native177/original7 guard mismatch')
    if patch != expected_patch or relay != b'\xff\x25\0\0\0\0' + struct.pack('<Q', words[8]):
        raise ValueError('Prepared CALL/relay/thunk encoding mismatch')
    return Plan(words, raw, guard, original, patch, relay)


def read32(backend, address):
    return struct.unpack('<I', backend.read(address, 4))[0]


def write32(backend, address, value):
    backend.write(address, struct.pack('<I', value))


class HeldRecovery(RuntimeError):
    def __init__(self, session, message):
        super().__init__(message)
        self.session = session


class Session:
    def __init__(self, backend, identity, symbols, expected_nonce=None):
        self.backend, self.identity, self.symbols = backend, identity, symbols
        self.plan = None
        self.expected_nonce = expected_nonce
        self.event = None
        self.attached = False
        self.changed = False
        self.protection_changed = False
        self.code_valid = True
        self.foreign_body = False
        self.event_proof = None
        self.receipt_published = False
        self.accept_status = 'not_attempted'
        self.termination_started = False
        self.exit_event_seen = False
        self.error = ''
        self.history = []

    def record(self, action):
        if len(self.history) < 256:
            self.history.append(action)

    def state(self, outcome):
        return {'outcome': outcome, 'pid': self.identity.pid,
                'process_creation': self.identity.creation, 'dll_sha256': self.identity.dll_sha256,
                'exe_sha256': self.identity.exe_sha256, 'nonce': self.plan.words[1] if self.plan else 0,
                'attached': self.attached, 'event_held': self.event is not None,
                'event_code': self.event_proof.code if self.event_proof else 0,
                'event_thread': self.event_proof.tid if self.event_proof else 0,
                'code_valid': self.code_valid, 'receipt_published': self.receipt_published,
                'foreign_body': self.foreign_body,
                'accept_request': self.accept_status,
                'termination_started': self.termination_started,
                'held_event_code': self.event.code if self.event else 0,
                'error': self.error[:512], 'history': self.history,
                'observer_enable': 0, 'native_execution_permission': False,
                'native_commands_suppressed': 0}

    def snapshot(self):
        if read32(self.backend, self.symbols[PREFIX + 'ExternalInstallState']) != 1:
            raise ValueError('Native external observer is not in Prepared state')
        for name in ('PrepareRequest', 'AcceptRequest', 'Enable'):
            if read32(self.backend, self.symbols[PREFIX + name]):
                raise ValueError('Concurrent prepare/accept/enable request is active')
        seq = read32(self.backend, self.symbols[PREFIX + 'ExternalPlanSeq'])
        if not seq or seq & 1:
            raise ValueError('Prepared plan is not published coherently')
        raw = self.backend.read(self.symbols[PREFIX + 'ExternalPlan'], PLAN_BYTES)
        if seq != read32(self.backend, self.symbols[PREFIX + 'ExternalPlanSeq']):
            raise ValueError('Prepared plan publication changed')
        plan = decode_plan(raw, self.identity)
        if self.expected_nonce is not None and plan.words[1] != self.expected_nonce:
            raise ValueError('Prepared nonce differs from the explicitly requested transaction')
        return plan

    def publish(self, outcome, detached):
        event, plan = self.event_proof, self.plan
        protection = self.backend.protection(plan.site)
        if protection != plan.protection:
            raise OSError('Receipt final protection differs')
        words = [RECEIPT_VERSION, plan.words[1], plan.words[16], self.identity.pid,
                 self.identity.creation, outcome, event.code, event.tid,
                 plan.protection, protection,
                 7 if self.changed else 0, int(self.changed), 1, int(detached), 0,
                 event.exception, event.first_chance]
        raw = struct.pack('<17Q', *words)
        raw += struct.pack('<Q', fnv(raw))
        address = self.symbols[PREFIX + 'ExternalReceiptSeq']
        seq = read32(self.backend, address)
        odd = seq if seq & 1 else seq + 1
        if odd >= 0xffffffff:
            raise ValueError('Receipt publication sequence exhausted')
        write32(self.backend, address, odd)
        self.backend.write(self.symbols[PREFIX + 'ExternalReceipt'], raw)
        if self.backend.read(self.symbols[PREFIX + 'ExternalReceipt'], RECEIPT_BYTES) != raw:
            raise OSError('Receipt readback differs')
        write32(self.backend, address, odd + 1)
        if read32(self.backend, address) != odd + 1:
            raise OSError('Receipt sequence readback differs')
        self.receipt_published = True
        self.record('receipt_' + str(outcome))

    def release(self):
        if self.termination_started:
            raise HeldRecovery(self, 'Confirmed termination requires shutdown-event draining before release')
        if not self.code_valid:
            raise HeldRecovery(self, 'Unsafe code still holds the debug event')
        if self.event:
            self.backend.continue_event(self.event, CONTINUE if self.event == self.event_proof else
                                        NOT_HANDLED if self.event.code == EXCEPTION else CONTINUE)
            self.event = None
            self.record('continued')
        self.backend.detach(self.identity.pid)
        self.attached = False
        self.record('detached')

    def rollback(self):
        if not self.event or self.event != self.event_proof:
            raise HeldRecovery(self, 'Rollback requires the same outstanding attach breakpoint')
        if self.termination_started:
            raise HeldRecovery(self, 'Confirmed termination requires shutdown-event draining')
        if self.foreign_body or not (self.changed or self.protection_changed):
            raise HeldRecovery(self, 'No owned site mutation permits overwriting a foreign native body')
        self.code_valid = False
        current = self.backend.read(self.plan.words[5], 177)
        offset = SITE_RVA - ALLOCATOR_RVA
        if current[:offset] != self.plan.guard[:offset] or current[offset + 7:] != self.plan.guard[offset + 7:]:
            self.foreign_body = True
            raise HeldRecovery(self, 'Native body changed outside the owned site7; no overwrite is permitted')
        self.backend.protect(self.plan.site, 7, 0x40)
        self.backend.write(self.plan.site, self.plan.original)
        if self.backend.read(self.plan.words[5], 177) != self.plan.guard:
            raise OSError('Rollback original177 readback differs')
        self.backend.flush(self.plan.site, 7)
        self.backend.protect(self.plan.site, 7, self.plan.protection)
        if self.backend.protection(self.plan.site) != self.plan.protection:
            raise OSError('Rollback protection readback differs')
        self.changed = self.protection_changed = False
        self.code_valid = True
        self.record('rollback_verified')


def install(backend, symbols, *, expected_nonce=None, max_events=4096):
    identity = backend.identity()
    for name in SYMBOLS:
        size = PLAN_BYTES if name.endswith('ExternalPlan') else RECEIPT_BYTES if name.endswith('ExternalReceipt') else 4
        if name not in symbols or symbols[name] % (8 if size > 4 else 4) or not identity.dll_base <= symbols[name] <= identity.dll_base + identity.dll_size - size:
            raise ValueError('Symbol is missing/outside exact live DLL: ' + name)
    session = Session(backend, identity, symbols, expected_nonce)
    session.plan = session.snapshot()
    try:
        backend.attach(identity.pid)
        session.attached = True
        session.record('attached')
        backend.kill_on_exit(False)
        session.record('kill_on_exit_false')
        initial_process = False
        for _ in range(max_events):
            event = backend.wait_event()
            session.event = event
            backend.close_event_handles(event)
            if event.pid != identity.pid:
                raise ValueError('Debug event belongs to another process')
            if event.code == CREATE_PROCESS:
                if initial_process or event.start_address:
                    raise ValueError('Unexpected attach CREATE_PROCESS event')
                initial_process = True
            if initial_process and event.code == EXCEPTION and event.exception == BREAKPOINT and event.first_chance == 1 and event.address == backend.attach_breakpoint:
                session.event_proof = event
                session.record('attach_breakpoint_held')
                break
            backend.continue_event(event, NOT_HANDLED if event.code == EXCEPTION else CONTINUE)
            session.event = None
            if event.code == EXIT_PROCESS:
                raise ValueError('Target exited before attach breakpoint')
        else:
            raise TimeoutError('Attach breakpoint event bound exceeded')
        if backend.identity() != identity or session.snapshot().raw != session.plan.raw:
            raise ValueError('Live process/DLL/prepared namespace changed')
        plan = session.plan
        backend.validate_thunk(plan)
        if backend.read(plan.words[5], 177) != plan.guard or backend.read(plan.site, 7) != plan.original:
            session.code_valid = False
            session.foreign_body = True
            raise ValueError('Actual native177/original7 differs')
        if backend.read(plan.words[7], 14) != plan.relay_code or backend.protection(plan.words[7]) != 0x20:
            raise ValueError('Prepared executable relay differs')
        if backend.protection(plan.site) != plan.protection:
            raise ValueError('Original site protection differs')
        rips = backend.thread_rips()
        if not rips or session.event.tid not in rips or any(plan.site <= rip < plan.site + 7 for rip in rips.values()):
            raise ValueError('A live RIP is inside site7 or a live thread is missing')
        session.record('all_live_rips_checked')
        # Mark uncertainty before each mutating API: false/short returns can
        # still have changed bytes/protection and therefore require rollback.
        session.protection_changed = True
        previous = backend.protect(plan.site, 7, 0x40)
        if previous != plan.protection:
            raise ValueError('VirtualProtectEx returned another original protection')
        session.changed = True
        session.code_valid = False
        backend.write(plan.site, plan.patch)
        expected = plan.guard[:SITE_RVA - ALLOCATOR_RVA] + plan.patch + plan.guard[SITE_RVA - ALLOCATOR_RVA + 7:]
        if backend.read(plan.words[5], 177) != expected or backend.read(plan.site, 7) != plan.patch:
            raise OSError('Patched native177/site7 readback differs')
        backend.flush(plan.site, 7)
        backend.protect(plan.site, 7, plan.protection)
        if backend.protection(plan.site) != plan.protection:
            raise OSError('Final code protection differs')
        session.protection_changed = False
        session.code_valid = True
        session.publish(4, False)
        session.release()
        session.publish(1, True)
        session.accept_status = 'uncertain'
        write32(backend, symbols[PREFIX + 'AcceptRequest'], 1)
        session.accept_status = 'written'
        session.record('accept_requested_after_detach')
        return session.state('committed_detached_accept_requested')
    except BaseException as error:
        session.error = str(error)
        if not session.attached:
            # Post-detach publication failure cannot use the earlier event.
            return session.state('valid_detached_accept_status_unknown' if session.accept_status == 'uncertain' else
                                 'valid_detached_unaccepted' if session.changed else 'failed_no_write')
        if session.changed and not session.event and session.code_valid:
            raise HeldRecovery(session, 'Valid patch was continued but detach failed: ' + session.error) from error
        if session.changed or session.protection_changed:
            try:
                session.rollback()
            except BaseException as rollback_error:
                session.code_valid = False
                session.error += '; rollback: ' + str(rollback_error)
                raise HeldRecovery(session, session.error) from error
        try:
            session.release()
        except BaseException as release_error:
            session.error += '; release: ' + str(release_error)
            raise HeldRecovery(session, session.error) from error
        if session.event_proof:
            try:
                session.publish(2 if 'rollback_verified' in session.history else 3, True)
            except BaseException as receipt_error:
                session.error += '; receipt: ' + str(receipt_error)
        return session.state('rolled_back_detached' if 'rollback_verified' in session.history else 'failed_no_write_detached')


def drain_termination(session, max_events=4096):
    if not session.termination_started:
        raise HeldRecovery(session, 'No confirmed TerminateProcess success permits termination-event continuation')
    backend = session.backend
    try:
        for _ in range(max_events):
            if session.event:
                event = session.event
                if event.pid != session.identity.pid:
                    raise ValueError('Termination debug event belongs to another process')
                # TerminateProcess has succeeded: continuation drains kernel
                # shutdown, and does not resume live corrupt user-mode code.
                disposition = CONTINUE if event == session.event_proof else NOT_HANDLED if event.code == EXCEPTION else CONTINUE
                backend.continue_event(event, disposition)
                session.event = None
                session.exit_event_seen |= event.code == EXIT_PROCESS
                session.record('termination_event_continued_' + str(event.code))
            if session.exit_event_seen and backend.wait_exited(0):
                session.attached = False
                session.record('owned_game_exit_verified')
                return session.state('owned_game_terminated_verified')
            session.event = backend.wait_event()
            backend.close_event_handles(session.event)
        raise TimeoutError('Termination debug-event bound exceeded')
    except BaseException as error:
        session.error = 'Termination remains attached/pending: ' + str(error)
        raise HeldRecovery(session, session.error) from error


def recover_once(session, command, *, owned_game=False, terminate_on_unrecoverable=False):
    backend = session.backend
    if backend.wait_exited(0):
        session.event = None
        session.attached = False
        session.record('target_exit_verified')
        return session.state('target_exit_verified')
    if session.termination_started:
        return drain_termination(session)
    if not session.attached and session.event is None and session.code_valid:
        return session.state('rolled_back_detached_receipt_failed' if 'rollback_verified' in session.history else 'valid_detached_unaccepted')
    if command == 'terminate':
        current = backend.identity()
        if not owned_game or not terminate_on_unrecoverable or current != session.identity or Path(current.exe_path).name.lower() != 'cyberpunk2077.exe':
            raise ValueError('Termination requires the exact verified owned Cyberpunk2077 path/hash/creation')
        backend.terminate()
        session.termination_started = True
        session.record('terminate_process_succeeded')
        return drain_termination(session)
    if command == 'retry-rollback' and session.event_proof and session.event:
        session.rollback()
        session.release()
        try:
            session.publish(2, True)
        except BaseException as error:
            session.error = 'Rollback detached safely; receipt: ' + str(error)
            return session.state('rolled_back_detached_receipt_failed')
        return session.state('rolled_back_detached')
    if command == 'retry-detach' and session.code_valid:
        session.release()
        return session.state('valid_detached_unaccepted')
    raise ValueError('Recovery cannot continue/exit with invalid code; retry-rollback or permitted terminate is required')


def pe_bytes(path, rva, count, raw=None):
    raw = Path(path).read_bytes() if raw is None else raw
    nt = struct.unpack_from('<I', raw, 0x3c)[0]
    if raw[:2] != b'MZ' or raw[nt:nt + 4] != b'PE\0\0' or struct.unpack_from('<H', raw, nt + 4)[0] != 0x8664:
        raise ValueError('Expected an AMD64 PE image')
    sections, optional = struct.unpack_from('<H', raw, nt + 6)[0], struct.unpack_from('<H', raw, nt + 20)[0]
    if not 0 < sections <= 96 or struct.unpack_from('<H', raw, nt + 24)[0] != 0x20b:
        raise ValueError('Expected bounded PE32+ sections')
    for index in range(sections):
        at = nt + 24 + optional + index * 40
        virtual_size, virtual, size, offset = struct.unpack_from('<4I', raw, at + 8)
        if virtual <= rva and count <= size - (rva - virtual) and rva - virtual < max(size, virtual_size):
            result = raw[offset + rva - virtual:offset + rva - virtual + count]
            if len(result) == count:
                return result
    raise ValueError('PE RVA span is not backed by exact file bytes')


def export_rva(path, name):
    raw = Path(path).read_bytes();nt = struct.unpack_from('<I', raw, 0x3c)[0]
    exports, size = struct.unpack_from('<2I', raw, nt + 24 + 112)
    directory = struct.unpack('<10I', pe_bytes(path, exports, 40, raw))
    functions, names, function_table, name_table, ordinals = directory[5:]
    if names > 65536 or functions > 65536:
        raise ValueError('PE export directory exceeds bounds')
    for index in range(names):
        pointer = struct.unpack('<I', pe_bytes(path, name_table + index * 4, 4, raw))[0]
        text = bytearray()
        for offset in range(256):
            byte = pe_bytes(path, pointer + offset, 1, raw)
            if byte == b'\0':
                break
            text += byte
        if text.decode('ascii') == name:
            ordinal = struct.unpack('<H', pe_bytes(path, ordinals + index * 2, 2, raw))[0]
            if ordinal >= functions:
                raise ValueError('PE export ordinal exceeds bounds')
            rva = struct.unpack('<I', pe_bytes(path, function_table + ordinal * 4, 4, raw))[0]
            if exports <= rva < exports + size:
                raise ValueError('Forwarded attach breakpoint export is unsupported')
            return rva
    raise ValueError('Exact attach breakpoint export is missing')


# Fixed-width Win64 layouts also remain importable for the fake-backend tests.
U32, U64, PTR = c.c_uint32, c.c_uint64, c.c_void_p
class ExceptionRecord(c.Structure):
    _fields_ = [('code', U32), ('flags', U32), ('record', PTR), ('address', PTR), ('count', U32), ('parameters', U64 * 15)]
class ExceptionInfo(c.Structure):
    _fields_ = [('record', ExceptionRecord), ('first', U32)]
class ProcessInfo(c.Structure):
    _fields_ = [(name, PTR) for name in ('file', 'process', 'thread', 'base')] + [('offset', U32), ('bytes', U32)] + [(name, PTR) for name in ('tls', 'start', 'name')] + [('unicode', c.c_uint16)]
class ThreadInfo(c.Structure):
    _fields_ = [(name, PTR) for name in ('thread', 'tls', 'start')]
class DllInfo(c.Structure):
    _fields_ = [('file', PTR), ('base', PTR), ('offset', U32), ('bytes', U32), ('name', PTR), ('unicode', c.c_uint16)]
class DebugUnion(c.Union):
    _fields_ = [('exception', ExceptionInfo), ('process', ProcessInfo), ('thread', ThreadInfo), ('dll', DllInfo), ('raw', c.c_byte * 160)]
class DebugEvent(c.Structure):
    _fields_ = [('code', U32), ('pid', U32), ('tid', U32), ('data', DebugUnion)]
class ThreadEntry(c.Structure):
    _fields_ = [(name, U32) for name in ('size', 'uses', 'tid', 'pid')] + [('base', c.c_int32), ('delta', c.c_int32), ('flags', U32)]
class MemoryInfo(c.Structure):
    _fields_ = [('base', PTR), ('allocation', PTR), ('allocation_protect', U32), ('region_size', c.c_size_t), ('state', U32), ('protect', U32), ('type', U32)]


class WindowsBackend:
    def __init__(self, pid, dll, exe, exe_sha256, terminate_allowed=False, timeout_ms=5000):
        if os.name != 'nt' or c.sizeof(PTR) != 8 or c.sizeof(DebugEvent) != 176:
            raise RuntimeError('The installer requires native 64-bit Windows Python')
        self.pid, self.dll, self.exe = pid, dll.resolve(), exe.resolve()
        self.exe_sha256, self.timeout_ms = exe_sha256.lower(), timeout_ms
        self.k = c.WinDLL('kernel32', use_last_error=True)
        declarations = {
            'OpenProcess': ([U32, c.c_int, U32], PTR), 'CloseHandle': ([PTR], c.c_int),
            'ReadProcessMemory': ([PTR, PTR, PTR, c.c_size_t, c.POINTER(c.c_size_t)], c.c_int),
            'WriteProcessMemory': ([PTR, PTR, PTR, c.c_size_t, c.POINTER(c.c_size_t)], c.c_int),
            'VirtualProtectEx': ([PTR, PTR, c.c_size_t, U32, c.POINTER(U32)], c.c_int),
            'VirtualQueryEx': ([PTR, PTR, c.POINTER(MemoryInfo), c.c_size_t], c.c_size_t),
            'FlushInstructionCache': ([PTR, PTR, c.c_size_t], c.c_int),
            'DebugActiveProcess': ([U32], c.c_int), 'DebugSetProcessKillOnExit': ([c.c_int], c.c_int),
            'WaitForDebugEvent': ([c.POINTER(DebugEvent), U32], c.c_int),
            'ContinueDebugEvent': ([U32, U32, U32], c.c_int), 'DebugActiveProcessStop': ([U32], c.c_int),
            'GetProcessTimes': ([PTR] + [c.POINTER(U64)] * 4, c.c_int),
            'QueryFullProcessImageNameW': ([PTR, U32, c.c_wchar_p, c.POINTER(U32)], c.c_int),
            'CreateToolhelp32Snapshot': ([U32, U32], PTR),
            'Thread32First': ([PTR, c.POINTER(ThreadEntry)], c.c_int), 'Thread32Next': ([PTR, c.POINTER(ThreadEntry)], c.c_int),
            'OpenThread': ([U32, c.c_int, U32], PTR), 'GetProcessIdOfThread': ([PTR], U32),
            'GetThreadContext': ([PTR, PTR], c.c_int), 'TerminateProcess': ([PTR, U32], c.c_int),
            'WaitForSingleObject': ([PTR, U32], U32), 'GetProcessId': ([PTR], U32),
        }
        for name, (arguments, result) in declarations.items():
            function = getattr(self.k, name);function.argtypes = arguments;function.restype = result
        self.terminate_allowed = terminate_allowed
        self.handle = self.k.OpenProcess(0x1038 | 0x100000 | (1 if terminate_allowed else 0), False, pid)
        if not self.handle:
            raise c.WinError(c.get_last_error())
        root = Path(__file__).resolve().parents[2]
        sys.path.insert(0, str(root / 'tools/swimming_tests'))
        from live_symbols import Module
        self.Module = Module
        self.k.Module32FirstW.argtypes = self.k.Module32NextW.argtypes = [PTR, c.POINTER(Module)]
        self.k.Module32FirstW.restype = self.k.Module32NextW.restype = c.c_int
        try:
            self.identity()
            ntdll = next((module for module in self.modules() if module[2].name.lower() == 'ntdll.dll'), None)
            if not ntdll:
                raise ValueError('Target ntdll module is missing')
            self.attach_breakpoint = ntdll[0] + export_rva(ntdll[2], 'DbgBreakPoint')
            if self.read(self.attach_breakpoint, 1) != b'\xcc':
                raise ValueError('Target attach breakpoint instruction differs')
        except BaseException:
            self.close()
            raise

    def checked(self, result):
        if not result:
            raise c.WinError(c.get_last_error())
        return result

    def modules(self):
        snapshot = self.k.CreateToolhelp32Snapshot(0x18, self.pid)
        if snapshot == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())
        module = self.Module();module.size = c.sizeof(module);result = []
        try:
            more = self.k.Module32FirstW(snapshot, c.byref(module))
            while more:
                if len(result) >= 4096:
                    raise ValueError('Module inventory exceeds bound')
                result.append((module.base, module.bytes, Path(module.path).resolve()))
                more = self.k.Module32NextW(snapshot, c.byref(module))
            if c.get_last_error() != 18:
                raise c.WinError(c.get_last_error())
        finally:
            self.checked(self.k.CloseHandle(snapshot))
        return result

    def identity(self):
        text = c.create_unicode_buffer(32768);length = U32(len(text))
        self.checked(self.k.QueryFullProcessImageNameW(self.handle, 0, text, c.byref(length)))
        exe = Path(text.value).resolve()
        if exe != self.exe or exe.name.lower() != 'cyberpunk2077.exe' or self.k.GetProcessId(self.handle) != self.pid:
            raise ValueError('PID is not the exact expected Cyberpunk2077 executable')
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        if digest(exe) != self.exe_sha256:
            raise ValueError('Expected executable SHA256 differs')
        creation, exited, kernel, user = U64(), U64(), U64(), U64()
        self.checked(self.k.GetProcessTimes(self.handle, c.byref(creation), c.byref(exited), c.byref(kernel), c.byref(user)))
        modules = self.modules()
        game = next((item for item in modules if item[2] == exe), None)
        dll = next((item for item in modules if item[2].name.lower() == self.dll.name.lower()), None)
        if not game or not dll or digest(dll[2]) != digest(self.dll):
            raise ValueError('Exact live game/DLL identity differs')
        return Identity(self.pid, creation.value, game[0], game[1], dll[0], dll[1], str(exe), self.exe_sha256, digest(self.dll))

    def read(self, address, count):
        buffer = c.create_string_buffer(count);done = c.c_size_t()
        self.checked(self.k.ReadProcessMemory(self.handle, address, buffer, count, c.byref(done)))
        if done.value != count:
            raise OSError('ReadProcessMemory returned a short read')
        return buffer.raw

    def write(self, address, data):
        buffer = c.create_string_buffer(data);done = c.c_size_t()
        self.checked(self.k.WriteProcessMemory(self.handle, address, buffer, len(data), c.byref(done)))
        if done.value != len(data):
            raise OSError('WriteProcessMemory returned a partial write')

    def protection(self, address):
        memory = MemoryInfo()
        if self.k.VirtualQueryEx(self.handle, address, c.byref(memory), c.sizeof(memory)) != c.sizeof(memory) or memory.state != 0x1000:
            raise OSError('VirtualQueryEx did not identify committed memory')
        return memory.protect

    def protect(self, address, count, protection):
        old = U32();self.checked(self.k.VirtualProtectEx(self.handle, address, count, protection, c.byref(old)));return old.value

    def flush(self, address, count):
        self.checked(self.k.FlushInstructionCache(self.handle, address, count))

    def validate_thunk(self, plan):
        expected = pe_bytes(self.dll, plan.words[8] - plan.words[9], plan.words[13])
        if self.read(plan.words[8], plan.words[13]) != expected or self.protection(plan.words[8]) != 0x20:
            raise ValueError('Prepared thunk differs from exact matching DLL bytes/protection')

    def attach(self, pid):
        self.checked(self.k.DebugActiveProcess(pid))

    def kill_on_exit(self, enabled):
        self.checked(self.k.DebugSetProcessKillOnExit(enabled))

    def wait_event(self):
        raw = DebugEvent();self.checked(self.k.WaitForDebugEvent(c.byref(raw), self.timeout_ms))
        fields = {};handles = ()
        if raw.code == EXCEPTION:
            fields = dict(exception=raw.data.exception.record.code, first_chance=raw.data.exception.first, address=raw.data.exception.record.address)
        elif raw.code == CREATE_PROCESS:
            # Debug-provided process/thread handles are closed by Windows on
            # EXIT continuation. Only the image-file handle belongs here.
            handles = (raw.data.process.file,)
            fields['start_address'] = raw.data.process.start or 0
        elif raw.code == CREATE_THREAD:
            handles = ()
        elif raw.code == LOAD_DLL:
            handles = (raw.data.dll.file,)
        return Event(raw.code, raw.pid, raw.tid, handles=handles, **fields)

    def close_event_handles(self, event):
        for handle in set(event.handles):
            if handle and handle != c.c_void_p(-1).value:
                self.checked(self.k.CloseHandle(handle))

    def continue_event(self, event, disposition):
        self.checked(self.k.ContinueDebugEvent(event.pid, event.tid, disposition))

    def detach(self, pid):
        self.checked(self.k.DebugActiveProcessStop(pid))

    def thread_ids(self):
        snapshot = self.k.CreateToolhelp32Snapshot(4, 0)
        if snapshot == c.c_void_p(-1).value:
            raise c.WinError(c.get_last_error())
        entry = ThreadEntry();entry.size = c.sizeof(entry);result = set()
        try:
            more = self.k.Thread32First(snapshot, c.byref(entry))
            while more:
                if entry.pid == self.pid:
                    result.add(entry.tid)
                    if len(result) > 16384:
                        raise ValueError('Live thread inventory exceeds bound')
                more = self.k.Thread32Next(snapshot, c.byref(entry))
            if c.get_last_error() != 18:
                raise c.WinError(c.get_last_error())
        finally:
            self.checked(self.k.CloseHandle(snapshot))
        return result

    def thread_rips(self):
        before = self.thread_ids();result = {}
        for tid in sorted(before):
            thread = self.checked(self.k.OpenThread(8 | 0x800, False, tid))
            try:
                if self.k.GetProcessIdOfThread(thread) != self.pid:
                    raise ValueError('Thread owner changed')
                storage = c.create_string_buffer(1232 + 15);aligned = (c.addressof(storage) + 15) & ~15
                c.c_uint32.from_address(aligned + 48).value = 0x100001
                self.checked(self.k.GetThreadContext(thread, aligned))
                rip = c.c_uint64.from_address(aligned + 248).value
                if not rip:
                    raise ValueError('A live thread has no readable x64 RIP')
                result[tid] = rip
            finally:
                self.checked(self.k.CloseHandle(thread))
        if before != self.thread_ids():
            raise ValueError('Live thread inventory changed while debugger event held')
        return result

    def terminate(self):
        if not self.terminate_allowed:
            raise ValueError('Termination policy was not authorized')
        self.checked(self.k.TerminateProcess(self.handle, 0xc0000409))

    def wait_exited(self, timeout_ms=5000):
        result = self.k.WaitForSingleObject(self.handle, timeout_ms)
        if result not in (0, 0x102):
            raise c.WinError(c.get_last_error())
        return result == 0

    def close(self):
        if self.handle:
            self.checked(self.k.CloseHandle(self.handle));self.handle = None


def save_state(path, result):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result), flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--nonce', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--owned-game', action='store_true')
    parser.add_argument('--terminate-on-unrecoverable', action='store_true')
    args = parser.parse_args(argv)
    if args.pid <= 0 or not 0 < args.nonce < (1 << 64) or len(args.exe_sha256) != 64 or any(char not in '0123456789abcdefABCDEF' for char in args.exe_sha256):
        parser.error('Positive PID/nonce and exact executable SHA256 are required')
    if args.terminate_on_unrecoverable and not args.owned_game:
        parser.error('--terminate-on-unrecoverable also requires --owned-game')
    root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(root / 'tools/swimming_tests'))
    from live_symbols import resolve
    resolved = resolve(args.pid, args.dll.resolve(), list(SYMBOLS))
    symbols = {name: int(resolved[name], 16) for name in SYMBOLS}
    backend = WindowsBackend(args.pid, args.dll, args.exe, args.exe_sha256, args.owned_game and args.terminate_on_unrecoverable)
    try:
        actual = backend.identity()
        if actual.dll_sha256 != resolved['sha256'] or actual.dll_base != int(resolved['module_base'], 16):
            raise ValueError('DLL identity changed after matching symbol resolution')
        result = install(backend, symbols, expected_nonce=args.nonce)
    except HeldRecovery as held:
        session = held.session
        while True:
            # No finally/detach/exit path is allowed while this process is live
            # with uncertain executable code. EOF and Ctrl-C retain the helper.
            try:
                save_state(args.out, session.state('unsafe_failure_recovery_required'))
                command = ('retry-detach' if session.code_valid else 'terminate') if args.owned_game and args.terminate_on_unrecoverable else input('Recovery command (retry-rollback / retry-detach): ').strip()
                result = recover_once(session, command, owned_game=args.owned_game,
                                      terminate_on_unrecoverable=args.terminate_on_unrecoverable)
                break
            except (EOFError, KeyboardInterrupt):
                try:
                    time.sleep(1)
                except KeyboardInterrupt:
                    pass
            except BaseException as error:
                session.error = str(error)
                try:
                    time.sleep(1)
                except KeyboardInterrupt:
                    pass
    except BaseException:
        backend.close()
        raise
    save_state(args.out, result)
    backend.close()
    return 0 if result['outcome'] == 'committed_detached_accept_requested' else 1


if __name__ == '__main__':
    raise SystemExit(main())
