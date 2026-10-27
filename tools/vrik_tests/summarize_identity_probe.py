"""Summarize separately sampled pose-ID counters without equating their snapshots."""
import argparse,json,math
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('recording',type=Path)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
data=json.loads(args.recording.read_text(encoding='utf-8'))
rows=data['samples']
if not rows:raise RuntimeError('No observations')

def difference(a,b):
    if isinstance(a,dict):return {k:difference(a[k],b[k]) for k in a}
    if isinstance(a,list):return [y-x for x,y in zip(a,b)]
    return b-a

def summarize(samples,settled=False):
    first,last=samples[0],samples[-1]
    report={'samples':len(samples),'stable_cache':sum(r['cache_stable'] for r in samples),
        'duration_s':(last['perf_ns']-first['perf_ns'])/1e9,
        'identity':difference(first['identity_counters'],last['identity_counters']),
        'source':difference(first['source_counters'],last['source_counters']),
        'native_publication':difference(first['native_publication_counters'],last['native_publication_counters']),
        'pose_lookup':difference(first['pose_counters'],last['pose_counters']),
        'submit':difference(first['submit_counters'],last['submit_counters']),
        'last_pose_ids':last['pose_ids']}
    stable=[r for r in samples if r['cache_stable']]
    report['shadow_max_mm']=max((r['shadow_max_mm'] for r in stable),default=None)
    ages=[r['native_pair']['age_ms'] for r in samples if 'age_ms' in r['native_pair']]
    report['native_age_max_ms']=max(ages) if ages else None
    report['sampled_zero_capture_ids']=[sum(int(r['pose_ids']['identity_capture_ids'][eye],16)==0 for r in samples) for eye in (0,1)]
    if settled:
        poses=[]
        for r in stable:
            if r['perf_ns']-first['perf_ns']<300_000_000:continue
            if not poses or poses[-1]['cache_capture_ms']!=r['cache_capture_ms']:poses.append(r)
        report['settled_step_mm']={hand:max((math.dist(a['cache'][hand][:3],b['cache'][hand][:3])*1000
            for a,b in zip(poses,poses[1:])),default=0) for hand in ('left_hand','right_hand')}
    return report

report=summarize(rows)
report['phases']={phase:summarize([r for r in rows if r['phase']==phase],
    phase.endswith(('hold','rest')) or phase in ('baseline','after')) for phase in dict.fromkeys(r['phase'] for r in rows)}
report['pid']=data['pid']
report['read_failures']=len(data['read_failures'])
report['error']=data.get('error');report['cleanup_error']=data.get('cleanup_error')
report['inputs_restored']=data.get('final_status',{}).get('head_tracking')==data['initial_status']['head_tracking']
report['boundary']='Counters and pose snapshots come from separate publications. Equal or different sampled IDs alone do not prove one rendering invocation.'
args.out.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='phases'},indent=2))
