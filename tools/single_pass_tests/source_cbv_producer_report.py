"""Current native CBV producer/recording joins, never copy permission."""
import struct

from source_cbv_report import MAX_ROWS, _no_permission, _source_rows, _words, analyze as analyze_cbvs

ROW_WORDS = 24
ROW_BYTES = ROW_WORDS * 8
DIRECT_CREATOR = 0x1F2960
DIRECT_COMPLETION = 0x1F292F
TRANSIENT_CREATOR = 0x1F0BF7
TRANSIENT_COMPLETION = 0x1F08C2


def analyze(raw, source_report, cbv_report):
    source_stats, sources = _source_rows(source_report)
    if not isinstance(cbv_report, dict) or cbv_report.get('verified') is not True:
        raise ValueError('Producer report lacks verified current source CBVs')
    stats = _words(cbv_report.get('stats'), 16, 'source-CBV stats')
    values = cbv_report.get('rows')
    if not isinstance(values, (list, tuple)) or len(values) != stats[3] or len(values) > MAX_ROWS:
        raise ValueError('Producer source-CBV membership extent differs')
    cbvs = [_words(row, 24, 'source-CBV row') for row in values]
    checked = analyze_cbvs(stats, b''.join(struct.pack('<24Q', *row) for row in cbvs), source_report)
    for field in ('sourceCommands', 'cbvCount', 'ownedCount'):
        if type(cbv_report.get(field)) is not int or cbv_report[field] != checked[field]:
            raise ValueError('Producer source-CBV aggregate differs: ' + field)
    if not isinstance(raw, (bytes, bytearray, memoryview)) or (isinstance(raw, memoryview) and
            (not raw.c_contiguous or raw.itemsize != 1 or raw.ndim != 1)) or len(raw) not in (len(cbvs) * ROW_BYTES,len(cbvs) * 320):
        raise ValueError('Producer binary extent/type differs from the current CBV set')
    words=40 if cbvs and len(raw)==len(cbvs)*320 else 24
    rows = [tuple(row)+(0,)*(40-words) for row in struct.iter_unpack(f'<{words}Q', raw)]
    leases, tokens, entries = {}, {}, []
    for row, cbv in zip(rows, cbvs):
        source = sources[cbv[0]]
        if row[:6] != tuple(cbv[:6]) or row[6] != cbv[22] or row[9:12] != (cbv[19], cbv[12], cbv[10]):
            raise ValueError('Producer occurrence/logical/creator/backing differs from its exact CBV')
        if row[23] or row[22] not in (0, 1) or row[32] not in (0,1) or any(row[33:]) or row[8] not in (0, DIRECT_COMPLETION,TRANSIENT_COMPLETION):
            raise ValueError('Producer completion/eligibility/reserved ABI is unsupported')
        if row[9] not in (DIRECT_CREATOR,TRANSIENT_CREATOR) and (row[7] or row[8]):
            raise ValueError('Non-direct CBV acquired direct producer metadata')
        if not row[12] or row[13:17] != (source[6], source[7], source[8], source[9]):
            raise ValueError('Producer current source token/list/generation/context/registry differs')
        lease = tuple(row[13:17])
        if leases.setdefault(lease, row[12]) != row[12] or tokens.setdefault(row[12], lease) != lease:
            raise ValueError('Producer source recording token identity is inconsistent')
        eligible = ((cbv[2], cbv[3], cbv[7], cbv[19], cbv[21]) == (1, 5, 3, DIRECT_CREATOR, 0) and
                    row[8] == DIRECT_COMPLETION and 0 < row[6] <= 65536 and row[6] >= cbv[6] and
                    row[7] == ((row[6] + 255) & ~255) == cbv[11] and row[12:17] == row[17:22])
        if row[22] and not eligible:
            raise ValueError('Direct producer eligibility lacks exact completed bytes and creation/source recording identity')
        transient=(cbv[7]==3 and cbv[19]==TRANSIENT_CREATOR and cbv[21]>0 and row[8]==TRANSIENT_COMPLETION and
                   row[24]>0 and row[26]>0 and 0<cbv[6]<=row[25]<=row[6]<=65536 and
                   row[7]==((row[6]+255)&~255)==cbv[11] and row[12:17]==row[17:22]==row[27:32])
        if row[32] and not transient:
            raise ValueError('Transient producer lacks exact completed prefix and creation/writer/source recording identity')
        if row[9]!=TRANSIENT_CREATOR and any(row[24:33]):
            raise ValueError('Another creator acquired transient writer closure')
        entries.append(dict(sourceOrdinal=row[0], side=row[1], visibility=row[2], shaderRegister=row[3],
            parameter=row[4], tableOffset=row[5], logicalBytes=row[6], reservedBytes=row[7],
            completedProducerRva=row[8], creatorRva=row[9], sourceResource=row[10], cbvGpuAddress=row[11],
            sourceToken=row[12], sourceList=row[13], sourceGeneration=row[14], sourceContext=row[15], sourceRegistry=row[16],
            creationToken=row[17], creationList=row[18], creationGeneration=row[19], creationContext=row[20], creationRegistry=row[21],
            inlineDirectEligible=bool(row[22]), writerNonce=row[24],writerBytes=row[25],writerDestination=row[26],
            writerToken=row[27],writerList=row[28],writerGeneration=row[29],writerContext=row[30],writerRegistry=row[31],
            inlineTransientEligible=bool(row[32]),producerRecordingJoinOnly=True, **_no_permission()))
    return dict(verified=True, rows=[list(row) for row in rows], producers=entries, frame=source_stats[1],
                sourceCommands=len(sources), cbvCount=len(rows), directEligibleCount=sum(row[22] for row in rows),
                transientEligibleCount=sum(row[32] for row in rows),
                producerRecordingJoinOnly=True, **_no_permission())
