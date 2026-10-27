import unittest
from preparation_wait import wait


class PreparationWaitTests(unittest.TestCase):
    def run_wait(self, finish, moving=True, alive=True):
        now = [0.0]
        slow = []
        def read(name):
            return dict(Request=0, State=2 if now[0] >= finish else 1,
                        Seq=int(now[0]) if moving else 2, Count=4, PacketCount=8, Failure=0)[name]
        def sleep(_):
            now[0] += 1
        result = wait(read, lambda: alive, on_slow=slow.append, timeout=30, idle_timeout=15,
                      monotonic=lambda: now[0], sleep=sleep)
        return result, slow

    def test_old_observation_deadline_is_not_a_restart(self):
        result, slow = self.run_wait(12)
        self.assertEqual(result['elapsedSeconds'], 12)
        self.assertEqual(len(slow), 1)
        self.assertTrue(slow[0]['processAlive'])
        self.assertTrue(result['slowObservation'])

    def test_fast_capture_needs_no_dump(self):
        result, slow = self.run_wait(2)
        self.assertEqual(slow, [])
        self.assertFalse(result['slowObservation'])

    def test_idle_limit_reports_live_incomplete_not_exit(self):
        with self.assertRaisesRegex(TimeoutError, 'live process'):
            self.run_wait(100, moving=False)

    def test_progress_has_an_overall_bound(self):
        with self.assertRaisesRegex(TimeoutError, 'live process'):
            self.run_wait(100)

    def test_actual_exit_is_distinct(self):
        with self.assertRaisesRegex(RuntimeError, 'Game exited'):
            self.run_wait(100, alive=False)


if __name__ == '__main__':
    unittest.main()
