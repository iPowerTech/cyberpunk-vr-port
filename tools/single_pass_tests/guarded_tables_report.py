"""Validate descriptor construction before source payload processing, not GPU readiness."""
import struct
from source_cbv_report import _source_rows


def analyze(stats, raw, source):
    if len(stats)!=16 or any(type(v) is not int or not 0<=v<2**64 for v in stats):
        raise ValueError('Guarded descriptor stats ABI differs')
    source_stats,original=_source_rows(source)
    if not isinstance(raw, bytes) or len(raw)%128:
        raise ValueError('Guarded descriptor row ABI differs')
    rows=list(struct.iter_unpack('<16Q',raw))
    if not 0<len(rows)==len(original)<=2048 or stats[:5]!=[2,source_stats[1],len(rows),len(rows),len(rows)]:
        raise ValueError('Not every source built guarded descriptor tables')
    if stats[7:]!=[1,1,1,1,0,0,0,2**64-1,0]:
        raise ValueError('Guarded tables waited for CPU payloads or missed source finalization')
    checks=expected=0
    for ordinal,(row,saved) in enumerate(zip(rows,original)):
        if row[:8]!=(ordinal,saved[1],saved[6],saved[7],ordinal+1,saved[11],1,0):
            raise ValueError('Guarded table scope differs from its original occurrence')
        if (not 0<row[8]<=64 or not 0<row[9]<=2 or not 0<row[10]<=135168 or not 0<row[11]<=128 or
                row[12]>4096 or row[12]%128 or row[10]<=row[12] or row[13:]!=(1,1,0)):
            raise ValueError('Guarded table checks, owners or pre-readback markers differ')
        if row[12] and (row[9]!=2 or row[10]!=228+row[12]):
            raise ValueError('Guarded multilayer B4/T5 check coverage differs')
        checks+=row[9]
        expected+=row[10]
    if stats[5:7]!=[checks,expected]:
        raise ValueError('Guarded descriptor aggregate coverage differs')
    return dict(verified=True,stats=stats,rows=[list(row) for row in rows],sources=len(rows),
                checks=checks,expectedBytes=expected,constructedBeforeCpuReadback=True,
                gpuChecksRequired=True,sourceContentsCpuVerified=False,nativeSuppression=0)


def validate_cache(early, current, guarded_rows, verified_commands):
    for values in (early,current):
        if (len(values)!=4 or any(type(v) is not int or not 0<=v<2**64 for v in values) or
                values[0]>1000000 or values[1]!=values[3]):
            raise ValueError('Guarded/verified immutable cache ABI differs')
    guarded=sum(row[8] for row in guarded_rows)
    verified=sum(row[28] for row in verified_commands if row[21])
    if early[2]+early[3]!=guarded or current[2]+current[3]!=guarded+verified or any(a>b for a,b in zip(early,current)):
        raise ValueError('Guarded/verified cache totals or phase ordering differ')
