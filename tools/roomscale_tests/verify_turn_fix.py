"""Validate saved before/after evidence for the HMD/model-offset frame fix."""
import argparse
import base64
import hashlib
import json
import math
from pathlib import Path

from live_read import decode


def rotate(v, yaw):
    co, si = math.cos(yaw), math.sin(yaw)
    return [co*v[0]-si*v[1], si*v[0]+co*v[1], v[2]]


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if not args.out.parent.is_dir() or args.out.exists():
        raise RuntimeError('Fresh report under an existing parent required')
    root = Path(__file__).resolve().parents[2]
    old = root / 'build/roomscale-live-13216-hmd-180-before.json'
    old_data = json.loads(old.read_text(encoding='utf-8'))
    before_change = old_data['summary']['head_view_offset_change_body_local']
    assert before_change > .23
    assert old_data['summary']['camera_minus_head_body_local_after'][1] < -.12
    report = {
        'pid': 19332,
        'dll_sha256': '3bcdd9ee38376d40c3d0dc9abeec3a042fabdac10b2cd0383e11b7e3c5db6200',
        'exe_base': '0x7ff744420000', 'plugin_base': '0x7ffa3ec60000',
        'user_visual_confirmation': 'Теперь нормально',
        'before': {'path': str(old.relative_to(root)), 'sha256': hashlib.sha256(old.read_bytes()).hexdigest(),
                   'body_relative_change_m': before_change},
        'after': {},
        'boundary': 'Settled camera/head relation and CCT position in OpenXR Simulator. The script does not certify every frame of transient animation or new terrain/vehicle behavior.',
    }
    total_frames = 0
    for angle in (20, 180):
        path = root / f'build/roomscale-live-19332-hmd-{angle}-after.json'
        raw = path.read_bytes()
        data = json.loads(raw)
        assert data['pid'] == 19332 and not data.get('error')
        assert not data['focus_changed'] and not data['keys_sent']
        endpoints = {}
        for row in data['samples']:
            snapshot = decode(base64.b64decode(row['raw_base64'], validate=True))
            assert row['publication_sequence'] % 2 == 0
            for key in ('calls', 'injected', 'origin', 'gates'):
                assert row[key] == snapshot[key]
            assert tuple(row['after']) == snapshot['after']
            total_frames += 1
        first = data['before']
        for label in ('before', 'after', 'returned'):
            row = data[label]
            assert row['gates'] == 15
            assert row['after'] == first['after'] and row['injected'] == first['injected']
            assert row['origin'] == first['origin']
            shared = row['shared']
            assert math.dist(shared['head_base'], [0, 0, 0]) < 1e-4
            bake = [a+b for a,b in zip(shared['cam_bake'], shared['eye_bake_model'])]
            assert math.dist(bake, shared['total_offset']) < 1e-4
            observed_delta = sub(row['centre'], row['engine_camera'])
            predicted_delta = rotate(bake, row['entity_yaw'])
            formula_error = math.dist(observed_delta, predicted_delta)
            assert formula_error < .0005, (angle, label, formula_error)
            local = rotate(sub(row['centre'], row['head_world']), -row['entity_yaw'])
            offset_error = math.dist(local, [-.02, .10, .15])
            assert offset_error < .001, (angle, label, local)
            endpoints[label] = {'camera_minus_head_body_local': local, 'model_formula_error_m': formula_error,
                                'error_from_calibrated_offset_m': offset_error,
                                'entity_yaw': row['entity_yaw'], 'tracking_yaw': row['yaw'], 'realign': row['realign']}
        change = math.dist(endpoints['before']['camera_minus_head_body_local'],
                           endpoints['after']['camera_minus_head_body_local'])
        assert change < .001
        assert abs(change - data['summary']['head_view_offset_change_body_local']) < 1e-8
        report['after'][str(angle)] = {
            'path': str(path.relative_to(root)), 'sha256': hashlib.sha256(raw).hexdigest(),
            'snapshots': len(data['samples']), 'body_relative_change_m': change,
            'commanded_head_turn_degrees': math.degrees(data['target']['yaw']-data['original']['yaw']),
            'body_turn_degrees': data['summary']['body_yaw_delta_deg'],
            'cct_shift_m': data['summary']['cct_shift'], 'endpoints': endpoints,
        }
    after_change = report['after']['180']['body_relative_change_m']
    report['measured_improvement_ratio_180'] = before_change / after_change
    report['validated_snapshots'] = total_frames
    args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    print(f'PASS HMD frame fix:20deg={report["after"]["20"]["body_relative_change_m"]*1000:.3f}mm; '
          f'180deg={after_change*1000:.3f}mm, previously{before_change*1000:.3f}mm; CCT unchanged')
    print(f'Validated {total_frames} diagnostic snapshots and6 settled endpoints. User visually confirmed.')
    print(args.out)


if __name__ == '__main__':
    main()
