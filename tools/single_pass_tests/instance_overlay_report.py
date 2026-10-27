"""Check compact private VB versions against original fence-completed GPU bytes."""
import struct
from source_cbv_report import _source_rows, _no_permission


def analyze(stats, raw, blob, gpu_blob, gpu_report, source_report, writers, mode=1):
    if mode==2:return analyze_native(stats,raw,blob,gpu_blob,gpu_report,source_report,writers)
    if mode!=1:raise ValueError('Unknown instance capture mode')
    _,sources=_source_rows(source_report)
    if len(stats)!=8 or stats[0]!=2 or any(stats[6:]) or not gpu_report.get('verified') or not writers.get('verified'):
        raise ValueError('Instance overlay lacks completed source reports')
    if not isinstance(raw,(bytes,bytearray)) or len(raw)%192 or len(raw)//192>2048 or not isinstance(blob,(bytes,bytearray)):
        raise ValueError('Instance overlay ABI differs')
    rows=list(struct.iter_unpack('<24Q',raw));eligible=[r for r in gpu_report['rows'] if r[5]==1]
    if len(rows)!=len(eligible) or stats[1]!=len(rows) or stats[2]!=len(blob) or stats[3]>64*1024*1024:
        raise ValueError('Instance overlay omitted a matched version or exceeded its budget')
    if rows and (not stats[4] or stats[5]<stats[4] or stats[5]==2**64-1):raise ValueError('Instance overlay destination fence is incomplete')
    cursor=0;dense=0;width=0;identity=None
    for version,(row,original) in enumerate(zip(rows,eligible)):
        ordinal=original[0];source=sources[ordinal];cpu=writers['rows'][ordinal]
        if row[:9]!=(ordinal,source[1],version,source[11],source[6],source[7],ordinal+1,source[10],source[12]) or row[9:12]!=(cpu[12],cpu[13],cpu[14]):
            raise ValueError('Instance overlay borrowed another source, PSO or view')
        stride=row[11];logical=source[24]*stride;copied=source[21]*stride
        if (stride,row[10]) not in ((48,6*1024*1024),(64,2*1024*1024)) or copied!=row[16] or copied!=original[11] or logical+copied>row[10]:
            raise ValueError('Instance overlay original draw domain differs')
        target=max(((cursor+stride-1)//stride)*stride,logical);view_offset=target-logical
        if row[15]!=target or row[17]!=stats[3] or view_offset+row[10]>row[17] or row[14]<view_offset or row[18:21]!=(1,1,1) or row[21]!=original[18]:
            raise ValueError('Compact private view changed original addressing or completion')
        current=(row[12],row[13],row[14]-view_offset,row[22],row[23])
        if not all(current) or (identity is not None and current!=identity):raise ValueError('Instance overlay backing/copy identity differs')
        identity=current
        if row[21]-1>len(gpu_blob) or copied>len(gpu_blob)-(row[21]-1) or blob[dense:dense+copied]!=gpu_blob[row[21]-1:row[21]-1+copied]:
            raise ValueError('Private instance GPU bytes differ from original GPU inputs')
        cursor=target+copied;width=max(width,view_offset+row[10]);dense+=copied
    if dense!=stats[2] or width!=stats[3]:raise ValueError('Instance overlay storage accounting differs')
    return dict(verified=True,stats=list(stats),rows=[list(r) for r in rows],versions=len(rows),copiedBytes=dense,
                backingBytes=width,gpuBytesMatched=True,privateIaConsumed=False,wholeInputEquivalenceProved=False,**_no_permission())


def analyze_native(stats, raw, blob, gpu_blob, gpu_report, source_report, writers):
    _,sources=_source_rows(source_report)
    if len(stats)!=8 or stats[0]!=2 or any(stats[6:]) or not gpu_report.get('verified') or not writers.get('verified'):
        raise ValueError('Native instance copies lack completed original source reports')
    if not isinstance(raw,(bytes,bytearray)) or len(raw)%192 or len(raw)//192>2048 or not isinstance(blob,(bytes,bytearray)):
        raise ValueError('Native instance copy ABI differs')
    rows=list(struct.iter_unpack('<24Q',raw))
    captured=[r for r in gpu_report['rows'] if r[2]]
    eligible=[r for r in captured if r[5]==1]
    cursor=[0,0];width=[0,0];versions=[0,0];mappings={};copied_total=0
    # Reserve every recorded source, even if its permission/byte check later
    # failed. An excluded source must not shift a later private version's address.
    for item in captured:
        ordinal,eye=item[:2];source=sources[ordinal];cpu=writers['rows'][ordinal]
        stride=cpu[14];logical=source[24]*stride;copied=source[21]*stride
        if (stride,cpu[13]) not in ((48,6*1024*1024),(64,2*1024*1024)) or copied!=item[11] or logical+copied>cpu[13]:
            raise ValueError('Native instance copy original view domain differs')
        target=max(((cursor[eye]+stride-1)//stride)*stride,logical);view_offset=target-logical
        mappings[ordinal]=(versions[eye],target,view_offset)
        cursor[eye]=target+copied;width[eye]=max(width[eye],view_offset+cpu[13]);versions[eye]+=1;copied_total+=copied
    if len(rows)!=len(eligible) or stats[1:4]!=[len(captured),copied_total,sum(width)] or max(width)>32*1024*1024:
        raise ValueError('Native instance copy coverage or arena accounting differs')
    if captured and (not stats[4] or stats[5]<stats[4] or stats[5]==2**64-1):
        raise ValueError('Original instance-copy submission fence is incomplete')
    dense=0;identities={}
    for row,item in zip(rows,eligible):
        ordinal,eye=item[:2];source=sources[ordinal];cpu=writers['rows'][ordinal];version,target,view_offset=mappings[ordinal]
        if row[:9]!=(ordinal,eye,version,source[11],source[6],source[7],ordinal+1,source[10],source[12]) or row[9:12]!=(cpu[12],cpu[13],cpu[14]):
            raise ValueError('Native instance copy borrowed a different source or version')
        if row[15:22]!=(target,item[11],width[eye],1,1,1,item[18]) or row[22:24]!=tuple(source[6:8]):
            raise ValueError('Instance copy was not recorded in its ORIGINAL native recording')
        identity=(row[12],row[13],row[14]-view_offset,row[22],row[23])
        if row[14]<view_offset or not all(identity) or identity[2]%256 or (eye in identities and identities[eye]!=identity):
            raise ValueError('Native instance private arena identity differs')
        identities[eye]=identity
        at=item[18]-1;copied=item[11]
        if at<0 or at>len(gpu_blob) or copied>len(gpu_blob)-at or blob[dense:dense+copied]!=gpu_blob[at:at+copied]:
            raise ValueError('Native instance private bytes differ from original GPU read')
        dense+=copied
    if dense!=len(blob):raise ValueError('Native instance verification payload extent differs')
    if len(identities)==2 and any(identities[0][i]==identities[1][i] for i in range(3)):
        raise ValueError('Different native recordings unexpectedly share an instance arena')
    return dict(verified=True,mode='native-original',stats=list(stats),rows=[list(r) for r in rows],versions=len(rows),
                capturedVersions=len(captured),copiedBytes=copied_total,verifiedBytes=dense,viewExtentBytes=sum(width),
                # Native mode2 creates a fixed32MiB arena for each participating
                # eye; required view extent is not actual allocation savings.
                allocatedBytes=32*1024*1024*sum(v>0 for v in versions),
                gpuBytesMatched=True,instanceCpuUploadBytes=0,extraCopySubmissions=0,privateIaConsumed=False,
                wholeInputEquivalenceProved=False,**_no_permission())
