"""Validate one delayed paired G-buffer reference, never production admission."""

FIELDS=('mainSeen','peerSeen','mainCaptured','peerCaptured','pairedDraws',
        'mainSubmitted','peerSubmitted','queueProof','width','height',
        'mainSequence','peerSequence','familyId','serializedBatches','reason','mode')


def validate_options(enabled,*,pass_targets,paired_apply,conflicts=()):
    if not enabled:return
    if not pass_targets or not paired_apply:
        raise ValueError('Paired G-buffer requires pass targets and paired preparation apply')
    if any(conflicts):raise ValueError('Paired G-buffer cannot share other geometry or GPU probes')


def decode(state,stats,different,compared_bytes,camera,changed):
    if tuple(map(len,(stats,different,compared_bytes,camera,changed)))!=(16,12,2,16,12):
        raise ValueError('Invalid paired geometry publication ABI')
    return dict(state=state,stats=dict(zip(FIELDS,stats)),different=list(different),
                bytes=list(compared_bytes),cameraDiagnostics=list(camera),changed=list(changed),
                family=40,purpose='matched-draw-reference-only')


def validate(report,*,require_contribution=True):
    if not isinstance(report,dict) or report.get('purpose')!='matched-draw-reference-only':
        raise RuntimeError('Invalid paired geometry report purpose')
    stats=report.get('stats')
    family=report.get('family')
    if type(family) is not int or family not in (40,70):raise RuntimeError('Unknown audited paired shader family')
    if not isinstance(stats,dict) or set(stats)!=set(FIELDS):
        raise RuntimeError('Incomplete paired geometry stats')
    arrays=[report.get('different'),report.get('bytes'),report.get('cameraDiagnostics'),report.get('changed')]
    if any(not isinstance(v,list) or len(v)!=n for v,n in zip(arrays,(12,2,16,12))):
        raise RuntimeError('Invalid paired geometry comparison arrays')
    values=[report.get('state'),*stats.values(),*(x for row in arrays for x in row)]
    if any(type(v) is not int or not 0<=v<2**64 for v in values):
        raise RuntimeError('Invalid paired geometry numeric value')
    if report['state']!=3 or stats['reason'] or stats['mode']!=1:
        raise RuntimeError(f'Paired geometry reference failed: {report}')
    for name in ('mainCaptured','peerCaptured','pairedDraws','mainSubmitted','peerSubmitted'):
        if stats[name]!=1:raise RuntimeError('Missing or repeated paired geometry operation: '+name)
    if stats['queueProof']!=3:raise RuntimeError('Both native queue/recording proofs are required')
    camera=report['cameraDiagnostics']
    if camera[0]!=(3 if family==70 else 1) or not camera[8] or camera[9]!=2048 or camera[15]!=1:
        raise RuntimeError('Actual GPU camera table proof is missing')
    if min(stats['mainSeen'],stats['peerSeen'])<1 or not stats['familyId'] or not stats['serializedBatches']:
        raise RuntimeError('Missing native draw or ordered submission evidence')
    if not 0<stats['mainSequence']<stats['peerSequence']:
        raise RuntimeError('Paired draw source sequence is not MAIN then PEER')
    width,height=stats['width'],stats['height']
    if not 0<width<=4096 or not 0<height<=4096:
        raise RuntimeError('Invalid paired geometry image extent')
    if report['bytes']!=[width*height*17]*2 or any(report['different']):
        raise RuntimeError('Paired three-MRT/depth/stencil output is not fully byte-exact')
    if any(v>width*height for v in report['changed']):
        raise RuntimeError('Invalid native changed-pixel count')
    if require_contribution and any(not sum(report['changed'][eye*6:eye*6+3]) for eye in range(2)):
        raise RuntimeError('Paired reference must visibly contribute native G-buffer pixels in both eyes')

