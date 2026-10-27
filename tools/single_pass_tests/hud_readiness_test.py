from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import hud_readiness as hud


class NativeHudTests(unittest.TestCase):
    def test_repeated_fresh_samples_do_not_count_or_reset_progress(self):
        stable=hud.StableHud()
        def sample(now,frame):
            return dict(now=now,menu=0,tracked=1,layers=2,serial=frame,sourceTick=now-now%200,
                        prepared=10+now//16,submitted=10+now//16,displayTime=100+now)
        self.assertFalse(stable.observe(sample(1000,1)))
        self.assertFalse(stable.observe(sample(1100,1)))
        self.assertEqual(stable.report()['stableSamples'],1)
        for now in range(1200,2000,100):self.assertFalse(stable.observe(sample(now,1+(now-1000)//200)))
        self.assertTrue(stable.observe(sample(2000,6)))
        self.assertEqual(stable.report()['stableSamples'],6)
        self.assertEqual(stable.report()['sourceSerialAdvances'],5)

    def test_duplicate_sources_stalling_past_bound_restart_interval(self):
        stable=hud.StableHud()
        def sample(now,serial):
            return dict(now=now,menu=0,tracked=1,layers=1,serial=serial,sourceTick=now,
                        prepared=now,submitted=now,displayTime=now)
        self.assertFalse(stable.observe(sample(1000,1)))
        self.assertFalse(stable.observe(sample(1100,1)))
        self.assertFalse(stable.observe(sample(1400,1)))
        self.assertEqual(stable.report()['stableMilliseconds'],0)
        for now in range(1600,2400,200):self.assertFalse(stable.observe(sample(now,1+(now-1400)//200)))
        self.assertTrue(stable.observe(sample(2400,6)))

    def setUp(self):
        self.now = 0.0
        self.writes = []
        self.menu = 0
        self.serial_frozen = False
        self.submission_frozen = False
        self.source_stale = False
        self.source_future = False
        self.sequence_odd = False
        self.sequence_torn = False
        self.sequence_reads = 0
        self.fail_restore = False
        self.reader = Mock()
        self.reader.handle = 12
        self.reader.k.OpenProcess.return_value = 13
        self.reader.k.GetTickCount64.side_effect = lambda: 1000 + int(self.now * 1000)
        self.reader.k.WaitForSingleObject.return_value = 258  # WAIT_TIMEOUT: process still alive
        self.reader.k.WriteProcessMemory.side_effect = self.write
        self.reader.read.side_effect = self.read
        names = ['CyberpunkVR_RuntimeDiagnostics', 'g_menuModeValue', 'CyberpunkVR_HudPoseDebug',
                 'CyberpunkVR_HudPoseDebugSeq', 'CyberpunkVR_HudSubmittedFrames']
        self.symbols = {name: hex(i + 1) for i, name in enumerate(names)}
        for patcher in (
                patch.dict(sys.modules, live_read=SimpleNamespace(Reader=Mock(return_value=self.reader)),
                           live_symbols=SimpleNamespace(resolve=Mock(return_value=self.symbols))),
                patch.object(hud.time, 'monotonic', side_effect=lambda: self.now),
                patch.object(hud.time, 'sleep', side_effect=self.sleep)):
            patcher.start()
            self.addCleanup(patcher.stop)

    def sleep(self, seconds):
        self.now += seconds

    def write(self, handle, address, data, size, done):
        self.writes.append(data._obj.value)
        done._obj.value = size
        if self.fail_restore and data._obj.value == 0:
            raise OSError('restore failed')
        return True

    def read(self, address, size):
        frame = 10 + int(self.now * 60)
        if address == 1:
            return struct.pack('<I', 0)
        if address == 2:
            return struct.pack('<I', self.menu)
        if address == 4:
            self.sequence_reads += 1
            return struct.pack('<I', 1 if self.sequence_odd else
                               self.sequence_reads * 2 if self.sequence_torn else 2)
        if address == 5:
            return struct.pack('<Q', 10 if self.submission_frozen else frame)
        data = bytearray(96)
        struct.pack_into('<Qq', data, 0, frame, 100 + int(self.now * 1000))
        struct.pack_into('<ii', data, 36, 1, 2)
        tick = 990 + int(self.now * 1000)
        if self.source_stale:
            tick = 1
        if self.source_future:
            tick += 1000
        struct.pack_into('<QQ', data, 80, 1 if self.serial_frozen else frame, tick)
        return bytes(data)

    def wait(self, check=lambda: True, timeout=3):
        return hud.wait_for_stable_hud(77, Path('C:/game'), timeout, check)

    def assert_closed(self):
        self.assertEqual(self.writes, [1, 0])
        self.reader.k.CloseHandle.assert_called_once_with(13)
        self.reader.close.assert_called_once()

    def test_live_sources_and_submissions_required_and_controls_restored(self):
        check = Mock(return_value=True)
        report = self.wait(check)
        self.assertGreaterEqual(report['stableMilliseconds'], 1000)
        self.assertGreaterEqual(report['submittedFrames'], 30)
        self.assertGreater(report['sourceSerialAdvances'], 0)
        self.assertGreaterEqual(check.call_count, 10)
        self.assert_closed()

    def test_frozen_texture_does_not_pass_even_with_successful_submissions(self):
        self.serial_frozen = True
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_fresh_preparation_without_successful_submissions_does_not_pass(self):
        self.submission_frozen = True
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_advancing_serial_with_stale_source_does_not_pass(self):
        self.source_stale = True
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_source_from_future_does_not_pass(self):
        self.source_future = True
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_hud_stall_restarts_full_stability_interval(self):
        def check():
            self.serial_frozen = .8 < self.now < 1.3
            return True
        report = self.wait(check)
        self.assertGreaterEqual(self.now, 2.2)
        self.assertGreaterEqual(report['stableMilliseconds'], 1000)
        self.assert_closed()

    def test_native_menu_does_not_pass(self):
        self.menu = 1
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_incoherent_publication_does_not_pass(self):
        self.sequence_torn = True
        with self.assertRaises(TimeoutError):
            self.wait()
        self.assert_closed()

    def test_busy_publication_keeps_crash_checks_active(self):
        self.sequence_odd = True
        check = Mock(side_effect=[True, RuntimeError('crash')])
        with self.assertRaisesRegex(RuntimeError, 'crash'):
            self.wait(check)
        self.assert_closed()

    def test_world_leaving_resets_hud_interval(self):
        def check():
            return not .8 < self.now < 1.1
        report = self.wait(check)
        self.assertGreaterEqual(self.now, 2.1)
        self.assertGreaterEqual(report['stableMilliseconds'], 1000)
        self.assert_closed()

    def test_world_rechecked_before_publishing_readiness(self):
        calls = 0
        def check():
            nonlocal calls
            calls += 1
            # First candidate reaches stability at 1.1s, then the final check fails.
            return calls != 13
        report = self.wait(check)
        self.assertGreaterEqual(self.now, 2.1)
        self.assertGreaterEqual(report['stableMilliseconds'], 1000)
        self.assert_closed()

    def test_failed_restore_in_live_process_is_not_success(self):
        self.fail_restore = True
        with self.assertRaisesRegex(OSError, 'restore failed'):
            self.wait()
        self.assert_closed()

    def test_exited_process_does_not_mask_original_crash(self):
        self.fail_restore = True
        self.reader.k.WaitForSingleObject.return_value = 0
        with self.assertRaisesRegex(RuntimeError, 'crash'):
            self.wait(Mock(side_effect=RuntimeError('crash')))
        self.assert_closed()


if __name__ == '__main__':
    unittest.main()
