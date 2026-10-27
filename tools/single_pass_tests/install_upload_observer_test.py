"""Pure fake-backend transaction fixtures; no Windows APIs or target process."""
import ctypes
from dataclasses import replace
import struct
import unittest
from unittest import mock

import install_upload_observer as installer


class FakeBackend:
    def __init__(self):
        self.current = installer.Identity(77, 88, 0x140000000, 0x2000000,
                                          0x180000000, 0x100000,
                                          'C:/owned/Cyberpunk2077.exe', 'a' * 64, 'b' * 64)
        self.attach_breakpoint = 0x7fff00001000
        self.symbols = {name: self.current.dll_base + 0x10000 + index * 0x1000
                        for index, name in enumerate(installer.SYMBOLS)}
        self.blocks, self.faults, self.counts, self.calls = {}, {}, {}, []
        self.closed, self.continued = [], []
        self.attached, self.held, self.exited = False, None, False
        self.termination_started = False
        self.auto_closed = []
        self.site = self.current.game_base + installer.SITE_RVA
        self.allocator = self.current.game_base + installer.ALLOCATOR_RVA
        self.relay = self.current.game_base + 0x8000000
        self.thunk = self.current.dll_base + 0x2000
        self.page_protection = 0x20
        self.rips = {8: self.attach_breakpoint + 1, 9: self.current.game_base + 0x1000}
        self.events = [installer.Event(installer.CREATE_PROCESS, 77, 8, handles=(101,)),
                       installer.Event(installer.CREATE_THREAD, 77, 9),
                       installer.Event(installer.LOAD_DLL, 77, 8, handles=(105,)),
                       installer.Event(installer.EXCEPTION, 77, 9, 0xc0000005, 1, 0x1234),
                       installer.Event(installer.EXCEPTION, 77, 9, installer.BREAKPOINT, 1, 0x1234),
                       installer.Event(installer.EXCEPTION, 77, 8, installer.BREAKPOINT, 1, self.attach_breakpoint)]
        self.original_guard = bytearray(b'\x90' * 177)
        offset = installer.SITE_RVA - installer.ALLOCATOR_RVA
        self.original_guard[offset:offset + 7] = installer.ORIGINAL
        self.patch = b'\xe8' + struct.pack('<i', self.relay - self.site - 5) + b'\x75\x1f'
        self.relay_code = b'\xff\x25\0\0\0\0' + struct.pack('<Q', self.thunk)
        self.native_fnv = installer.fnv(self.original_guard)
        words = [installer.PLAN_VERSION, 99, 77, 88, self.current.game_base,
                 self.allocator, self.site, self.relay, self.thunk, self.current.dll_base,
                 177, 7, 14, 64, 0x20, self.native_fnv, 0, 0, 0, 0]
        raw = struct.pack('<20Q', *words) + self.original_guard + installer.ORIGINAL + self.patch + self.relay_code + bytes(3)
        raw = raw[:128] + struct.pack('<Q', installer.fnv(raw)) + raw[136:]
        self.blocks[self.allocator] = self.original_guard.copy()
        self.blocks[self.relay] = bytearray(self.relay_code)
        self.blocks[self.thunk] = bytearray(b'\x90' * 64)
        for name, address in self.symbols.items():
            size = installer.PLAN_BYTES if name.endswith('ExternalPlan') else installer.RECEIPT_BYTES if name.endswith('ExternalReceipt') else 4
            self.blocks[address] = bytearray(size)
        self.blocks[self.symbols[installer.PREFIX + 'ExternalPlan']] = bytearray(raw)
        self.blocks[self.symbols[installer.PREFIX + 'ExternalPlanSeq']] = bytearray(struct.pack('<I', 2))
        self.blocks[self.symbols[installer.PREFIX + 'ExternalInstallState']] = bytearray(struct.pack('<I', 1))

    def operation(self, name, *arguments):
        self.calls.append((name, *arguments))
        self.counts[name] = self.counts.get(name, 0) + 1
        mode = self.faults.get((name, self.counts[name]))
        if mode == 'raise':
            raise OSError(name + ' injected failure')
        return mode

    def block(self, address, count):
        for base, data in self.blocks.items():
            if base <= address and address + count <= base + len(data):
                return data, address - base
        raise ValueError('Unmapped fake span')

    def identity(self):
        mode = self.operation('identity')
        return replace(self.current, creation=999) if mode == 'foreign' else self.current

    def read(self, address, count):
        name = 'read_guard' if address == self.allocator and count == 177 else 'read_site' if address == self.site else 'read_receipt' if address == self.symbols[installer.PREFIX + 'ExternalReceipt'] else 'read'
        mode = self.operation(name, address, count)
        data, offset = self.block(address, count);result = bytes(data[offset:offset + count])
        return bytes([result[0] ^ 1]) + result[1:] if mode == 'corrupt' else result

    def write(self, address, raw):
        name = 'write_site' if address == self.site else 'write_receipt' if address == self.symbols[installer.PREFIX + 'ExternalReceipt'] else 'write_seq' if address == self.symbols[installer.PREFIX + 'ExternalReceiptSeq'] else 'write_accept'
        mode = self.operation(name, address, bytes(raw))
        if address == self.site and not self.held:
            raise AssertionError('Code write without outstanding event')
        data, offset = self.block(address, len(raw))
        if mode == 'partial':
            data[offset:offset + 3] = raw[:3]
            raise OSError('partial write')
        data[offset:offset + len(raw)] = raw
        if mode == 'foreign_body':
            self.blocks[self.allocator][0] ^= 1
        if mode == 'after':
            raise OSError('reported failure after write')

    def protection(self, address):
        mode = self.operation('query_protection', address)
        return 0x40 if mode == 'wrong' else self.page_protection if address == self.site else 0x20

    def protect(self, address, count, protection):
        mode = self.operation('protect', address, count, protection)
        if not self.held:
            raise AssertionError('Protection write without outstanding event')
        old = self.page_protection;self.page_protection = protection
        if mode == 'after':
            raise OSError('reported failure after protection change')
        return 0x80 if mode == 'wrong_old' else old

    def flush(self, address, count):
        self.operation('flush', address, count)
        if not self.held:
            raise AssertionError('Code flush without outstanding event')

    def validate_thunk(self, plan):
        self.operation('thunk')

    def attach(self, pid):
        self.operation('attach', pid);self.attached = True

    def kill_on_exit(self, enabled):
        self.operation('kill_on_exit', enabled)

    def wait_event(self):
        self.operation('wait')
        if self.held:
            raise AssertionError('Another event requested while one is held')
        if not self.events:
            raise TimeoutError('No attach breakpoint')
        self.held = self.events.pop(0)
        return self.held

    def close_event_handles(self, event):
        self.operation('close_event', event.code)
        self.closed.extend(event.handles)

    def continue_event(self, event, disposition):
        self.operation('continue', event, disposition)
        if self.held is not event:
            raise AssertionError('Wrong outstanding event continued')
        self.continued.append((event, disposition));self.held = None
        if event.code == installer.EXIT_THREAD:
            self.auto_closed.append(104)
        if event.code == installer.EXIT_PROCESS:
            self.auto_closed.extend((102, 103))
            self.exited = True
            self.attached = False

    def detach(self, pid):
        self.operation('detach', pid)
        if not self.attached:
            raise OSError('DebugActiveProcessStop on already detached target')
        if self.held:
            raise AssertionError('Detached while an outstanding event remains')
        self.attached = False

    def thread_rips(self):
        self.operation('rips');return self.rips.copy()

    def terminate(self):
        mode = self.operation('terminate')
        self.termination_started = True
        if mode != 'pending':
            self.events.extend((installer.Event(installer.EXIT_THREAD, 77, 9), installer.Event(installer.EXIT_PROCESS, 77, 8)))

    def wait_exited(self, timeout_ms=5000):
        self.operation('wait_exited', timeout_ms);return self.exited


class ConstantsAndLayouts(unittest.TestCase):
    def test_native_abi_and_win32_event_values(self):
        self.assertEqual(installer.ALLOCATOR_FNV, 0xBF311A1C73348164)
        self.assertEqual(installer.PLAN_BYTES, 368)
        self.assertEqual(installer.RECEIPT_BYTES, 144)
        self.assertEqual(installer.EXCEPTION, 1)
        self.assertEqual(installer.CREATE_PROCESS, 3)
        self.assertEqual(installer.BREAKPOINT, 0x80000003)
        self.assertEqual(ctypes.sizeof(installer.DebugEvent), 176)
        self.assertEqual(installer.DebugEvent.data.offset, 16)
        self.assertEqual(installer.ExceptionInfo.first.offset, 152)
        self.assertEqual(installer.fnv(b'hello'), 0xa430d84680aabd0b)


class Transactions(unittest.TestCase):
    def setUp(self):
        self.backend = FakeBackend()
        # Synthetic native bytes are confined to fake transactions. Production
        # exposes no switch which changes the fixed native177 FNV guard.
        patcher = mock.patch.object(installer, 'ALLOCATOR_FNV', self.backend.native_fnv)
        patcher.start();self.addCleanup(patcher.stop)

    def run_install(self):
        return installer.install(self.backend, self.backend.symbols, expected_nonce=99)

    def assert_original_detached(self, result):
        self.assertFalse(self.backend.attached)
        self.assertIsNone(self.backend.held)
        self.assertEqual(bytes(self.backend.blocks[self.backend.allocator]), bytes(self.backend.original_guard))
        self.assertEqual(self.backend.page_protection, 0x20)
        self.assertTrue(result['code_valid'])
        self.assertEqual(installer.read32(self.backend, self.backend.symbols[installer.PREFIX + 'AcceptRequest']), 0)

    def test_committed_order_receipt_and_initial_handle_closure(self):
        result = self.run_install()
        self.assertEqual(result['outcome'], 'committed_detached_accept_requested')
        self.assertFalse(self.backend.attached)
        self.assertEqual(self.backend.closed, [101, 105])
        self.assertEqual([status for event, status in self.backend.continued if event.code == installer.EXCEPTION],
                         [installer.NOT_HANDLED, installer.NOT_HANDLED, installer.CONTINUE])
        names = [call[0] for call in self.backend.calls]
        self.assertLess(names.index('kill_on_exit'), names.index('write_site'))
        self.assertLess(names.index('rips'), names.index('write_site'))
        self.assertLess(names.index('write_receipt'), names.index('detach'))
        self.assertLess(names.index('detach'), names.index('write_accept'))
        raw = bytes(self.backend.blocks[self.backend.symbols[installer.PREFIX + 'ExternalReceipt']])
        receipt = struct.unpack('<18Q', raw)
        self.assertEqual(receipt[:8], (installer.RECEIPT_VERSION, 99, struct.unpack_from('<Q', self.backend.blocks[self.backend.symbols[installer.PREFIX + 'ExternalPlan']], 128)[0],
                                      77, 88, 1, installer.EXCEPTION, 8))
        self.assertEqual(receipt[8:17], (0x20, 0x20, 7, 1, 1, 1, 0, installer.BREAKPOINT, 1))
        self.assertEqual(receipt[17], installer.fnv(raw[:136]))
        self.assertEqual(installer.read32(self.backend, self.backend.symbols[installer.PREFIX + 'Enable']), 0)

    def test_corrupt_plan_rejected_before_attach(self):
        raw = self.backend.blocks[self.backend.symbols[installer.PREFIX + 'ExternalPlan']]
        raw[200] ^= 1
        with self.assertRaises(ValueError):
            self.run_install()
        self.assertNotIn('attach', self.backend.counts)

    def test_plan_wrong_identity_original_patch_relay_and_reserved(self):
        address = self.backend.symbols[installer.PREFIX + 'ExternalPlan']
        original = bytes(self.backend.blocks[address])
        for offset in (0, 8, 16, 24, 32, 40, 48, 64, 72, 80, 88, 96, 112, 120, 136, 337, 344, 351, 365):
            with self.subTest(offset=offset):
                raw = bytearray(original);raw[offset] ^= 1
                raw[128:136] = bytes(8);raw[128:136] = struct.pack('<Q', installer.fnv(raw))
                self.backend.blocks[address] = raw
                with self.assertRaises(ValueError):
                    self.run_install()
        self.backend.blocks[address] = bytearray(original)

    def test_zero_and_oversized_thunk_extent_are_rejected(self):
        address = self.backend.symbols[installer.PREFIX + 'ExternalPlan']
        original = bytes(self.backend.blocks[address])
        for count in (0, 4097):
            raw = bytearray(original);struct.pack_into('<Q', raw, 104, count)
            raw[128:136] = bytes(8);struct.pack_into('<Q', raw, 128, installer.fnv(raw))
            self.backend.blocks[address] = raw
            with self.assertRaises(ValueError):
                self.run_install()
        self.backend.blocks[address] = bytearray(original)

    def test_odd_plan_and_active_requests_rejected_before_attach(self):
        for name in ('PrepareRequest', 'AcceptRequest', 'Enable', 'ExternalPlanSeq'):
            with self.subTest(name=name):
                address = self.backend.symbols[installer.PREFIX + name]
                old = self.backend.blocks[address]
                self.backend.blocks[address] = bytearray(struct.pack('<I', 1))
                with self.assertRaises(ValueError):
                    self.run_install()
                self.backend.blocks[address] = old

    def test_idle_accepted_and_rejected_native_states_cannot_be_installed(self):
        address = self.backend.symbols[installer.PREFIX + 'ExternalInstallState']
        for value in (0, 2, 3, 4, 5, 6):
            self.backend.blocks[address] = bytearray(struct.pack('<I', value))
            with self.assertRaises(ValueError):
                self.run_install()
        self.backend.blocks[address] = bytearray(struct.pack('<I', 1))

    def test_prewrite_failures_detach_without_code_writes(self):
        failures = [('attach', 1), ('kill_on_exit', 1), ('wait', 1), ('close_event', 1),
                    ('identity', 2), ('thunk', 1), ('read_guard', 1), ('rips', 1)]
        for operation, count in failures:
            with self.subTest(operation=operation):
                backend = FakeBackend();backend.faults[(operation, count)] = 'foreign' if operation == 'identity' else 'raise'
                result = installer.install(backend, backend.symbols)
                self.assertFalse(backend.attached)
                self.assertIsNone(backend.held)
                self.assertNotIn('write_site', backend.counts)
                self.assertTrue(result['code_valid'])

    def test_all_seven_site_rips_are_rejected(self):
        for offset in range(7):
            backend = FakeBackend();backend.rips[9] = backend.site + offset
            result = installer.install(backend, backend.symbols)
            self.assertFalse(backend.attached)
            self.assertNotIn('write_site', backend.counts)
            self.assertIn('live RIP', result['error'])

    def test_missing_breakpoint_thread_and_empty_rips_rejected(self):
        for rips in ({}, {9: 0x1234}):
            backend = FakeBackend();backend.rips = rips
            result = installer.install(backend, backend.symbols)
            self.assertFalse(backend.attached)
            self.assertNotIn('write_site', backend.counts)
            self.assertIn('live thread', result['error'])

    def test_breakpoint_without_initial_process_is_not_attach_proof(self):
        self.backend.events = self.backend.events[-1:]
        result = self.run_install()
        self.assert_original_detached(result)
        self.assertNotIn('write_site', self.backend.counts)
        self.assertEqual(self.backend.continued[0][1], installer.NOT_HANDLED)

    def test_second_chance_attach_address_is_preserved_not_claimed(self):
        self.backend.events[-1] = replace(self.backend.events[-1], first_chance=0)
        result = self.run_install()
        self.assert_original_detached(result)
        self.assertEqual(self.backend.continued[-1][1], installer.NOT_HANDLED)

    def test_changed_code_relay_or_protection_rejected_before_write(self):
        for kind in ('relay', 'protection'):
            backend = FakeBackend()
            if kind == 'relay':
                backend.blocks[backend.relay][0] ^= 1
            else:
                backend.page_protection = 0x80
            result = installer.install(backend, backend.symbols)
            self.assertFalse(backend.attached)
            self.assertNotIn('write_site', backend.counts)
            self.assertIn('differs', result['error'])

    def test_foreign_native_body_or_site_is_held_without_overwrite_continue_or_detach(self):
        for offset in (0, installer.SITE_RVA - installer.ALLOCATOR_RVA):
            with self.subTest(offset=offset):
                self.backend = FakeBackend();self.backend.blocks[self.backend.allocator][offset] ^= 1
                actual = bytes(self.backend.blocks[self.backend.allocator])
                with self.assertRaises(installer.HeldRecovery) as caught:
                    self.run_install()
                session = caught.exception.session
                self.assertTrue(session.foreign_body)
                self.assertFalse(session.code_valid)
                self.assertIs(session.event, self.backend.held)
                self.assertEqual(len(self.backend.continued), 5)
                self.assertNotIn('write_site', self.backend.counts)
                self.assertNotIn('protect', self.backend.counts)
                self.assertNotIn('detach', self.backend.counts)
                with self.assertRaises(installer.HeldRecovery):
                    installer.recover_once(session, 'retry-rollback')
                self.assertEqual(bytes(self.backend.blocks[self.backend.allocator]), actual)
                self.assertNotIn('write_site', self.backend.counts)

    def test_native_protection_whitelist_accepts_40_and_rejects_execute_only_10(self):
        address = self.backend.symbols[installer.PREFIX + 'ExternalPlan']
        original = bytes(self.backend.blocks[address])
        raw = bytearray(original);struct.pack_into('<Q', raw, 112, 0x10)
        raw[128:136] = bytes(8);struct.pack_into('<Q', raw, 128, installer.fnv(raw))
        self.backend.blocks[address] = raw
        with self.assertRaises(ValueError):
            self.run_install()
        raw = bytearray(original);struct.pack_into('<Q', raw, 112, 0x40)
        raw[128:136] = bytes(8);struct.pack_into('<Q', raw, 128, installer.fnv(raw))
        self.backend.blocks[address] = raw;self.backend.page_protection = 0x40
        result = self.run_install()
        self.assertEqual(result['outcome'], 'committed_detached_accept_requested')
        self.assertEqual(self.backend.page_protection, 0x40)

    def test_foreign_body_outside_owned_site_blocks_rollback_after_our_write(self):
        self.backend.faults[('write_site', 1)] = 'foreign_body'
        with self.assertRaises(installer.HeldRecovery) as caught:
            self.run_install()
        session = caught.exception.session
        self.assertTrue(session.foreign_body)
        self.assertFalse(session.code_valid)
        self.assertEqual(self.backend.counts['write_site'], 1)
        self.assertNotIn('detach', self.backend.counts)
        self.assertEqual(len(self.backend.continued), 5)
        with self.assertRaises(installer.HeldRecovery):
            installer.recover_once(session, 'retry-rollback')
        self.assertEqual(self.backend.counts['write_site'], 1)

    def test_each_changed_stage_failure_rolls_back_before_same_event_continue(self):
        failures = [('protect', 1, 'raise'), ('protect', 1, 'after'), ('protect', 1, 'wrong_old'),
                    ('write_site', 1, 'raise'), ('write_site', 1, 'partial'), ('write_site', 1, 'after'),
                    ('read_guard', 2, 'corrupt'), ('read_site', 2, 'raise'), ('flush', 1, 'raise'),
                    ('protect', 2, 'raise'), ('protect', 2, 'after'), ('query_protection', 4, 'wrong'),
                    ('write_receipt', 1, 'partial'), ('read_receipt', 1, 'corrupt'),
                    ('write_seq', 1, 'raise'), ('write_seq', 2, 'after')]
        for operation, count, mode in failures:
            with self.subTest(operation=operation, count=count, mode=mode):
                self.backend = FakeBackend();self.backend.faults[(operation, count)] = mode
                result = self.run_install();self.assert_original_detached(result)
                self.assertEqual(result['outcome'], 'rolled_back_detached')
                calls = self.backend.calls
                rollback = max(index for index, call in enumerate(calls) if call[0] == 'write_site')
                continued = max(index for index, call in enumerate(calls) if call[0] == 'continue')
                self.assertLess(rollback, continued)
                self.assertEqual(self.backend.continued[-1][0].code, installer.EXCEPTION)
                self.assertEqual(self.backend.continued[-1][0].address, self.backend.attach_breakpoint)

    def test_continue_failure_rolls_back_under_unchanged_event(self):
        self.backend.faults[('continue', 6)] = 'raise'
        result = self.run_install();self.assert_original_detached(result)
        self.assertIn('rollback_verified', result['history'])

    def test_detach_failure_keeps_valid_patch_and_helper_attachment(self):
        self.backend.faults[('detach', 1)] = 'raise'
        with self.assertRaises(installer.HeldRecovery) as caught:
            self.run_install()
        session = caught.exception.session
        self.assertTrue(session.attached)
        self.assertTrue(session.code_valid)
        self.assertIsNone(session.event)
        result = installer.recover_once(session, 'retry-detach')
        self.assertFalse(result['attached'])
        self.assertEqual(result['outcome'], 'valid_detached_unaccepted')

    def test_postdetach_receipt_failure_does_not_claim_outstanding_rollback(self):
        self.backend.faults[('write_receipt', 2)] = 'raise'
        result = self.run_install()
        self.assertEqual(result['outcome'], 'valid_detached_unaccepted')
        self.assertFalse(result['attached'])
        self.assertEqual(self.backend.counts['write_site'], 1)
        self.assertNotIn('write_accept', self.backend.counts)

    def test_accept_partial_write_is_reported_as_unknown(self):
        self.backend.faults[('write_accept', 1)] = 'partial'
        result = self.run_install()
        self.assertEqual(result['outcome'], 'valid_detached_accept_status_unknown')
        self.assertEqual(result['accept_request'], 'uncertain')
        self.assertFalse(result['attached'])

    def held_failure(self, rollback_operation='write_site', rollback_count=2, rollback_mode='partial'):
        self.backend.faults[('flush', 1)] = 'raise'
        self.backend.faults[(rollback_operation, rollback_count)] = rollback_mode
        with self.assertRaises(installer.HeldRecovery) as caught:
            self.run_install()
        return caught.exception.session

    def test_each_rollback_failure_keeps_same_event_without_continue_detach(self):
        cases = [('protect', 2, 'raise'), ('write_site', 2, 'partial'),
                 ('read_guard', 3, 'corrupt'), ('flush', 2, 'raise'), ('protect', 3, 'after')]
        for operation, count, mode in cases:
            with self.subTest(operation=operation):
                self.backend = FakeBackend()
                session = self.held_failure(operation, count, mode)
                self.assertTrue(session.attached)
                self.assertFalse(session.code_valid)
                self.assertIs(session.event, self.backend.held)
                self.assertEqual(session.event.address, self.backend.attach_breakpoint)
                self.assertNotIn('detach', self.backend.counts)
                self.assertEqual(len(self.backend.continued), 5)

    def test_direct_release_cannot_bypass_confirmed_termination(self):
        session = self.held_failure();session.termination_started = True;session.code_valid = True
        before = len(self.backend.continued)
        with self.assertRaises(installer.HeldRecovery):
            session.release()
        self.assertEqual(len(self.backend.continued), before)
        self.assertNotIn('detach', self.backend.counts)

    def test_unrecoverable_without_owned_policy_never_terminates(self):
        session = self.held_failure()
        for owned, stop in ((False, False), (True, False), (False, True)):
            with self.assertRaises(ValueError):
                installer.recover_once(session, 'terminate', owned_game=owned, terminate_on_unrecoverable=stop)
        with self.assertRaises(ValueError):
            installer.recover_once(session, 'retry-detach')
        self.assertTrue(session.attached)
        self.assertIs(session.event, self.backend.held)
        self.assertNotIn('terminate', self.backend.counts)
        self.assertNotIn('detach', self.backend.counts)

    def test_owned_termination_requires_exact_identity_then_verified_exit(self):
        session = self.held_failure()
        result = installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertEqual(result['outcome'], 'owned_game_terminated_verified')
        self.assertTrue(self.backend.exited)
        self.assertFalse(result['attached'])
        self.assertFalse(result['event_held'])
        self.assertNotIn('detach', self.backend.counts)
        names = [call[0] for call in self.backend.calls]
        terminate = names.index('terminate')
        exit_continue = max(index for index, call in enumerate(self.backend.calls) if call[0] == 'continue')
        self.assertLess(terminate, exit_continue)
        self.assertEqual(self.backend.continued[-1][0].code, installer.EXIT_PROCESS)
        self.assertEqual(self.backend.auto_closed, [104, 102, 103])
        self.assertEqual(self.backend.closed, [101, 105])
        self.assertEqual(len(self.backend.continued), 8)

    def test_pending_termination_never_allows_helper_exit_or_detach(self):
        session = self.held_failure();self.backend.faults[('terminate', 1)] = 'pending'
        with self.assertRaises(installer.HeldRecovery):
            installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertTrue(session.attached)
        self.assertIs(session.event, self.backend.held)
        self.assertNotIn('detach', self.backend.counts)
        self.assertTrue(session.termination_started)

    def test_failed_terminate_never_continues_corrupt_code(self):
        session = self.held_failure();self.backend.faults[('terminate', 1)] = 'raise'
        before = len(self.backend.continued)
        with self.assertRaises(OSError):
            installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertFalse(session.termination_started)
        self.assertEqual(len(self.backend.continued), before)
        self.assertIs(session.event, self.backend.held)

    def test_exit_continuation_failure_keeps_pending_event_for_recovery(self):
        session = self.held_failure();self.backend.faults[('continue', 8)] = 'raise'
        with self.assertRaises(installer.HeldRecovery):
            installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertEqual(session.event.code, installer.EXIT_PROCESS)
        self.assertFalse(self.backend.exited)
        self.assertTrue(session.attached)
        result = installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertTrue(self.backend.exited)
        self.assertEqual(result['outcome'], 'owned_game_terminated_verified')

    def test_all_recovery_commands_drain_confirmed_termination_without_detach_or_rollback(self):
        for command in ('retry-detach', 'retry-rollback', 'terminate', 'unknown-command'):
            with self.subTest(command=command):
                self.backend = FakeBackend();session = self.held_failure()
                self.backend.faults[('continue', 7)] = 'raise'
                with self.assertRaises(installer.HeldRecovery):
                    installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
                self.assertTrue(session.termination_started)
                self.assertEqual(session.event.code, installer.EXIT_THREAD)
                session.code_valid = True
                writes = self.backend.counts['write_site']
                result = installer.recover_once(session, command)
                self.assertEqual(result['outcome'], 'owned_game_terminated_verified')
                self.assertEqual(self.backend.counts['write_site'], writes)
                self.assertEqual(self.backend.counts['terminate'], 1)
                self.assertNotIn('detach', self.backend.counts)

    def test_changed_owned_identity_cannot_be_terminated(self):
        session = self.held_failure();self.backend.current = replace(self.backend.current, exe_sha256='c' * 64)
        with self.assertRaises(ValueError):
            installer.recover_once(session, 'terminate', owned_game=True, terminate_on_unrecoverable=True)
        self.assertNotIn('terminate', self.backend.counts)

    def test_explicit_retry_rollback_can_release_only_after_full_verification(self):
        session = self.held_failure()
        result = installer.recover_once(session, 'retry-rollback')
        self.assert_original_detached(result)
        self.assertEqual(result['outcome'], 'rolled_back_detached')

    def test_successful_retry_rollback_with_receipt_failure_never_detaches_twice(self):
        session = self.held_failure();self.backend.faults[('write_receipt', 1)] = 'raise'
        result = installer.recover_once(session, 'retry-rollback')
        self.assert_original_detached(result)
        self.assertEqual(result['outcome'], 'rolled_back_detached_receipt_failed')
        self.assertEqual(self.backend.counts['detach'], 1)
        result = installer.recover_once(session, 'retry-detach')
        self.assertEqual(result['outcome'], 'rolled_back_detached_receipt_failed')
        self.assertEqual(self.backend.counts['detach'], 1)


if __name__ == '__main__':
    unittest.main()
