"""Validate early, same-frame paired GPU output without claiming native replacement."""
import argparse

ALL_DRAWS=2**32-1
MAX_SOURCES=1024
MAX_INSTANCE_ROWS=MAX_SOURCES*4


def draw_limit(value):
    if value=='all':return ALL_DRAWS
    try:
        count=int(value)
        if 1<=count<=MAX_SOURCES or count==ALL_DRAWS:return count
    except (TypeError,ValueError):pass
    raise argparse.ArgumentTypeError(f'Expected all or a draw count in 1..{MAX_SOURCES}')


def validate_skinned_alpha_options(enabled,skinning,reference,optimized,*,continuous=False,native_timing=False):
    if enabled and (not skinning or reference==optimized or continuous or native_timing):
        raise ValueError('Skinned alpha requires --skinning-depth and one alpha depth mode without continuous or native timing work')


FIELDS=('mainSeen','peerSeen','recorded','matched','unsupported','missingCamera','missingMaterial',
        'bankFailure','unmatchedPeer','inputMismatch','referenceDraws','otherFrame','mainEnded','peerEnded','queueFailure','failure')
MASK_FIELDS=('mainPackets','peerPackets','mainInstances','peerInstances','both','mainOnly','pairedRuns','originFailure','unresolvedMain','unresolvedPeer',
             'peerOnly','peerOnlyRuns','knownPeerOnly','unrecordedPeerOnly')
NATIVE_FIELDS=('enabled','mainDraws','peerDraws','mainFallback','peerRuns','mainExported','peerExported','imports')
OPTIMIZED_FIELDS=('enabled','gpuDecision','mainCommands','peerUnconditional','peerOptimizedRuns','peerFallbackBatches','referenceDraws','cpuAdmitted')


def decode_gpu_times(values):
    if len(values)!=6 or any(not isinstance(v,int) or v<0 for v in values):
        raise ValueError('Invalid depth GPU timestamps')
    durations=[]
    for start,end,frequency in (values[:3],values[3:]):
        if end<=start or not frequency:raise ValueError('Incomplete depth GPU timestamps')
        durations.append((end-start)*1000.0/frequency)
    return dict(raw=list(values),mainMs=durations[0],peerMs=durations[1],sumMs=sum(durations),
                scope='depth group including private copies and gate, not total frame time')


def validate_tracking(report):
    stats=report.get('descriptorStoreStats',[])
    if report.get('descriptorFailure')!=0 or len(stats)!=6 or any(v<0 for v in stats) or stats[0]:
        raise RuntimeError('Descriptor metadata tracking failed or overflowed during the capture')


def validate_camera_stages(camera,frames):
    masks=camera.get('stageMasks',[0,0,0,0]);stages=camera.get('stages',1)
    if not camera['enabled']:
        if masks!=[0,0,0,0]:raise RuntimeError('Disabled native camera changed stage bindings')
        return
    if (stages not in (1,3) or len(masks)!=4 or any(v<0 for v in masks) or
            masks[:2]!=[frames*stages]*2 or masks[2]+masks[3]!=frames*stages):
        raise RuntimeError('Native camera stage binding/restoration coverage is incomplete')


def validate_skinning(skin,optimized,alpha_proofs=None):
    stats=skin.get('stats',[]);proofs=skin.get('proofs',[]);bindings=skin.get('bindingChecks',[0]*4)
    if alpha_proofs is not None:
        if len(alpha_proofs)!=6 or len(proofs)!=4:raise RuntimeError('Invalid combined shader proof counters')
        # The shared snapshot store publishes skinning plus alpha resources.
        proofs=[proofs[0]-alpha_proofs[0],proofs[1]-alpha_proofs[1]-alpha_proofs[2],
                proofs[2]-alpha_proofs[3],proofs[3]-alpha_proofs[4]]
    if len(stats)!=6 or len(proofs)!=4 or len(bindings)!=4 or any(not isinstance(v,int) or v<0 for v in stats+proofs+bindings):
        raise RuntimeError('Invalid skinning proof counters')
    if not skin['enabled']:
        if any(stats+proofs+bindings):raise RuntimeError('Disabled skinning path changed rendering')
        return
    if not stats[0] or not 0<stats[1]<=stats[0] or not stats[2] or any(stats[3:]):
        raise RuntimeError(f'Skinning input/copy contract was not covered: {skin}')
    if bindings!=[stats[1],stats[2],0,0]:
        raise RuntimeError(f'Skinning submission bindings/payloads were not validated: {skin}')
    if optimized:
        if proofs[0]!=stats[1] or proofs[1]<2*proofs[0] or not proofs[2] or not proofs[3]:
            raise RuntimeError(f'Skinning resources lack complete GPU snapshots: {skin}')
    elif any(proofs):raise RuntimeError('Reference mode unexpectedly used optimized resource snapshots')


def validate_native_timing(report):
    validate_tracking(report)
    validate_skinned_alpha(report)
    stats=report['stats'];native=report['nativeTargets'];o=report['optimized']
    if (report['state']!=3 or report['result'] or not report.get('nativeTiming') or report.get('comparison') is not None or
            report.get('nativeDifferences') is not None or report.get('nativeDrawsReplaced')!=0 or
            not stats['mainEnded'] or not stats['peerEnded'] or not stats['mainSeen'] or not stats['peerSeen'] or
            any(stats[n] for n in FIELDS if n not in ('mainSeen','peerSeen','mainEnded','peerEnded')) or
            native!=dict(zip(NATIVE_FIELDS,(3,stats['mainSeen'],stats['peerSeen'],0,0,0,0,0))) or
            o!=dict(zip(OPTIMIZED_FIELDS,(0,2**64-1,0,0,0,0,0,0))) or
            any(report.get(n) for n in ('coverage','masks','extraCoverage','sources'))):
        raise RuntimeError('Native timing changed rendering or did not cover the entire group')
    if report.get('gpuTimes')!=decode_gpu_times(report.get('gpuTimes',{}).get('raw',[])):
        raise RuntimeError('Native timing duration differs from the timestamp evidence')


def validate_alpha_reference(report,optimized):
    alpha=report.get('alphaReference',dict(enabled=False,stats=[0]*6));stats=alpha['stats']
    replacement=report.get('alphaOptimized',dict(enabled=False,proofs=[0]*6));proofs=replacement['proofs']
    recording=replacement.get('recording',None if replacement['enabled'] else [0]*6)
    if (not isinstance(recording,list) or len(recording)!=6 or
            any(type(value) is not int or not 0<=value<2**64 for value in recording)):
        raise RuntimeError('Invalid alpha recording receipt')
    expected_recording=[1,1,1,1,0,1] if replacement['enabled'] else [0]*6
    if recording!=expected_recording:
        raise RuntimeError(f'Alpha recording ownership coverage differs: {recording}')
    if len(stats)!=6 or any(not isinstance(v,int) or v<0 for v in stats):raise RuntimeError('Invalid alpha reference counters')
    if len(proofs)!=6 or any(not isinstance(v,int) or v<0 for v in proofs):raise RuntimeError('Invalid alpha snapshot counters')
    if not alpha['enabled'] and not replacement['enabled']:
        if any(stats+proofs):raise RuntimeError('Disabled alpha path recorded draws')
        return
    if (alpha['enabled'] and replacement['enabled']) or optimized!=replacement['enabled'] or not stats[0] or not 0<stats[1]<=stats[0] or not stats[2] or any(stats[3:]):
        raise RuntimeError(f'Alpha reference did not cover paired/native draws: {stats}')
    if replacement['enabled']:
        if proofs[0]!=stats[1] or proofs[1]!=stats[1] or proofs[2]<proofs[0] or proofs[3]!=stats[1]*292 or not proofs[4] or proofs[5]:
            raise RuntimeError(f'Alpha GPU snapshot proof incomplete: {proofs}')
    elif any(proofs):raise RuntimeError('Reference alpha unexpectedly captured optimized snapshots')


def validate_skinned_alpha(report):
    skinned=report.get('skinnedAlpha',dict(enabled=False,stats=[0]*6))
    if not isinstance(skinned,dict):raise RuntimeError('Invalid skinned alpha counters')
    stats=skinned.get('stats')
    if (type(skinned.get('enabled')) is not bool or not isinstance(stats,list) or len(stats)!=6 or
            any(type(value) is not int or not 0<=value<2**64 for value in stats)):
        raise RuntimeError('Invalid skinned alpha counters')
    if not skinned['enabled']:
        if any(stats):raise RuntimeError('Disabled skinned alpha path recorded work')
        return
    skin=report.get('skinning',{});alpha=report.get('alphaReference',{})
    reference=alpha.get('enabled',False);replacement=report.get('alphaOptimized',{}).get('enabled',False)
    if skin.get('enabled') is not True or type(reference) is not bool or type(replacement) is not bool or reference==replacement or report.get('nativeTiming'):
        raise RuntimeError('Skinned alpha requires skinning and one alpha path')
    totals=(alpha.get('stats'),skin.get('stats'))
    if any(not isinstance(values,list) or len(values)!=6 or
           any(type(value) is not int or not 0<=value<2**64 for value in values) for values in totals):
        raise RuntimeError('Invalid total counters for skinned alpha')
    if (not 0<stats[1]<=stats[0] or not stats[2] or any(stats[3:]) or
            any(stats[i]>values[i] for values in totals for i in range(6))):
        raise RuntimeError(f'Skinned alpha coverage incomplete or exceeds total work: {stats}')


def validate(report,expected,whole_target=False,optimized=False,rejected=False):
    validate_tracking(report)
    stats=report['stats'];image=report.get('comparison')
    if report['state']!=3 or report['result']:
        raise RuntimeError(f'Early paired GPU request failed: {stats}; details={report.get("failureData")}; camera={report.get("cameraBindingInfo")}; alpha={report.get("alphaOptimized")}')
    camera=report.get('nativeCamera',dict(enabled=False,stats=[0,0,0]))
    validate_camera_stages(camera,1)
    validate_alpha_reference(report,optimized)
    validate_skinned_alpha(report)
    if 'skinning' in report:validate_skinning(report['skinning'],optimized,report.get('alphaOptimized',{}).get('proofs'))
    if camera['stats']!=([1,stats['recorded'],1] if camera['enabled'] else [0,0,0]):
        raise RuntimeError('Native camera binding/byte validation or draw coverage is incomplete')
    gpu=report.get('gpuInstances')
    if (not gpu or len(gpu)!=4 or not 0<=gpu[0]<=stats['recorded'] or gpu[1]>stats['peerSeen'] or
            (optimized and (gpu[2]!=gpu[0] or gpu[3]<gpu[2]*48 or gpu[3]%48)) or
            (not optimized and (gpu[2] or gpu[3]))):
        raise RuntimeError('GPU-only instance sources lack their admission proof')
    if report['state']!=3 or report['result'] or (not optimized and (not image or not image['equal'])):
        raise RuntimeError(f'Early paired GPU output did not match native references: {report}')
    if expected==ALL_DRAWS:
        expected=stats['recorded']
        if not 0<expected<MAX_SOURCES:
            raise RuntimeError('Full depth selection is empty or reached its safety capacity')
    if stats['recorded']!=expected or stats['matched']!=expected or not stats['mainEnded'] or not stats['peerEnded']:
        raise RuntimeError(f'Early paired GPU coverage incomplete: {stats}')
    native=report.get('nativeTargets',{})
    total=native.get('mainDraws') if whole_target else expected
    if any(stats[n] for n in ('bankFailure','inputMismatch','queueFailure','failure')) or (not optimized and image['draws']!=total):
        raise RuntimeError(f'Early paired GPU inputs failed: {stats}')
    if whole_target and (native.get('enabled')!=(2 if optimized else 1) or native.get('mainDraws')!=stats['mainSeen'] or
            native.get('peerDraws')!=stats['peerSeen'] or native.get('mainFallback')!=native['mainDraws']-expected or
            native.get('mainExported')!=1 or native.get('peerExported')!=1 or native.get('imports')!=2 or
            (not optimized and report.get('nativeDifferences')!=[0,0,0,0])):
        raise RuntimeError('Whole native depth target or copy-back coverage is incomplete')
    coverage=report.get('coverage',[]);masks=report.get('masks',[]);plan=report.get('maskStats',{})
    if len(coverage)!=expected or len(masks)!=expected or any(
            not total or matched!=both or total!=both+main_only or min(matched,both,main_only)<0
            for (matched,total),(both,main_only) in zip(coverage,masks)):
        raise RuntimeError('Not every source instance has a verified early eye mask')
    if (not plan.get('mainPackets') or not plan.get('peerPackets') or plan.get('originFailure')!=0 or
            plan.get('both')!=sum(x[0] for x in masks) or plan.get('mainOnly')!=sum(x[1] for x in masks) or
            plan.get('mainInstances',0)<sum(x[1] for x in coverage) or plan.get('peerInstances',0)<plan['both'] or
            plan.get('pairedRuns',0)<expected):
        raise RuntimeError('Early CPU membership evidence is incomplete')
    extras=report.get('extraCoverage',[])
    if (len(extras)!=expected or any(matched!=total or total<0 for matched,total in extras) or
            plan.get('peerOnly')!=sum(x[1] for x in extras) or plan.get('peerOnlyRuns')!=sum(x[1]>0 for x in extras) or
            plan.get('unrecordedPeerOnly',-1)<0 or plan.get('knownPeerOnly')!=plan['peerOnly']+plan['unrecordedPeerOnly']):
        raise RuntimeError('Not every recorded VRCAM-only instance has a native reference')
    if plan['peerInstances']<plan['both']+plan['peerOnly']:
        raise RuntimeError('Recorded peer coverage exceeds the early CPU input')
    if not optimized and not plan['both'] and not plan['peerOnly'] and image.get('written',[1,0])[0]!=0:
        raise RuntimeError('MAIN-only plan wrote unexpected peer reference pixels')
    if optimized:
        o=report.get('optimized',{})
        if (image is not None or o.get('enabled')!=1 or o.get('gpuDecision')!=int(not rejected) or o.get('cpuAdmitted')!=int(not rejected) or
                o.get('referenceDraws')!=0 or stats['referenceDraws'] or
                o.get('mainCommands')!=native['mainFallback']+plan['pairedRuns']+plan['peerOnlyRuns'] or
                o.get('peerUnconditional',-1)+o.get('peerFallbackBatches',-1)!=native['peerDraws'] or
                o.get('peerUnconditional',-1)+o.get('peerOptimizedRuns',-1)!=native['peerRuns']):
            raise RuntimeError('Optimized depth admission or command accounting failed')
        executed=o['mainCommands']+o['peerUnconditional']+(o['peerFallbackBatches'] if rejected else o['peerOptimizedRuns'])
        saved=native['mainDraws']+native['peerDraws']-executed
        if report.get('netDrawCommandReduction')!=saved or report.get('nativeDrawsReplaced')!=max(0,saved):
            raise RuntimeError('Optimized draw reduction differs from executed command counts')
