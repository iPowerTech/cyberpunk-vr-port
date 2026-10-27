"""Per-stage replay check in one owned launch (geometry_cost_live.py launch).

For each RenderElements stage: replay VRCAM's capture into MAIN with MAIN's camera (StageReplay
mode 4), take a screenshot, return to native recording, take another, and compare each eye with
the native shot. VRCAM (left half) is never changed by the replay, so its difference is the
scene's own noise (crowd, traffic, particles) over the same few seconds. A stage passes when MAIN's
difference stays within that noise, it actually replayed, and almost nothing fell back.

Finally the union of passing stages is checked the same way, so its mask can go straight into
control_ab.py (CyberpunkVR_StageReplayMode 0 vs 4).
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

STAGES = (3, 4, 5, 6, 7, 9, 10, 20, 21, 22, 23, 24, 30)
COUNTERS = ('replayed', 'skipped', 'fallbacks', 'captures')


def eye_difference(a, b):
    """Mean absolute luma difference and share of pixels differing by more than 24, per eye."""
    from PIL import ImageChops
    width, height = a.size
    out = {}
    for name, box in (('vrcam', (0, 0, width // 2, height)), ('main', (width // 2, 0, width, height))):
        hist = ImageChops.difference(a.crop(box).convert('L'), b.crop(box).convert('L')).histogram()
        total = sum(hist)
        out[name] = dict(meanAbs=sum(i * n for i, n in enumerate(hist)) / total, over24=sum(hist[25:]) / total)
    return out


def verdict(diff, delta, tolerance=1.5, floor=0.5):
    main, vrcam = diff['main'], diff['vrcam']
    image = (main['meanAbs'] <= max(vrcam['meanAbs'] * tolerance, vrcam['meanAbs'] + floor) and
             main['over24'] <= max(vrcam['over24'] * tolerance, vrcam['over24'] + 0.002))
    attempts = delta['replayed'] + delta['fallbacks']
    share = delta['fallbacks'] / attempts if attempts else 1.0
    return dict(image=image, replayed=delta['replayed'] > 0, fallbackShare=share,
                ok=image and delta['replayed'] > 0 and share < 0.05)


def mask(stages):
    value = 0
    for stage in stages:
        value |= 1 << stage
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--state', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stages', default=','.join(map(str, STAGES)))
    parser.add_argument('--partitions', type=int, default=1)
    parser.add_argument('--settle', type=float, default=2.5)
    parser.add_argument('--window', type=float, default=2.0)
    parser.add_argument('--combined-only', action='store_true', help='check only the union of --stages')
    args = parser.parse_args()
    import stage_replay_live as srl
    from PIL import Image
    args.out.mkdir(parents=True, exist_ok=True)
    session = srl.Session(args.state)
    p = session.probe
    original = dict(mode=p.read32('CyberpunkVR_StageReplayMode'), partitions=p.read32('CyberpunkVR_VrcamGbufferPartitions'),
                    barriers=p.read32('CyberpunkVR_StageReplayAllowBarriers'))
    results = []

    def run(label, stages):
        session.write64('CyberpunkVR_StageReplayStages', mask(stages))
        p.write32('CyberpunkVR_StageReplayMode', 4)
        time.sleep(args.settle)
        before = srl.stats(session)
        time.sleep(args.window)
        after = srl.stats(session)
        replay_shot = args.out / f'{label}-replay.bmp'
        session.live.screenshot(replay_shot, session.check)
        p.write32('CyberpunkVR_StageReplayMode', 0)
        time.sleep(args.settle)
        native_shot = args.out / f'{label}-native.bmp'
        session.live.screenshot(native_shot, session.check)
        delta = {key: after[key] - before[key] for key in COUNTERS}
        reasons = {name: after['fallbackReasons'].get(name, 0) - before['fallbackReasons'].get(name, 0)
                   for name in after['fallbackReasons']}
        diff = eye_difference(Image.open(replay_shot).convert('RGB'), Image.open(native_shot).convert('RGB'))
        row = dict(label=label, stages=list(stages), delta=delta, fallbackReasons={k: v for k, v in reasons.items() if v},
                   camera=after.get('camera'), diff=diff, verdict=verdict(diff, delta))
        results.append(row)
        print(json.dumps(dict(label=label, ok=row['verdict']['ok'], image=row['verdict']['image'],
                              main=round(diff['main']['meanAbs'], 3), vrcam=round(diff['vrcam']['meanAbs'], 3),
                              replayed=delta['replayed'], fallbacks=delta['fallbacks'], reasons=row['fallbackReasons'])), flush=True)
        return row

    try:
        p.write32('CyberpunkVR_StageReplaySource', 1)
        p.write32('CyberpunkVR_VrcamGbufferPartitions', args.partitions)
        p.write32('CyberpunkVR_StageReplayAllowBarriers', 1)
        stages = [int(s) for s in args.stages.split(',') if s]
        passing = stages if args.combined_only else [stage for stage in stages if run(f'stage-{stage}', [stage])['verdict']['ok']]
        if len(passing) > 1:
            combined = run('combined', passing)
            value = dict(passing=passing, combinedOk=combined['verdict']['ok'], combinedMask=hex(mask(passing)), rows=results)
        else:
            value = dict(passing=passing, rows=results)
    finally:
        p.write32('CyberpunkVR_StageReplayMode', 0)
        p.write32('CyberpunkVR_VrcamGbufferPartitions', original['partitions'])
        p.write32('CyberpunkVR_StageReplayAllowBarriers', original['barriers'])
        p.write32('CyberpunkVR_StageReplayMode', original['mode'])
        session.reader.close()
    (args.out / 'sweep.json').write_text(json.dumps(value, indent=1) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in value.items() if k != 'rows'}), flush=True)


if __name__ == '__main__':
    main()
