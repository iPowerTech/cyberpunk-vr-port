"""Alternate native and optimized depth groups in one owned, world-ready launch."""
from contextlib import redirect_stdout
import json
from pathlib import Path
from statistics import median

from early_depth_report import ALL_DRAWS,decode_gpu_times,validate,validate_native_timing


def summarize(samples):
    if not samples or len(samples)%2:raise ValueError('Depth benchmark needs complete A/B pairs')
    identity=None;pairs=[]
    for index in range(0,len(samples),2):
        pair=samples[index:index+2]
        if {s['mode'] for s in pair}!={'native','optimized'}:raise ValueError('Missing benchmark mode')
        values={}
        for sample in pair:
            r=sample['capture'];d=r['earlyDepthGpu'];key=(r['pid'],r['dll'],tuple(d['targetSize']))
            if not r['passed'] or r.get('cleanup')!='restored':raise ValueError('Capture or cleanup failed')
            if identity is not None and identity!=key:raise ValueError('Benchmark process, build, or resolution changed')
            identity=key
            if sample['mode']=='native':validate_native_timing(d)
            else:validate(d,ALL_DRAWS,True,True)
            if d['gpuTimes']!=decode_gpu_times(d['gpuTimes']['raw']):raise ValueError('Inconsistent GPU duration')
            values[sample['mode']]=d
        a,b=values['native'],values['optimized']
        pairs.append(dict(order=[s['mode'] for s in pair],nativeMs=a['gpuTimes']['sumMs'],optimizedMs=b['gpuTimes']['sumMs'],
                          savedMs=a['gpuTimes']['sumMs']-b['gpuTimes']['sumMs'],
                          nativeSourceDraws=[a['nativeTargets'][n] for n in ('mainDraws','peerDraws')],
                          optimizedSourceDraws=[b['nativeTargets'][n] for n in ('mainDraws','peerDraws')],
                          drawCommandsRemoved=b['netDrawCommandReduction']))
    durations={mode:[s['capture']['earlyDepthGpu']['gpuTimes']['sumMs'] for s in samples if s['mode']==mode]
               for mode in ('native','optimized')}
    return dict(pairs=pairs,distributions={mode:dict(minMs=min(v),medianMs=median(v),maxMs=max(v)) for mode,v in durations.items()},
                medianPairedSavingMs=median(p['savedMs'] for p in pairs),
                limitation='Adjacent frames, bounded cold targets and diagnostic observers; sum of per-eye depth GPU ranges, not frame critical path or FPS.')


def run(pid,out,pair_count,check,ready):
    from read_preparation import main as capture
    out=Path(out);out.mkdir(parents=True,exist_ok=True)
    report=dict(passed=False,warmup=[],samples=[])
    try:
        for index in range(pair_count+1):
            modes=('native','optimized') if index%2==0 else ('optimized','native')
            for mode in modes:
                check();hud=ready();stem=f'{index:02d}-{mode}'
                path=out/(stem+'.json')
                args=['--pid',str(pid),'--out',str(path),'--frames','2','--merge-root',
                      '--schedule-preparation','apply','--early-depth-gpu','all',
                      '--native-depth-timing' if mode=='native' else '--optimized-depth']
                with (out/(stem+'.log')).open('w',encoding='utf-8') as log,redirect_stdout(log):
                    result=capture(args)
                entry=dict(mode=mode,file=path.name,hud=hud,capture=json.loads(path.read_text()))
                report['warmup' if not index else 'samples'].append(entry)
                check()
                if result:raise RuntimeError(f'Depth benchmark capture failed: {path}')
                timing=entry['capture']['earlyDepthGpu']['gpuTimes']
                print(f'Depth {stem}: MAIN {timing["mainMs"]:.4f} ms, VRCAM {timing["peerMs"]:.4f} ms, sum {timing["sumMs"]:.4f} ms',flush=True)
        report['summary']=summarize(report['samples']);report['passed']=True
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        (out/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report['summary']
