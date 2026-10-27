"""Bounded background simulator trajectories with read-only CCT observations."""
import argparse
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

from live_read import Reader
from simulator_probe import await_pose, read_status, send


def delta(a, b):
    return [y-x for x, y in zip(a, b)]


def planar(v):
    return math.hypot(v[0], v[1])


def wrapped(angle):
    return math.remainder(angle, 2*math.pi)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--debug-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--sequence-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--entity-position-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--entity-quat-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--realign-address', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--action-address', type=lambda s: int(s, 0))
    parser.add_argument('--feature-address', type=lambda s: int(s, 0))
    parser.add_argument('--exe-base', type=lambda s: int(s, 0))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--scenario', choices=('right', 'forward', 'vertical', 'turn-walk', 'wall'), required=True)
    parser.add_argument('--distance', type=float, default=.30)
    parser.add_argument('--duration', type=float, default=2.0)
    parser.add_argument('--native-pulse', action='store_true', help='One logged native W pulse during the outbound physical trajectory')
    parser.add_argument('--native-pulse-seconds', type=float, default=.08)
    parser.add_argument('--directory', type=Path,
                        default=Path(os.environ['LOCALAPPDATA']) / 'OpenXR-Simulator')
    parser.add_argument('--exe', type=Path, default=Path(
        'C:/Program Files (x86)/Steam/steamapps/common/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe'))
    args = parser.parse_args()
    feature_args = (args.action_address, args.feature_address, args.exe_base)
    if any(v is not None for v in feature_args) and not all(v is not None for v in feature_args):
        raise ValueError('Action, feature and EXE base addresses must be provided together')
    if not args.out.parent.is_dir() or args.out.exists():
        raise RuntimeError('Fresh output under an existing parent required')
    if not .02 <= args.distance <= .5 or not 1 <= args.duration <= 5:
        raise ValueError('Bounded probe: 0.02..0.5 metres over 1..5 seconds')
    if args.native_pulse and (args.scenario != 'right' or args.distance > .3):
        raise ValueError('Native pulse is limited to a short rightward physical trajectory')
    if not .03 <= args.native_pulse_seconds <= .18:
        raise ValueError('Native pulse duration must be 0.03..0.18 seconds')
    status = read_status(args.directory)
    head = status['head_tracking']
    if any(abs(head[key]) > .0001 for key in ('yaw', 'pitch', 'roll')):
        raise RuntimeError('Start with neutral angles; no command sent')
    original = {**head['position'], 'yaw': 0.0, 'pitch': 0.0, 'roll': 0.0}
    reader = Reader(args.pid, args.exe, args.debug_address, args.sequence_address)
    rows = []
    result = {'pid': args.pid, 'scenario': args.scenario, 'distance': args.distance,
              'duration': args.duration, 'initial_status': status,
              'original_pose': original, 'focus_changed': bool(args.native_pulse), 'keys_sent': bool(args.native_pulse),
              'auxiliary_note': 'Entity pose/realign reads are outside the diagnostic seqlock; compare settled endpoints.',
              'samples': rows}
    current = dict(original)
    baseline = None
    keyboard = None
    keyboard_log = args.out.with_suffix('.input.jsonl')

    def sample(label):
        row = reader.sample()
        row.update(label=label, commanded=dict(current))
        row['entity_position'] = struct.unpack('<3f', reader.read(args.entity_position_address, 12))
        q = struct.unpack('<4f', reader.read(args.entity_quat_address, 16))
        row['entity_quaternion'] = q
        row['entity_yaw'] = 2*math.atan2(q[2], q[3])
        row['realign'] = struct.unpack('<f', reader.read(args.realign_address, 4))[0]
        if args.action_address:
            action = reader.read(args.action_address, 0xb0)
            feature = reader.read(args.feature_address, 0x90)
            if (struct.unpack_from('<Q', action)[0] != args.exe_base+0x2babf38 or
                struct.unpack_from('<Q', action, 0xa8)[0] != args.feature_address or
                struct.unpack_from('<Q', feature)[0] != args.exe_base+0x2bacb90):
                raise RuntimeError('Locomotion feature identity changed')
            row['move_xy'] = struct.unpack_from('<2f', action, 0x90)
            row['feature'] = dict(direction=struct.unpack_from('<3f', feature, 0x40),
                                  speed=struct.unpack_from('<f', feature, 0x50)[0],
                                  desired_speed=struct.unpack_from('<f', feature, 0x54)[0],
                                  vertical_speed=struct.unpack_from('<f', feature, 0x84)[0],
                                  terrain_angle=struct.unpack_from('<f', feature, 0x80)[0])
        rows.append(row)
        if baseline:
            if (row['player'], row['backend'], row['origin']) != (baseline['player'], baseline['backend'], baseline['origin']):
                raise RuntimeError('Player/backend/reference space changed; probe invalidated')
            if planar(delta(baseline['after'], row['after'])) > args.distance*2 + .3:
                raise RuntimeError('Native travel exceeded bounded trajectory')
        if row['gates'] != 15:
            raise RuntimeError(f"Movement gate changed: {row['gates']:x}")
        return row

    def hold(label, seconds=1.2):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            row = sample(label)
            time.sleep(.02)
        return row

    def sweep(target, label, seconds=None):
        nonlocal current, keyboard
        start_pose = dict(current)
        start = time.monotonic()
        duration = seconds or args.duration
        while True:
            t = min(1.0, (time.monotonic()-start)/duration)
            if args.native_pulse and label == 'outbound' and t >= .25 and keyboard is None:
                helper = Path(__file__).resolve().parents[2] / 'render_camera_RE/scripts/native_re_input.py'
                keyboard = subprocess.Popen([sys.executable, '-B', str(helper), '--pid', str(args.pid),
                    '--exe', str(args.exe), '--log', str(keyboard_log), '--keys', 'W', '--seconds', str(args.native_pulse_seconds)],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            u = t*t*(3-2*t)
            current = {k: start_pose[k] + (target[k]-start_pose[k])*u for k in original}
            send(args.directory, current)
            sample(label)
            if t >= 1:
                break
            if args.native_pulse:
                # Do not phase-lock diagnostic reads to the simulator's command
                # acknowledgement. The brief W pulse spans only a few CCT ticks.
                for _ in range(3):
                    time.sleep(.003)
                    sample(label)
            else:
                time.sleep(.01)
        await_pose(args.directory, current)

    error = None
    try:
        baseline = hold('baseline', .5)
        result['baseline'] = baseline
        turn_pose = dict(original)
        if args.scenario == 'turn-walk':
            turn_pose['yaw'] = math.pi
            sweep(turn_pose, 'turn', 3.0)
            turned = hold('turned')
            result['turned'] = turned
        goal = dict(turn_pose)
        if args.scenario == 'right': goal['x'] += args.distance
        elif args.scenario in ('forward', 'wall'): goal['z'] -= args.distance
        elif args.scenario == 'vertical': goal['y'] -= args.distance
        else: goal['z'] += args.distance  # forward in the 180-degree-turned head frame
        sweep(goal, 'outbound')
        reached = hold('held')
        result['reached'] = reached
        sweep(turn_pose, 'return')
        result['returned'] = hold('returned')
        if args.scenario == 'turn-walk':
            sweep(original, 'unturn', 3.0)
            result['unturned'] = hold('unturned')
    except BaseException as exc:
        error = f'{type(exc).__name__}: {exc}'
        result['error'] = error
    finally:
        try:
            if keyboard is not None:
                try:
                    output, _ = keyboard.communicate(timeout=5)
                    result['keyboard'] = {'returncode':keyboard.returncode,'output':output,'log':str(keyboard_log)}
                    if keyboard.returncode:
                        result['keyboard_error'] = 'Native input helper failed; inspect its log'
                except subprocess.TimeoutExpired:
                    result['keyboard_error'] = 'Input helper timed out; explicit release cleanup required'
            # A short final command restores the exact initial simulator pose.
            # Physics is never set/teleported by this harness.
            send(args.directory, original)
            result['final_status'] = await_pose(args.directory, original)
            time.sleep(.4)
            current = dict(original)
            result['final'] = sample('final')
        except BaseException as exc:
            result['cleanup_error'] = str(exc)
        finally:
            reader.close()

    if 'reached' in result:
        b, end, back = result['baseline'], result['reached'], result['returned']
        travel = delta(b['after'], end['after'])
        held = [r for r in rows if r['label'] == 'held']
        stable = held[len(held)//2:]
        drift = max((planar(delta(stable[0]['after'], r['after'])) for r in stable), default=0)
        heading = b['yaw']
        raw_x = args.distance if args.scenario == 'right' else 0
        raw_y = args.distance if args.scenario in ('forward', 'wall') else -args.distance if args.scenario == 'turn-walk' else 0
        expected = [math.cos(heading)*raw_x-math.sin(heading)*raw_y,
                    math.sin(heading)*raw_x+math.cos(heading)*raw_y]
        result['summary'] = {
            'world_delta': travel, 'expected_free_delta': expected,
            'free_space_error': planar(delta(expected, travel[:2])),
            'travel': planar(travel), 'held_drift': drift,
            'held_speed_max': max((planar(r['velocity']) for r in stable), default=0),
            'return_error': planar(delta(b['after'], back['after'])),
            'outbound_injections': end['injected']-b['injected'],
            'injections_after_settling': stable[-1]['injected']-stable[0]['injected'] if stable else None,
            'entity_to_cct_at_hold': planar(delta(end['entity_position'], end['after'])),
            'consumed': delta(b['consumed'], end['consumed'])
        }
        if 'turned' in result:
            turn = result['turned']
            result['summary']['turn_entity_shift'] = planar(delta(b['after'], turn['after']))
            result['summary']['turn_yaw_degrees'] = math.degrees(wrapped(turn['entity_yaw']-b['entity_yaw']))
            result['summary']['turn_realign_degrees'] = math.degrees(wrapped(turn['realign']-b['realign']))
            result['summary']['tracking_basis_change_degrees'] = math.degrees(wrapped(turn['yaw']-b['yaw']))
            result['summary']['final_head_body_yaw_gap_degrees'] = math.degrees(wrapped(math.pi-turn['realign']))
        result['checks'] = {
            'free_space_delta': result['summary']['free_space_error'] < .015,
            'no_held_drift': drift < .001,
            'no_held_speed': result['summary']['held_speed_max'] < .01,
            'no_repeated_held_injections': result['summary']['injections_after_settling'] == 0,
            'return_to_start': result['summary']['return_error'] < .015,
            'entity_matches_cct': result['summary']['entity_to_cct_at_hold'] < .01,
        }
        if 'turned' in result:
            result['checks'].update(
                no_turn_translation=result['summary']['turn_entity_shift'] < .001,
                tracking_axes_stable=abs(result['summary']['tracking_basis_change_degrees']) < .001,
                body_within_deadzone=abs(result['summary']['final_head_body_yaw_gap_degrees']) < 5.05)
        if args.scenario == 'wall':
            along = sum(travel[i]*expected[i] for i in range(2))/args.distance
            retreat = delta(end['after'], back['after'])
            result['summary'].update(outbound_along=along,
                blocked_requested_distance=args.distance-along,
                retreat_distance=planar(retreat),
                retreat_along=-sum(retreat[i]*expected[i] for i in range(2))/args.distance)
            # A wall rejects part of the outward path. The rejected span is NOT
            # re-issued on return; moving the head back immediately walks away.
            del result['checks']['free_space_delta']
            del result['checks']['return_to_start']
            result['checks'].update(
                wall_rejected_movement=along < args.distance-.025,
                full_request_consumed=abs(result['summary']['consumed'][1]-args.distance) < .001,
                moved_away_from_wall=result['summary']['retreat_along'] > args.distance-.025)
        if args.native_pulse:
            del result['checks']['free_space_delta']
            del result['checks']['return_to_start']
            mixed = [r for r in rows if planar(r['native']) > .0001 and planar(r['requested']) > .0001]
            result['summary']['simultaneous_native_physical_samples'] = len(mixed)
            result['summary']['native_requested_xy_peak'] = max(planar(r['native']) for r in rows)
            event = json.loads(keyboard_log.read_text(encoding='utf-8').splitlines()[-1])
            result['checks'].update(mixed_input_observed=len(mixed)>1,
                keyboard_released=not event['release_errors'],
                original_focus_restored=event['restored_original_focus'],
                game_window_targeted=event['title'].startswith('Cyberpunk 2077'),
                head_request_consumed=abs(result['summary']['consumed'][0]-args.distance)<.001)
        if args.feature_address:
            moving = [r for r in rows if r['label']=='outbound' and r['feature']['speed'] > .04]
            result['summary']['animation_speed_max'] = max((r['feature']['speed'] for r in rows), default=0)
            result['summary']['animation_moving_samples'] = len(moving)
            result['summary']['move_xy_peak'] = max((math.hypot(*r['move_xy']) for r in rows), default=0)
            if moving:
                result['summary']['animation_direction_mean'] = [sum(r['feature']['direction'][i] for r in moving)/len(moving) for i in range(3)]
            if not args.native_pulse:
                result['checks']['native_input_stayed_zero'] = result['summary']['move_xy_peak'] < .0001
            result['checks']['physical_movement_reaches_animation'] = len(moving) > 5
            if args.scenario == 'turn-walk' and moving:
                result['checks']['animation_forward_after_turn'] = result['summary']['animation_direction_mean'][1] > .9
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k not in ('samples', 'baseline', 'reached', 'returned', 'turned', 'unturned', 'final')}, indent=2))
    print(f"Recorded {len(rows)} samples -> {args.out}")
    if error or result.get('cleanup_error') or result.get('keyboard_error') or not all(result.get('checks', {}).values()):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
