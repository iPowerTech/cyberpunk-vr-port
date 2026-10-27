"""Witness original CPU pass boundaries, without inventing GPU completion."""
import struct
from source_cbv_report import _source_rows,_no_permission


def analyze(raw,source_report,ia=None,instances=None,gpu=None,*,require_cbvs=False):
    stats,sources=_source_rows(source_report)
    if not isinstance(raw,(bytes,bytearray)) or len(raw)!=320:
        raise ValueError('Source boundary ABI differs')
    rows=list(struct.iter_unpack('<20Q',raw));eyes=set()
    for order,row in enumerate(rows):
        eye=row[1]
        if row[0]!=stats[1] or eye not in (0,1) or eye in eyes or row[2]!=order or not row[16] or row[19]:
            raise ValueError('Source boundary lost its frame, native queue or pre-readback position')
        eyes.add(eye);selected=[s for s in sources if s[1]==eye]
        if not selected or row[5]!=len(selected) or not row[5]<=row[6]<=len(sources) or any(tuple(s[6:8])!=row[3:5] for s in selected):
            raise ValueError('Source boundary does not close the exact original recording')
        if row[7]!=order or any(row[i] not in (0,1) for i in (8,9,17)) or row[13]==2**64-1:
            raise ValueError('Source boundary peer state is invalid')
        if row[8] and (not row[9] or not all(row[i] for i in (10,11,12))):
            raise ValueError('Submitted peer has no original submission certificate')
        if not row[8] and row[12] or row[13]>row[12]:
            raise ValueError('Source boundary inferred an unobserved peer signal')
        if require_cbvs and row[17]!=1:raise ValueError('Source CBV arena was not finalized at its original pass boundary')
        if ia is not None:
            if not ia.get('verified') or row[14]!=sum(r[1]==eye and bool(r[14]) for r in ia['commands']):
                raise ValueError('Source IA finalization count differs at the boundary')
        if instances is not None:
            if (not instances.get('verified') or instances.get('mode')!='native-original' or not gpu or not gpu.get('verified')
                    or row[18]!=sum(r[1]==eye and bool(r[2]) for r in gpu['rows'])):
                raise ValueError('Native instance views were not available before readback')
    last=rows[1]
    if last[6]!=len(sources):raise ValueError('Last source boundary precedes remaining source preparation')
    same_list=rows[0][3:5]==last[3:5]
    ordered=bool(last[8] and last[9] and last[10]==last[16])
    return dict(verified=True,rows=[list(r) for r in rows],firstEye=rows[0][1],lastEye=last[1],
                allCpuSourcesAvailable=True,payloadReadbacksProcessed=False,sameList=same_list,
                peerSubmittedOnSameQueue=ordered,peerGpuAlreadyComplete=bool(last[12] and last[13]>=last[12]),
                producerOrderOpportunity=same_list or ordered,**_no_permission())


def submission(values,boundaries,*,required=False):
    if len(values)!=16 or any(type(v) is not int or not 0<=v<2**64 for v in values) or not boundaries.get('verified'):
        raise ValueError('Source submission-order ABI differs')
    rows={r[1]:r for r in boundaries['rows']};main=rows[0];peer=rows[1]
    if values[0]!=main[0] or values[1]!=main[16] or values[4:6]!=main[3:5] or values[14] not in (0,1,2):
        raise ValueError('Source submission targets a different native frame or MAIN recording')
    if values[14]==0:
        if required:raise ValueError('The actual submission did not order peer inputs before MAIN')
        return dict(verified=True,eligible=False,raw=values)
    if values[2:4]!=peer[3:5] or peer[16]!=values[1] or values[11:]!=[1,1,1,values[14],1] or not 0<values[8]<=256 or values[7]>=values[8]:
        raise ValueError('Source submission lacks finalized, certified and dispatched inputs')
    if values[14]==2:
        if values[9:11]!=[1,0] or not values[6]<values[7]:raise ValueError('Same-batch source ordering lacks separate Execute calls')
    elif values[10]!=1 or values[6]!=2**32-1 or values[9] not in (0,1):
        raise ValueError('Earlier source submission was not witnessed')
    return dict(verified=True,eligible=True,mode='separate-calls' if values[14]==2 else 'earlier-submission',
                raw=values,cpuFenceWaitRequired=False,nativeCommandsSuppressed=0)
