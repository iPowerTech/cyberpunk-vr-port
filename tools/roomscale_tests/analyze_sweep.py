"""Summarize saved trajectories without changing the running application."""
import argparse
import json
import math
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('report', type=Path)
args = parser.parse_args()
report = json.loads(args.report.read_text(encoding='utf-8'))
rows = report['samples']
groups = {}
for row in rows:
    groups.setdefault(row['label'], []).append(row)
out = {}
for label, group in groups.items():
    out[label] = {
        'samples': len(group), 'first': group[0]['after'], 'last': group[-1]['after'],
        'injected_range': [group[0]['injected'], group[-1]['injected']],
        'cct_without_filtered_velocity_count': [group[0]['calls']-group[0]['feedback_reads'],
                                               group[-1]['calls']-group[-1]['feedback_reads']],
        'yaw_range_deg': [math.degrees(min(r['yaw'] for r in group)), math.degrees(max(r['yaw'] for r in group))],
        'max_native_xy': max(math.hypot(*r['native'][:2]) for r in group),
        'max_requested_xy': max(math.hypot(*r['requested'][:2]) for r in group),
        'max_observed_contact_residual': max(math.hypot(*(r['after'][i]-r['before'][i]-r['native'][i]-r['requested'][i] for i in range(2))) for r in group),
        'z_range': [min(r['after'][2] for r in group), max(r['after'][2] for r in group)],
        'last_consumed': group[-1]['consumed'],
    }
print(json.dumps(out, indent=2))
largest = sorted(rows, key=lambda r: math.hypot(*(r['after'][i]-r['before'][i]-r['native'][i]-r['requested'][i] for i in range(2))), reverse=True)[:8]
print('Largest observed CCT request/response differences (sampled, not the complete call stream):')
for r in largest:
    print(json.dumps({k:r[k] for k in ('label', 'calls', 'dt', 'native', 'requested', 'before', 'after', 'velocity', 'feedback')}))
first_native = next((i for i,r in enumerate(rows) if math.hypot(*r['native'][:2]) > .00001), None)
if first_native is not None:
    print('First nonzero native XY, including preceding observations:')
    for r in rows[max(0,first_native-5):first_native+5]:
        print(json.dumps({k:r[k] for k in ('label', 'calls', 'injected', 'feedback_reads', 'native', 'requested', 'before', 'after', 'velocity', 'feedback')}))
