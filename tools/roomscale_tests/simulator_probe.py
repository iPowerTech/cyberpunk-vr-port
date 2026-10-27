"""Bounded OpenXR Simulator pose-channel check. No keys, focus or game-memory writes."""
import argparse
import json
import os
from pathlib import Path
import time
import uuid
import math


def read_status(directory):
    path = directory / 'runtime_status.json'
    for _ in range(30):
        try:
            return json.loads(path.read_text(encoding='utf-8'))
        except (OSError, json.JSONDecodeError):
            time.sleep(.02)
    raise RuntimeError('Cannot read simulator status')


def send(directory, pose):
    target = directory / 'head_pose_command.json'
    temporary = directory / ('head_pose_command.json.' + uuid.uuid4().hex + '.writing')
    temporary.write_text(json.dumps(pose), encoding='ascii')
    for _ in range(50):
        try:
            os.replace(temporary, target)
            break
        except PermissionError:
            time.sleep(.01)
    else:
        raise RuntimeError('Simulator command file stayed locked')
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        if not target.exists():
            return
        time.sleep(.01)
    raise RuntimeError('Simulator did not acknowledge pose command')


def await_pose(directory, pose):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        status = read_status(directory)
        p = status['head_tracking']['position']
        if (all(abs(p[axis] - pose[axis]) < .001 for axis in ('x', 'y', 'z')) and
            all(abs(math.remainder(status['head_tracking'][axis]-pose[axis], 2*math.pi)) < .001
                for axis in ('yaw','pitch','roll'))):
            return status
        time.sleep(.05)
    raise RuntimeError('Simulator status did not confirm requested position')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--directory', type=Path,
                        default=Path(os.environ['LOCALAPPDATA']) / 'OpenXR-Simulator')
    parser.add_argument('--pid', type=int)
    parser.add_argument('--debug-address', type=lambda s: int(s, 0))
    parser.add_argument('--sequence-address', type=lambda s: int(s, 0))
    parser.add_argument('--exe', type=Path, default=Path(
        'C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe'))
    args = parser.parse_args()
    if not args.out.parent.is_dir() or args.out.exists() or not args.directory.is_dir():
        raise RuntimeError('Existing output parent and simulator directory, fresh output file required')
    initial = read_status(args.directory)
    head = initial['head_tracking']
    # This smoke test is deliberately translation-only at the known neutral pose.
    # Avoid assumptions about nonzero status-angle units.
    if any(abs(head[key]) > .0001 for key in ('yaw', 'pitch', 'roll')):
        raise RuntimeError('Smoke test requires neutral angles; no command sent')
    original = {**head['position'], 'yaw': 0.0, 'pitch': 0.0, 'roll': 0.0}
    shifted = {**original, 'x': original['x'] + .02}
    result = {'initial': initial, 'original_pose': original,
              'requested_pose': shifted, 'focus_changed': False, 'keys_sent': False}
    reader = None
    supplied = (args.pid, args.debug_address, args.sequence_address)
    if any(value is not None for value in supplied):
        if not all(value is not None for value in supplied):
            raise RuntimeError('PID, debug address and sequence address must be provided together')
        from live_read import Reader
        reader = Reader(args.pid, args.exe, args.debug_address, args.sequence_address)
        result['pid'] = args.pid
    try:
        if reader:
            result['game_before'] = reader.sample()
        send(args.directory, shifted)
        result['shifted'] = await_pose(args.directory, shifted)
        if reader:
            time.sleep(.35)
            result['game_shifted'] = reader.sample()
            time.sleep(.65)
            result['game_held'] = reader.sample()
    finally:
        try:
            send(args.directory, original)
            result['restored'] = await_pose(args.directory, original)
            if reader:
                time.sleep(.35)
                result['game_restored'] = reader.sample()
        finally:
            if reader:
                reader.close()
            args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
