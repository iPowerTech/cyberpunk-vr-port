"""Decode observed output-access and native lifetime checks, never permissions."""
import struct

STATS = ('schema', 'armed', 'finished', 'epoch', 'recordings', 'resources', 'accessRows', 'markers',
         'resets', 'closes', 'sources', 'exits', 'queries', 'qualified', 'failure', 'permission')
SOURCES = ('frame', 'side', 'list', 'journalGeneration', 'nativeGeneration', 'token', 'wholeCoverage',
           'closed', 'closeHRESULT', 'firstRow', 'rowCount', 'sourceRow', 'exitRow', 'failure', 'sourceIndex', 'qualified')
CHECKS = ('side', 'phase', 'rows', 'reads', 'writes', 'transitions', 'aliases', 'shaders', 'excludedShaders',
          'proofReads', 'proofScopes', 'failure', 'failureRow', 'failureTarget', 'qualified', 'permission')
LIFETIME = ('side', 'slot', 'captured', 'bound', 'validatedAfterPrefix', 'failure', 'stage', 'readerCode',
            'handleRefs', 'ownerRefs', 'owner', 'allocation', 'heap', 'bucket', 'bucketIndex', 'queueSlot')
FAILURE_EVENT = ('side','list','journalGeneration','row','kind','tag','resource','other')
BREADCRUMBS = ('attempts','resolved','fallbackUnknown','staleRecording','metadataConflicts',
               'lastResolverFailure','lastDestination','lastResource')


def read_report(read_export, attempts=8):
    def read(name, count, code='Q'):
        size=struct.calcsize(f'<{count}{code}');raw=read_export(name,size)
        if not isinstance(raw,bytes) or len(raw)!=size:
            raise ValueError('Output target observation ABI differs')
        return list(struct.unpack(f'<{count}{code}',raw))

    prefix='CyberpunkVR_TargetAccess'
    for _ in range(attempts):
        sequence=read(prefix+'Seq',1,'I')[0]
        if sequence&1:continue
        stats=read(prefix+'Stats',16);sources=read(prefix+'Sources',32);checks=read(prefix+'Checks',32)
        failure_event=read(prefix+'FailureEvent',8) if stats[0] in (2,3) else None
        breadcrumbs=read(prefix+'BreadcrumbStats',8) if stats[0]==3 else None
        if read(prefix+'Seq',1,'I')[0]!=sequence:continue
        if stats[0] not in (1,2,3) or stats[15] or checks[15] or checks[31]:
            raise ValueError('Output target observer schema or permission differs')
        if any(value>limit for value,limit in zip(stats[4:8],(256,2048,32768,8192))):
            raise ValueError('Output target observer bounds exceeded')
        lifetime=read('CyberpunkVR_RetainedTargetLifetime',128)
        rows=lambda values,fields:[dict(zip(fields,values[i:i+len(fields)])) for i in range(0,len(values),len(fields))]
        return dict(classification='OBSERVATION',exportPermission=False,residencyPermission=False,
                    nativeSuppressionPermission=False,sequence=sequence,stats=dict(zip(STATS,stats)),
                    sources=rows(sources,SOURCES),checks=rows(checks,CHECKS),lifetime=rows(lifetime,LIFETIME),
                    failureEvent=dict(zip(FAILURE_EVENT,failure_event)) if failure_event is not None else None,
                    breadcrumbs=dict(zip(BREADCRUMBS,breadcrumbs)) if breadcrumbs is not None else None,
                    raw=dict(stats=stats,sources=sources,checks=checks,lifetime=lifetime,failureEvent=failure_event,breadcrumbs=breadcrumbs))
    raise ValueError('Output target observation changed during reading')
