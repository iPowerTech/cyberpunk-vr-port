"""Exercise native cache eviction using acknowledged named Boolean setters."""
import json
from pathlib import Path
import time

from retirement_settings import combinations


def run(probe,telemetry,capture,out,check=lambda:None,*,timeout=60):
    from retirement_report import decode,merge_event_history,validate_retirement
    expected=capture['ownedExecution']['stats']
    admission=capture['ownedExecution']['admission']
    if len(admission)!=6 or admission[:4]!=[expected[1],expected[2],expected[3],expected[4]]:
        raise ValueError('Retirement test has no matching native source-cache admission')
    if (capture['pid'],capture['dll'])!=(telemetry.pid,telemetry.symbols['sha256']):
        raise ValueError('Native retirement test changed process/build')
    result=dict(pid=telemetry.pid,dll=capture['dll'],consumerId=expected[1],epoch=expected[2],pair=expected[3],
                passed=False,steps=[],samples=[],history=[])
    deadline=time.monotonic()+timeout
    last_error=None
    def collect():
        check();raw=telemetry.read()
        if (raw['pid'],raw['dll'])!=(capture['pid'],capture['dll']):raise ValueError('Retirement telemetry changed process/build')
        report=decode(raw)
        report['admission']=admission
        result['history']=merge_event_history(result['history'],report['events'])
        report['history']=result['history']
        result['last']=report
        result['state']=raw['Retirement']
        result['samples'].append(dict(sequence=raw['sequence'],eventSequence=raw['events']['sequence'],
            cacheCalls=raw['Cache'][0],distinctCacheTuples=raw['Cache'][3],totals=raw['RetirementTotals']))
        if raw['Allocator'][0] in (2,3):raise RuntimeError('Native allocator closed or rejected its retirement binding')
        return raw,report
    try:
        result['baseline']=probe.begin()
        raw,report=collect()
        if raw['Allocator'][5]!=1 or raw['Allocator'][18] or raw['Allocator'][21]:
            raise RuntimeError('Native retirement lacks complete allocator guards/classification')
        for values in combinations(probe.baseline)*2:
            if time.monotonic()>=deadline:break
            before=raw['Cache'][0]
            acknowledged=probe.apply(values)
            step=dict(values=values,acknowledged=acknowledged,cacheObserved=False)
            result['steps'].append(step)
            step_deadline=min(deadline,time.monotonic()+3)
            while time.monotonic()<step_deadline:
                raw,report=collect()
                if raw['Cache'][0]>before:
                    step.update(cacheObserved=True,cacheKey=raw['Cache'][7],viewCount=raw['Cache'][8],
                                distinctCacheTuples=raw['Cache'][3])
                    break
                time.sleep(.05)
            if not step['cacheObserved']:raise TimeoutError('Native target cache did not render after a CVar update')
            try:
                result['proof']=validate_retirement(report,expected[1],expected[2],expected[3],require_recycled=True)
                if not result['proof'].get('cacheOwnershipCorrelationProved'):raise ValueError('Retirement lacks sealed native cache correlation')
                result['passed']=True;break
            except ValueError as error:last_error=str(error)
            time.sleep(.1)
        while not result['passed'] and time.monotonic()<deadline:
            raw,report=collect()
            try:
                result['proof']=validate_retirement(report,expected[1],expected[2],expected[3],require_recycled=True)
                if not result['proof'].get('cacheOwnershipCorrelationProved'):raise ValueError('Retirement lacks sealed native cache correlation')
                result['passed']=True;break
            except ValueError as error:last_error=str(error)
            time.sleep(.05)
        if not result['passed']:raise TimeoutError('Native retirement was not exercised: '+str(last_error))
    except Exception as error:
        result['error']=str(error)
    finally:
        try:result['restoration']=probe.restore_live()
        except Exception as error:result['restoreError']=str(error);result['passed']=False
        Path(out).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    if not result['passed']:raise RuntimeError(result.get('restoreError') or result.get('error','Native retirement test failed'))
    return result


def validate_cycles(cycles,expected_count):
    if type(expected_count) is not int or not 1<=expected_count<=8 or len(cycles)!=expected_count:
        raise ValueError('Native retirement cycle count is incomplete')
    identities=set();last_consumer=last_return=last_recycle=0;last_totals=None;process=None;cache=None
    for cycle in cycles:
        if cycle.get('passed') is not True or not cycle.get('restoration') or cycle['restoration'].get('dirty') is not False:
            raise ValueError('Retirement cycle failed or its CVars were not restored')
        proof=cycle['proof'];state=cycle['state'];totals=cycle['last']['totals']
        if any(proof.get(name) is not True for name in ('nativeDestroyReturned','metadataRecycled','cacheOwnershipCorrelationProved')):
            raise ValueError('Retirement cycle has no complete native/cache proof')
        current=(cycle['pid'],cycle['dll']);current_cache=(proof['cacheBase'],proof['renderer'])
        if process is not None and (current!=process or current_cache!=cache):
            raise ValueError('Retirement cycles changed process/build/sealed cache')
        process,cache=current,current_cache
        identity=(proof['header'],proof['generation'],proof['born'])
        if identity in identities or proof['consumerId']<=last_consumer:
            raise ValueError('Retirement cycle replayed an old consumer/owner generation')
        identities.add(identity);last_consumer=proof['consumerId']
        sequence=proof['eventSequences']
        if sequence['destroyReturn']<=last_return or sequence['recycle']<=last_recycle:
            raise ValueError('Retirement cycle replayed old native destruction/recycling')
        last_return,last_recycle=sequence['destroyReturn'],sequence['recycle']
        if (len(state)!=8 or any(type(value) is not int or not 0<=value<1<<64 for value in state) or
                state[1]>1 or state[2]>128 or state[3] or state[4] or state[5] or state[6] or state[7]):
            raise ValueError('Retirement cycle left pending/detached/consumed native storage')
        if last_totals is not None and any(totals[name]<=last_totals[name] for name in
                ('savedDestroyBegun','savedDestroyReturned','metadataRecycled')):
            raise ValueError('Retirement cycles did not advance actual destruction totals')
        last_totals=totals
    return dict(cycles=expected_count,uniqueRetiredGenerations=len(identities),pid=process[0],dll=process[1],
                nativeDestroyReturned=last_totals['savedDestroyReturned'],metadataRecycled=last_totals['metadataRecycled'],
                boundedOutstandingReservations=True,performanceGainProved=False,wholeSceneSinglePassProved=False)
