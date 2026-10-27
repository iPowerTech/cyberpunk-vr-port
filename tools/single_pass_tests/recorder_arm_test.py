import unittest

from read_preparation import wait_recorder_armed


class RecorderArmTest(unittest.TestCase):
    def setUp(self):self.now=0.
    def clock(self):return self.now
    def sleep(self,seconds):self.now+=seconds
    def wait(self,rows,alive=lambda:True):
        iterator=iter(rows);current=None
        def read(name):
            nonlocal current
            if name=='Request':current=next(iterator)
            return current[0 if name=='Request' else 1]
        return wait_recorder_armed(read,alive,timeout=.04,monotonic=self.clock,sleep=self.sleep)

    def test_waits_for_actual_request_consumption_and_native_initialization(self):
        result=self.wait([(1,0),(1,0),(0,0),(0,1)])
        self.assertTrue(result['requestConsumed']);self.assertEqual(result['state'],1)

    def test_old_running_state_with_new_request_is_not_armed(self):
        self.assertEqual(self.wait([(1,1),(0,1)])['request'],0)
        self.assertGreater(self.now,0)

    def test_rejected_initialization_never_starts_trace(self):
        with self.assertRaises(RuntimeError):self.wait([(0,4)])

    def test_game_exit_is_not_ready(self):
        with self.assertRaises(RuntimeError):self.wait([],alive=lambda:False)

    def test_uninitialized_recorder_times_out(self):
        with self.assertRaises(TimeoutError):self.wait([(1,0)]*8)


if __name__=='__main__':unittest.main()
