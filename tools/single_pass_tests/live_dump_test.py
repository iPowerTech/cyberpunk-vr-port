import contextlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'crash'))
import live_dump
import read_dump


class LiveDumpTests(unittest.TestCase):
    def test_external_thread_dump_does_not_terminate_target(self):
        with tempfile.TemporaryDirectory() as directory:
            child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'],
                                     creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                time.sleep(.3)
                path = Path(directory) / 'pending.dmp'
                result = live_dump.capture(child.pid, path)
                self.assertIsNone(child.poll())
                self.assertGreater(result['bytes'], 32)
                self.assertFalse(result['exception'])
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    read_dump.report(str(path), None, threads=True)
                self.assertIn('THREAD STACK SCANS', output.getvalue())
                self.assertIn(' RIP ', output.getvalue())
                self.assertIn('no exception stream', output.getvalue())
                with self.assertRaises(FileExistsError):
                    live_dump.capture(child.pid, path)
            finally:
                child.terminate()
                child.wait(timeout=10)

    def test_self_dump_rejected(self):
        with self.assertRaises(ValueError):
            live_dump.capture(os.getpid(), 'unused.dmp')


if __name__ == '__main__':
    unittest.main()
