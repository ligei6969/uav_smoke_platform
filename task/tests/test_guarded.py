"""Exercise real child processes; never starts a camera, model or SDK."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'tools' / 'run_guarded.py'
if not MODULE.exists():
    MODULE = Path(__file__).with_name('run_guarded.py')
spec = importlib.util.spec_from_file_location('run_guarded', str(MODULE))
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


@unittest.skipUnless(os.name == 'posix', 'Linux process groups')
class WatchdogTests(unittest.TestCase):
    def test_success_and_failure_codes_are_preserved(self):
        for code in (0, 7):
            self.assertEqual(guard.run_guarded(
                [sys.executable, '-c', 'raise SystemExit(%d)' % code], 2, .2), code)

    def test_blocked_worker_exits_with_deadline_status(self):
        started = time.monotonic()
        result = guard.run_guarded(
            [sys.executable, '-c', 'import time; time.sleep(30)'], .2, .2)
        self.assertEqual(result, 124)
        self.assertLess(time.monotonic() - started, 3)

    def test_worker_ignoring_term_is_killed(self):
        with tempfile.TemporaryDirectory() as directory:
            ready = Path(directory) / 'ready'
            code = ('import signal,time; from pathlib import Path; '
                    'signal.signal(signal.SIGTERM, signal.SIG_IGN); '
                    'Path(%r).write_text("ready"); time.sleep(30)' % str(ready))
            self.assertEqual(guard.run_guarded(
                [sys.executable, '-c', code], .5, .2), 124)
            self.assertTrue(ready.exists())

    def test_invalid_limits_rejected_without_launch(self):
        for timeout, grace in ((0, 1), (float('nan'), 1), (1, float('inf'))):
            with self.assertRaises(ValueError):
                guard.run_guarded(['nonexistent-command'], timeout, grace)

    def test_cli_forwards_arguments_without_shell(self):
        process = subprocess.run([
            sys.executable, str(MODULE), '--timeout-sec', '2', '--',
            sys.executable, '-c', 'import sys; print(sys.argv[1])', 'literal;$HOME',
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
           timeout=5)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stdout.strip(), 'literal;$HOME')


if __name__ == '__main__':
    unittest.main()
