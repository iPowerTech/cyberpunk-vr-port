"""Check original-recording native descriptor reservations and material bindings."""
import struct
from source_cbv_report import _source_rows,_no_permission


def analyze(raw,source_report,table_report,entry_raw=None,ring_raw=None):
    _,sources=_source_rows(source_report)
    if not table_report.get('verified') or not isinstance(raw,(bytes,bytearray)) or len(raw)!=len(sources)*128:
        raise ValueError('Native descriptor reservations lack exact source tables')
    rows=list(struct.iter_unpack('<16Q',raw));intervals={};bound=descriptors=0;deferred={}
    rings=[];seen=set()
    if ring_raw is not None:
        if not isinstance(ring_raw,(bytes,bytearray)) or len(ring_raw)%128 or len(ring_raw)>2048*128:
            raise ValueError('Native descriptor ring ABI differs')
        rings=list(struct.iter_unpack('<16Q',ring_raw))
        for r in rings:
            source,registry,address,heap,capacity,head,tail,end_head,end_tail,first,count,absolute,pending,after_pending,no_wait,unchanged=r
            if source>=len(rows) or source in seen or not registry or address!=registry+0x149DE40 or address%8 or not heap or not 0<capacity<=1048576:
                raise ValueError('Native descriptor ring identity differs')
            seen.add(source);row=rows[source]
            if row[2]!=2 or row[6]!=heap or row[7:11]!=(first,count,0,count) or no_wait!=1 or unchanged!=1:
                raise ValueError('Native descriptor ring did not preserve its same-heap context')
            if (not 0<pending<=1048576 or not 0<after_pending<=1048576 or tail>head or head-tail>capacity or end_tail>end_head or end_head-end_tail>capacity
                    or not head<=end_head<=head+capacity or not tail<=end_tail<=head or not 0<count<=8192 or first>=capacity or count>capacity-first):
                raise ValueError('Native descriptor ring lacks live-context capacity/lifetime proof')
            start=head-head%capacity+first
            if start<head:start+=capacity
            if start!=absolute or absolute+count>end_head:
                raise ValueError('Native descriptor ring result does not match its atomic reservation')
    if seen!={r[0] for r in rows if r[2]==2}:
        raise ValueError('Native descriptor ring reservation lost its lifetime proof')
    entries=None
    if entry_raw is not None:
        if not isinstance(entry_raw,(bytes,bytearray)) or len(entry_raw)%64 or len(entry_raw)>131072*64:
            raise ValueError('Native descriptor reuse ABI differs')
        entries=list(struct.iter_unpack('<8Q',entry_raw))
    entry_at=allocated=reused=0;origins={};gpu_bases={}
    for ordinal,(row,source) in enumerate(zip(rows,sources)):
        if row[:2]!=(ordinal,source[1]) or row[11:13]!=tuple(source[6:8]) or row[2] not in (0,1,2):
            raise ValueError('Native descriptor reservation belongs to another source/recording')
        if not row[2]:
            if row[3] not in (4,7) or any(row[4:11]+row[13:]):raise ValueError('Invalid native descriptor deferral')
            deferred[row[3]]=deferred.get(row[3],0)+1
            continue
        finite=[t for t in table_report['rows'] if t[0]==ordinal and not t[14]]
        mask=sum(1<<t[2] for t in finite);expected=sum(t[3] for t in finite)
        if row[3] or row[4]!=expected or not 0<expected<=8192 or row[5]!=len(finite) or not row[6] or not row[8] or row[7]+row[8]>1048576:
            raise ValueError('Native descriptor heap/table coverage differs')
        fresh=expected
        if entries is not None:
            selected=entries[entry_at:entry_at+len(finite)];entry_at+=len(finite);fresh=0
            if len(selected)!=len(finite):raise ValueError('Native descriptor table proof is truncated')
            stride=table_report['resourceDescriptorStrideFromCamera']
            for entry,table in zip(selected,finite):
                if entry[:4]!=(ordinal,table[2],table[3],table[4]) or not entry[4] or entry[7] not in (0,1):
                    raise ValueError('Native descriptor reuse source table differs')
                key=(entry[5],entry[6])
                if entry[7]:
                    if entry[5]>=ordinal or key not in origins:raise ValueError('Native descriptor reuse has no earlier allocation')
                    original,owner,source_table=origins[key]
                    if owner[6]!=row[6] or owner[11:13]!=row[11:13] or entry[2:5]!=original[2:5] or table[5:8]!=source_table[5:8]:
                        raise ValueError('Native descriptor reuse crossed a heap, recording or immutable CPU version')
                    reused+=entry[2]
                else:
                    if key!=(ordinal,table[2]):raise ValueError('Fresh native descriptor table claims an older allocation')
                    base=entry[4]-(row[7]+row[9]+fresh)*stride
                    if base<=0 or (row[6] in gpu_bases and gpu_bases[row[6]]!=base):
                        raise ValueError('Fresh native descriptor table is outside its reserved GPU range')
                    gpu_bases[row[6]]=base;fresh+=entry[2];origins[key]=(entry,row,table)
        if row[9]+fresh!=row[10] or row[10]>row[8] or row[13]!=(source[13]&~mask) or row[14:]!=(1,1):
            raise ValueError('Native descriptors lack exact advancement, binding or submission')
        key=(row[6],row[11],row[12]);begin=row[7]+row[9];end=row[7]+row[10]
        for a,b in intervals.setdefault(key,[]):
            if begin<b and a<end:raise ValueError('Native descriptor reservations overwrite an earlier in-flight table')
        if fresh:intervals[key].append((begin,end))
        bound+=1;descriptors+=expected;allocated+=fresh
    if entries is not None and entry_at!=len(entries):raise ValueError('Unrelated native descriptor reuse rows remain')
    if not bound:raise ValueError('No source draw consumed native descriptor tables')
    return dict(verified=True,rows=[list(r) for r in rows],sources=len(sources),bound=bound,deferred=len(sources)-bound,
                deferredReasons=deferred,descriptors=descriptors,allocatedDescriptors=allocated,reusedDescriptors=reused,
                ringReservations=len(rings),ringDescriptors=sum(r[10] for r in rings),
                allSourcesBound=bound==len(sources),
                borrowedOriginalTables=True,sameRecordingOnly=True,laterFrameReuse=False,**_no_permission())
