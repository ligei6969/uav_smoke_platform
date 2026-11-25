"""Run a recorded-video worker with a process-level deadline on Linux.

This protects the caller even when a native decoder/DNN call cannot observe a
Python/C++ cancellation flag. It does not reconnect a camera or move hardware.
Example: python3 run_guarded.py --timeout-sec 300 -- ./smoke_analyze_video ...
"""
import argparse
import math
import os
import signal
import subprocess
import sys


def run_guarded(command, timeout_sec, grace_sec):
    if os.name != 'posix':
        raise ValueError('process-group watchdog requires POSIX')
    if not command or not all(math.isfinite(v) and 0 < v <= 86400
                              for v in (timeout_sec, grace_sec)):
        raise ValueError('command and finite positive deadlines are required')
    process = subprocess.Popen(command, start_new_session=True)

    def terminate_group():
        # Only signal the process group created by this invocation.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=grace_sec)
        except subprocess.TimeoutExpired:
            pass
        # Also remove descendants if the group leader exited before them.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait(timeout=grace_sec)

    try:
        return process.wait(timeout=timeout_sec)
    except subprocess.TimeoutExpired:
        terminate_group()
        print('video_watchdog: worker deadline exceeded; partial outputs are incomplete',
              file=sys.stderr)
        return 124
    except BaseException:
        terminate_group()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--timeout-sec', type=float, default=300)
    parser.add_argument('--grace-sec', type=float, default=2)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    try:
        result = run_guarded(command, args.timeout_sec, args.grace_sec)
        return result if result >= 0 else 128 - result
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    sys.exit(main())
