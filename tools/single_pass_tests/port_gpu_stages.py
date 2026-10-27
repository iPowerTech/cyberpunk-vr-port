"""GPU time of the port's own passes in a live owned launch (geometry_cost_live.py launch).

Requests `CyberpunkVR_GpuStageRequest` frames of the plugin's GPU stage profiler
(src/Render/GpuStageProfile.cpp: eye captures, depth capture, XR color/depth submission, HUD) and
prints mean/max microseconds per stage plus the per-frame sum. Engine passes are covered by the
scene GPU profile instead (geometry_cost_report.LiveProbe.profile); together they account for the
GPU frame.
"""
import argparse
import json
from pathlib import Path
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path[:0] = [str(ROOT / 'tools/quickboot_tests'), str(ROOT / 'tools/roomscale_tests'),
                str(ROOT / 'tools/swimming_tests'), str(HERE)]

NAMES = ('CyberpunkVR_GpuStageRequest', 'CyberpunkVR_GpuStageReport', 'CyberpunkVR_GpuStageReportSeq')


def summarize(report):
    """Per-stage mean/max microseconds and the per-frame sum. A stage recorded once per frame
    contributes its mean; `count` is how many frames it was seen in."""
    stages = [dict(name=s['name'], count=s['count'], meanUs=s['mean_us'], maxUs=s['max_us'])
              for s in report.get('stages', []) if s.get('count')]
    frames = max((s['count'] for s in stages), default=0)
    per_frame = sum(s['meanUs'] * s['count'] for s in stages) / frames if frames else 0.0
    return dict(frames=frames, invalid=report.get('invalid', 0), perFrameUs=per_frame,
                stages=sorted(stages, key=lambda s: -s['meanUs']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=32)
    parser.add_argument('--timeout', type=float, default=20)
    args = parser.parse_args()
    import stage_replay_live as srl
    srl.NAMES = tuple(srl.NAMES) + NAMES
    session = srl.Session(args.state)
    p = session.probe
    try:
        p.write32('CyberpunkVR_GpuStageRequest', min(args.frames, 32))
        end = time.monotonic() + args.timeout
        report = {}
        while time.monotonic() < end:
            time.sleep(0.5)
            session.check()
            report = p.seqlock('CyberpunkVR_GpuStageReport', 'CyberpunkVR_GpuStageReportSeq', 2048)
            if not report.get('pending') and report.get('batches'):
                break
    finally:
        session.reader.close()
    value = dict(report=report, summary=summarize(report))
    args.out.write_text(json.dumps(value, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(value['summary'], indent=1))


if __name__ == '__main__':
    main()
