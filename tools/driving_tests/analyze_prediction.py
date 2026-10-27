"""Summarize actual native-wheel phase and bounded steering lead from live A/B traces."""
import argparse,json,math
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('directory',type=Path);a=p.parse_args()
results=[]
for path in sorted(a.directory.glob('*.json')):
    d=json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(d,dict) or 'profile_begin' not in d.get('summary',{}):continue
    assert d['summary']['error'] is None,(path,d['summary']['error'])
    settings=d['settings'];limit,dead=settings['limit'],settings['dead']
    rows=[r for r in d['rows'] if r['phase']=='profile']
    t=np.array([r['t'] for r in rows]);angle=np.array([r['angle'] for r in rows]);out=np.array([r['steer'] for r in rows])
    rim=[]
    for r in rows:
        right,left=[h['animated'] for h in r['hands']]
        rim.append(-math.degrees(math.atan2(right[2]-left[2],right[0]-left[0])))
    rim=np.degrees(np.unwrap(np.radians(rim)))
    predicted=np.sign(out)*(dead+np.abs(out)*(limit-dead));predicted[out==0]=angle[out==0]
    good=(np.abs(angle)>dead+.02)&(np.abs(angle)<limit-.1)
    # A read-only sample can straddle separately published angle/output atomics.
    # Report observed extrema; do not interpret an isolated torn sample as input.
    lead=predicted[good]-angle[good]
    item={'file':path.name,'prediction':settings['prediction'],'profile':d['profile'],
          'frequency':d['frequency'],'samples':len(rows),'lead_p99_deg':float(np.quantile(np.abs(lead),.99)) if len(lead) else 0,
          'lead_max_observed_deg':float(np.max(np.abs(lead))) if len(lead) else 0,
          'lead_active_fraction':float(np.mean(np.abs(lead)>.01)) if len(lead) else 0,
          'final_steer':d['summary']['final_steer']}
    if d['profile']==1:
        omega=2*np.pi*d['frequency'];start=d['summary']['profile_begin']+.6
        select=(t>start+1/d['frequency'])&(t<start+4/d['frequency']-.15)
        x=np.column_stack((np.sin(omega*t[select]),np.cos(omega*t[select]),np.ones(np.count_nonzero(select))))
        def fit(signal):
            c=np.linalg.lstsq(x,signal[select],rcond=None)[0]
            return np.arctan2(c[1],c[0]),np.hypot(c[0],c[1]),float(np.sqrt(np.mean((signal[select]-x@c)**2)))
        pa,aa,ea=fit(angle);pn,an,en=fit(rim);pp,ap,ep=fit(predicted)
        item.update(native_lag_ms=float(math.remainder(pa-pn,2*np.pi)/omega*1000),
                    input_lead_ms=float(math.remainder(pp-pa,2*np.pi)/omega*1000),
                    physical_amplitude=float(aa),native_amplitude=float(an),native_fit_rms_deg=en)
    results.append(item)
print(json.dumps(results,indent=2))
(a.directory/'comparison.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
