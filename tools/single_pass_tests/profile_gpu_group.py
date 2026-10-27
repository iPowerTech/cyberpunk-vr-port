"""A bounded sample of per-group GPU costs; this is not a whole-frame FPS test."""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--runs', type=int, default=4, choices=range(2, 6))
p.add_argument('--direct-resolve', action='store_true')
p.add_argument('--mixed-materials', action='store_true')
p.add_argument('--depth-prepass', action='store_true')
p.add_argument('--start-indices', type=int, default=0)
p.add_argument('--depth-run', type=int, default=0)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
a.out.mkdir(parents=True, exist_ok=True)
rows = []
for n in range(a.runs):
    path = a.out / f'run-{n}.json'
    result = subprocess.run([sys.executable, '-B', str(root / 'tools/single_pass_tests/read_gpu_probe.py'),
        '--pid', str(a.pid), '--out', str(path), '--native-reference', '--group', '--late-visibility',
        '--prepare-gate-ms', '50', '--scene-depth', '--scene-route', '--gpu-times', '--rearm-completed',
        *(['--direct-resolve'] if a.direct_resolve else []),*(['--mixed-materials'] if a.mixed_materials else []),
        *(['--depth-prepass'] if a.depth_prepass else []), '--start-indices',str(a.start_indices),'--depth-run',str(a.depth_run)],
        text=True, capture_output=True)
    if result.returncode:
        print(result.stdout, end='')
        print(result.stderr, end='', file=sys.stderr)
        raise SystemExit(result.returncode)
    row = json.loads(path.read_text(encoding='utf-8'))
    rows.append(row)
    print(json.dumps({'run': n, 'draws': [row['sourceDraws'], row['mainDraws']],
        'differentBytes': row['differentBytes'], 'gpuTiming': row['gpuTiming']}), flush=True)

stages = rows[0]['gpuTiming']['stages']
# First run is reported but excluded from the summary as the cold sample.
summary = {name: {'medianMs': statistics.median(sum(r['gpuTiming']['stages'][name]['ms']) for r in rows[1:]),
                 'sampleMs': [sum(r['gpuTiming']['stages'][name]['ms']) for r in rows]}
           for name in stages}
report = {'pid': a.pid, 'runs': a.runs, 'discardedWarmup': 1,
          'directResolve': a.direct_resolve,
          'depthPrepass': a.depth_prepass,
          'startIndices': a.start_indices,
          'depthRun': a.depth_run,
          'scope': 'Instrumented material group. Reference/import/readback remain diagnostic costs; direct resolve has no output copy-back.',
          'stages': summary}
(a.out / 'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
