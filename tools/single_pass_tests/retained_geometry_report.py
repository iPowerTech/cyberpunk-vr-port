"""Validate bounded retained native geometry routing and its same-launch reference."""

MODES={'reference':1,'replace':2}
FIELDS=('mainDraws','peerDraws','mainScopes','peerScopes','mainSealed','peerSealed',
        'mainSubmitted','peerSubmitted','serializedBatches','serializedLists',
        'mainImported','peerImported','mainExported','peerExported','reason','mode')


def launch_modes(mode):
    if mode not in MODES:raise ValueError('Invalid retained geometry mode')
    return ('reference','replace') if mode=='replace' else ('reference',)


def validate_options(mode,stage,*,pass_targets,paired_apply,conflicts=()):
    if mode is None:
        if stage!=7:raise ValueError('--retained-stage requires --retained-geometry')
        return
    if mode not in MODES or stage not in (6,7):raise ValueError('Invalid retained geometry mode/stage')
    if not pass_targets or not paired_apply:
        raise ValueError('Retained geometry requires --pass-targets and paired preparation apply')
    if any(conflicts):raise ValueError('Retained geometry cannot share depth, alpha, skinning, continuous or other GPU probes')


def decode(mode,stage,state,verified,stats,different,compared_bytes,boundaries=None,indirect=None,indirect_rejected=None):
    if len(stats)!=len(FIELDS):raise ValueError('Invalid retained geometry stats ABI')
    report=dict(mode=mode,stage=stage,state=state,verified=verified,stats=dict(zip(FIELDS,stats)),
                different=list(different),bytes=list(compared_bytes))
    if boundaries is not None:report['boundaries']=list(boundaries)
    if indirect is not None:report['indirect']=list(indirect)
    if indirect_rejected is not None:report['indirectRejected']=list(indirect_rejected)
    return report


def validate(report,expected_mode=None,expected_stage=None):
    if not isinstance(report,dict):raise RuntimeError('Invalid retained geometry report')
    mode=report.get('mode');stage=report.get('stage');stats=report.get('stats')
    if mode not in MODES or type(stage) is not int or stage not in (6,7):
        raise RuntimeError('Invalid retained geometry mode/stage')
    if expected_mode is not None and mode!=expected_mode or expected_stage is not None and stage!=expected_stage:
        raise RuntimeError('Retained geometry request differs from its report')
    if not isinstance(stats,dict) or set(stats)!=set(FIELDS):raise RuntimeError('Incomplete retained geometry counters')
    arrays=(report.get('different'),report.get('bytes'))
    if any(not isinstance(values,list) or len(values)!=size for values,size in zip(arrays,(12,2))):
        raise RuntimeError('Invalid retained geometry comparison ABI')
    numbers=[report.get('state'),report.get('verified'),*stats.values(),*arrays[0],*arrays[1]]
    if any(type(value) is not int or not 0<=value<2**64 for value in numbers):
        raise RuntimeError('Invalid retained geometry counter value')
    if 'indirect' in report:
        indirect=report['indirect']
        if not isinstance(indirect,list) or len(indirect)!=2 or any(type(value) is not int or not 0<=value<2**64 for value in indirect):
            raise RuntimeError('Invalid retained geometry indirect command counters')
        if any(value>stats[eye+'Draws'] for value,eye in zip(indirect,('main','peer'))):
            raise RuntimeError('Retained indirect calls exceed the recorded command count')
    if 'indirectRejected' in report:
        rejected=report['indirectRejected']
        if not isinstance(rejected,list) or len(rejected)!=8 or any(type(value) is not int or not 0<=value<2**64 for value in rejected):
            raise RuntimeError('Invalid retained geometry indirect rejection diagnostics')
    if 'boundaries' in report:
        boundaries=report['boundaries']
        if not isinstance(boundaries,list) or len(boundaries)!=4 or any(type(value) is not int or not 0<=value<2**64 for value in boundaries):
            raise RuntimeError('Invalid retained geometry boundary counters')
        if any(not mask&(1<<stage) for mask in boundaries[2:]):
            raise RuntimeError('Retained geometry boundary masks lack the requested stage')
    if report['state']!=3 or report['verified']!=1 or stats['reason'] or stats['mode']!=MODES[mode]:
        raise RuntimeError(f'Retained geometry did not complete: {report}')
    for eye in ('main','peer'):
        if not 0<stats[eye+'Scopes']<=stats[eye+'Draws'] or stats[eye+'Sealed']!=1 or stats[eye+'Submitted']!=1:
            raise RuntimeError('Retained geometry lacks complete draws, scopes, sealing or submission for both eyes')
        if stats[eye+'Imported']<4 or stats[eye+'Exported']!=(stats[eye+'Imported'] if mode=='replace' else 0):
            raise RuntimeError('Retained geometry attachment import/export coverage differs')
    if not 1<=stats['serializedBatches']<=stats['serializedLists']:
        raise RuntimeError('Retained geometry did not serialize its submitted lists')
    if any(report['different']):raise RuntimeError('Retained geometry changed native color, depth or stencil bytes')
    if (mode=='reference' and not all(report['bytes'])) or (mode=='replace' and any(report['bytes'])):
        raise RuntimeError('Retained geometry comparison bytes differ from the requested mode')
