"""Inspect saved exact-source multilayer bytes without acquiring runtime owners."""

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct

from t5_snapshot_report import analyze as analyze_t5


ROOT_HASH = '030ea7831db8493ad9b03f05ae54aa8d70e27211b65a93f9c9e155a925110a7c'
VS_HASH = 'd13385d2f6f7206586c8927d9df03e9b2c6b83adb27e6d23c7589f7f496e31aa'
PS_HASH = '9a4947a3ec0eb3d4605d88b663bb27a0bc809fe8a3d91c4ae67886bd77148db8'
STRIDE = 128
EXTENT = 32768
SITES = (
    ('%520', 100, 'identity'), ('%624', 108, 'high16'),
    ('%642', 108, 'low16'), ('%660', 104, 'low16'),
    ('%718', 104, 'high16'), ('%991', 100, 'identity'),
    ('%1093', 108, 'high16'), ('%1111', 108, 'low16'),
    ('%1129', 104, 'low16'), ('%1187', 104, 'high16'),
)
COPY_FIELDS = (
    'version', 'resource', 'sequence', 'generation', 'publicationGeneration',
    'producerList', 'sourceResource', 'destinationOffset', 'sourceOffset',
    'bytes', 'destinationGpu', 'destinationWidth', 'sourceHeapType',
    'sourceCpuPage', 'sourceHeapFlags', 'hintPresent', 'hintGeneration',
    'hintFresh', 'hintOffset', 'hintBytes', 'outcome', 'stage', 'reserved',
    'checksum',
)
MARK_FIELDS = (
    'version', 'resource', 'sequence', 'generation', 'outcome', 'firstElement',
    'numElements', 'stride', 'format', 'dimension', 'componentMapping',
    'flags', 'viewOffset', 'viewBytes', 'resourceGpu', 'resourceWidth',
    'heapType', 'cpuPage', 'heapFlags', 'hintAccepted', 'stage', 'reserved0',
    'reserved1', 'checksum',
)


def require(value, reason):
    if not value:
        raise ValueError(reason)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def rows(data, words):
    require(len(data) % (words * 8) == 0, 'Torn binary ABI row extent')
    return [list(row) for row in struct.iter_unpack(f'<{words}Q', data)]


def intervals(indices):
    result = []
    for index in sorted(set(indices)):
        if result and result[-1]['offset'] + result[-1]['count'] == index:
            result[-1]['count'] += 1
        else:
            result.append(dict(offset=index, count=1))
    return result


def plan(b4, first_element, num_elements, resource_width):
    require(len(b4) == 228, 'b4 must be its exact original228 logical view')
    base = struct.unpack_from('<f', b4, 128)[0]
    mask = struct.unpack_from('<I', b4, 224)[0]
    require(math.isfinite(base) and 0 <= base < 2**32,
            'b4 base exceeds supported FPToui domain')
    require(0 <= first_element <= (2**64 - 1) // STRIDE
            and 0 < num_elements <= 2**32 - 1
            and first_element * STRIDE + num_elements * STRIDE <= resource_width,
            'Original structured view exceeds its COM buffer')
    base = math.trunc(base)
    records = [base + bit for bit in range(32) if mask & (1 << bit)]
    require(all(record < num_elements and record < 2**32 for record in records),
            'Conservative record exceeds original view')
    first = records[0] if records else 0
    span = records[-1] - first + 1 if records else 0
    offset = (first_element + first) * STRIDE if records else 0
    size = span * STRIDE
    require(size <= 4096 and offset + size <= resource_width,
            'Conservative compact read exceeds its COM buffer')
    return dict(baseRecord=base, mask=mask, records=records,
                firstRecord=first, recordSpan=span,
                resourceOffset=offset, byteExtent=size)


def enumerate_records(read_plan, payload):
    require(len(payload) == read_plan['byteExtent'], 't5 payload has wrong exact extent')
    records, selections = [], []
    for record in read_plan['records']:
        at = (record - read_plan['firstRecord']) * STRIDE
        fields = {offset: struct.unpack_from('<I', payload, at + offset)[0]
                  for offset in (100, 104, 108)}
        transformed = {}
        for site, offset, transform in SITES:
            value = fields[offset]
            index = value if transform == 'identity' else (
                value & 65535 if transform == 'low16' else value >> 16)
            selections.append(dict(record=record, site=site, fieldOffset=offset,
                                   transformation=transform, raw=value, index=index))
            transformed[f'{offset}:{transform}'] = index
        records.append(dict(record=record, rawFields=fields, transformed=transformed))
    candidates = sorted({entry['index'] for entry in selections})
    out_of_range = [entry for entry in selections if entry['index'] >= EXTENT]
    return dict(records=records, selections=selections, candidateIndices=candidates,
                sparseIntervals=intervals(candidates), evaluatedSites=len(selections),
                outOfRangeSelections=out_of_range,
                zeroIndexSelections=sum(entry['index'] == 0 for entry in selections),
                zeroMask=not read_plan['mask'], fallbackSubstitutions=0,
                enumerationAdmissible=not out_of_range,
                dynamicTableRequired=bool(candidates) and not out_of_range,
                compactDescriptorCount=len(candidates) if not out_of_range else 0,
                originalTableExtent=max(candidates) + 1 if candidates and not out_of_range else 0)


def validate_audit(audit):
    require(audit['family'] == dict(family=13, root=ROOT_HASH, vs=VS_HASH, ps=PS_HASH),
            'Selector audit changed the exact original family')
    require(audit['vertexBindlessSites'] == 0 and audit['pixelBindlessSites'] == 10,
            'Original site count differs')
    require([(site['createHandleSSA'], site['fieldByteOffset'], site['transformation'])
             for site in audit['sites']] == list(SITES), 'Original selector transforms differ')
    require(all(site['irChainMatched'] and site['originalParameter'] == 31
                and site['space'] == 1 and site['declaredExtent'] == EXTENT
                for site in audit['sites']), 'Original PS binding audit is incomplete')


def decode_diagnostic(row, fields, version, resource):
    require(len(row) == 32 and row[2] == 1 and not row[3]
            and not any(row[28:]), 'Missing or torn original diagnostic receipt')
    values = row[4:28]
    require(values[0] == version and values[1] == resource and values[2],
            'Diagnostic borrowed another resource or version')
    checksum = 14695981039346656037
    for byte in struct.pack('<23Q', *values[:23]):
        checksum = ((checksum ^ byte) * 1099511628211) & (2**64 - 1)
    require(values[23] == checksum, 'Diagnostic checksum differs')
    result = dict(zip(fields, values))
    require(not result.get('reserved', 0) and not result.get('reserved0', 0)
            and not result.get('reserved1', 0), 'Diagnostic reserved fields changed')
    return result


def analyze_capture(preparation, audit):
    preparation = Path(preparation).resolve()
    report = json.loads(preparation.read_text())
    stem, directory = preparation.stem, preparation.parent
    files = {}

    def binary(suffix, words=None, expected=None):
        path = directory / f'{stem}-{suffix}.bin'
        data = path.read_bytes()
        files[path.name] = dict(bytes=len(data), sha256=sha(data))
        value = rows(data, words) if words else data
        if expected is not None:
            require(value == expected, f'{path.name} differs from published JSON rows')
        return data

    require(report['passed'] is True, 'Capture did not pass its source run')
    source = report['sourceStream']
    sources = source['rows']
    binary('source-stream', 48, sources)
    selectors = report['sourceSelectors']
    commands = selectors['commands']
    binary('selector-command', 32, commands)
    inputs = selectors['inputs']
    binary('selector-input', 24, inputs)
    slots = selectors['slots']
    binary('selector-slot', 16, slots)
    metas = selectors['t5']
    metadata_raw = binary('selector-t5', 32, metas)
    native_raw = binary('t5-native', 96, report['t5Snapshots']['native'])
    snapshot_raw = binary('t5-snapshot', 32, report['t5Snapshots']['snapshots'])
    gpu = binary('t5-snapshot-payloads')
    t5_intent = binary('t5-intent-payloads')
    t5 = analyze_t5(native_raw, snapshot_raw, gpu, t5_intent, source, metadata_raw)
    for key in ('nativeCaptured', 'copied', 'declined', 'empty', 'gpuBytes', 'candidateBytes', 'gpuOnly'):
        require(t5[key] == report['t5Snapshots'][key], 'Published t5 summary differs')
    cbvs = report['cbvSnapshots']['rows']
    binary('cbv-snapshots', 32, cbvs)
    b4_gpu = binary('cbv-snapshot-payloads')
    b4_intent = binary('selector-payloads')
    copies = report['t5Diagnostics']['Copy']
    marks = report['t5Diagnostics']['Mark']
    binary('t5-copy-diagnostic', 32, copies)
    binary('t5-mark-diagnostic', 32, marks)
    require(len(copies) == len(marks) == len(metas), 'Diagnostic occurrence count differs')
    shaders = {record['id']: record for record in report['nativeShaderSources']['records']}
    native_directory = directory / report['nativeShaderSources']['directory']
    shader_files = {}
    occurrences = []
    for meta, native, snapshot, copy_row, mark_row in zip(metas, t5['native'], t5['snapshots'], copies, marks):
        ordinal, eye = meta[:2]
        source_row, command = sources[ordinal], commands[ordinal]
        require(command[:2] == meta[:2] and command[25] == 1 and command[18] == 13
                and command[28] == len(occurrences) + 1, 'Not the original multilayer command')
        shader = shaders[source_row[12]]
        require(int(shader['pipeline'], 16) == source_row[10], 'Original pipeline label differs')
        for stage, digest in (('root', ROOT_HASH), ('vs', VS_HASH), ('ps', PS_HASH)):
            descriptor = shader[stage]
            require(descriptor['sha256'] == digest, 'Original shader/root SHA256 differs')
            if digest not in shader_files:
                data = (native_directory / descriptor['file']).read_bytes()
                require(len(data) == descriptor['bytes'] and sha(data) == digest,
                        'Original shader/root artifact bytes differ')
                shader_files[digest] = dict(stage=stage, bytes=len(data), sha256=digest)
        own_inputs = [row for row in inputs if row[0] == ordinal]
        require(len(own_inputs) == 1, 'Multilayer occurrence lost exact b4 input')
        input_row = own_inputs[0]
        require(input_row[:7] == [ordinal, eye, 5, 4, 19, 0, 228]
                and input_row[7] == 1 and input_row[11] == ordinal + 1
                and input_row[13] == 1 and input_row[16] == 228
                and input_row[21:23] == [source_row[7], source_row[8]]
                and source_row[13] & (1 << 19), 'Original b4 logical view or recording differs')
        snapshot_index = input_row[14] - 1
        require(0 <= snapshot_index < len(cbvs), 'b4 input has no actual GPU snapshot')
        b4_snapshot = cbvs[snapshot_index]
        require(b4_snapshot[:7] == input_row[:7] and b4_snapshot[8] == 1
                and b4_snapshot[20] == 228 and b4_snapshot[29:31] == [1, 1]
                and b4_snapshot[12:14] == input_row[19:21]
                and b4_snapshot[23:27] == [input_row[17], input_row[18], input_row[21], input_row[22]],
                'b4 GPU snapshot borrowed another original source view')
        cpu_offset, gpu_offset = input_row[15] - 1, b4_snapshot[31] - 1
        require(cpu_offset >= 0 and cpu_offset + 228 <= len(b4_intent)
                and gpu_offset >= 0 and gpu_offset + 228 <= len(b4_gpu), 'b4 payload extent differs')
        b4 = b4_gpu[gpu_offset:gpu_offset + 228]
        require(b4 == b4_intent[cpu_offset:cpu_offset + 228], 'Original b4 CPU/GPU bytes differ')
        read_plan = plan(b4, meta[11], meta[12], native[35])
        require(meta[13] == STRIDE and meta[4:6] == [23, 5]
                and read_plan['resourceOffset'] == snapshot[10]
                and read_plan['byteExtent'] == snapshot[11], 't5 snapshot differs from exact b4 plan')
        start = snapshot[27] - 1 if snapshot[2] == 1 else 0
        payload = gpu[start:start + snapshot[18]] if snapshot[2] == 1 else b''
        require(snapshot[2] != 3, 'Declined copy has no GPU bytes to enumerate')
        enumeration = enumerate_records(read_plan, payload)
        require(copy_row[:2] == meta[:2] and mark_row[:2] == meta[:2], 'Diagnostic source occurrence differs')
        copy = decode_diagnostic(copy_row, COPY_FIELDS, 0x4350494e54303031, meta[10])
        mark = decode_diagnostic(mark_row, MARK_FIELDS, 0x53564d41524b3031, meta[10])
        copy_end = copy['destinationOffset'] + copy['bytes']
        read_end = read_plan['resourceOffset'] + read_plan['byteExtent']
        intersects = copy['destinationOffset'] < read_end and read_plan['resourceOffset'] < copy_end
        occurrences.append(dict(ordinal=ordinal, eye=eye, pipelineId=source_row[12],
            sourceRecording=dict(list=source_row[6], generation=source_row[7], context=source_row[8]),
            originalB4Sha256=sha(b4), b4CpuGpuEqual=True, plan=read_plan,
            payloadSha256=sha(payload), enumeration=enumeration,
            nativeBacking=dict(resource=meta[10], gpu=native[9], handle=native[7], kind=native[8],
                               logicalBytes=native[10], backingBytes=native[35], stateStatus=native[20], state=native[21]),
            selectorStatus=command[2], selectorError=command[3],
            capturedOriginalSlots=sum(row[0] == ordinal for row in slots),
            cpuT5CandidatePresent=bool(snapshot[25]), cpuT5GpuMatched=snapshot[28] == 1,
            latestCopy=copy, latestMark=mark, latestCopyIntersectsRequiredRead=intersects))
    require({row[0] for row in metas} == {row[0] for row in commands if row[25]},
            'Multilayer metadata omitted original commands')
    candidate_union = sorted({index for row in occurrences for index in row['enumeration']['candidateIndices']})
    result = dict(preparation=str(preparation), preparationSha256=sha(preparation.read_bytes()),
        originalAuditCanonicalSha256=sha(json.dumps(audit, sort_keys=True).encode()), binaryArtifacts=files,
        verifiedOriginalShaders=list(shader_files.values()), scopeSourceCommands=len(sources),
        multilayerOccurrences=len(occurrences), eyes=dict(Counter(row['eye'] for row in occurrences)),
        gpuCopiedOccurrences=t5['copied'], gpuBytes=len(gpu), cpuT5CandidateBytes=len(t5_intent),
        uniqueGpuPayloadHashes=dict(Counter(row['payloadSha256'] for row in occurrences)),
        uniqueB4Hashes=dict(Counter(row['originalB4Sha256'] for row in occurrences)),
        candidateUnion=candidate_union, sparseIntervals=intervals(candidate_union),
        uniqueCandidateCount=len(candidate_union), totalEvaluatedSites=sum(row['enumeration']['evaluatedSites'] for row in occurrences),
        outOfRangeSelections=sum(len(row['enumeration']['outOfRangeSelections']) for row in occurrences),
        zeroIndexSelections=sum(row['enumeration']['zeroIndexSelections'] for row in occurrences),
        zeroMaskOccurrences=sum(row['enumeration']['zeroMask'] for row in occurrences),
        fallbackSubstitutions=0, cpuT5CandidateOccurrences=sum(row['cpuT5CandidatePresent'] for row in occurrences),
        cpuT5GpuMatchedOccurrences=sum(row['cpuT5GpuMatched'] for row in occurrences),
        capturedOriginalMultilayerSlots=sum(row['capturedOriginalSlots'] for row in occurrences),
        occurrences=occurrences, originalCallbackGpuCopyReceiptsVerified=True,
        conservativeCandidatesOnly=True, exactPixelSelectedIndicesProved=False,
        runtimeSourceOwnersAcquired=False, beforeDrawSlotCoverageComplete=False,
        copyPermission=False, inputContentsFrozen=False, nativeExecutionPermission=False,
        nativeCommandsSuppressed=0, performanceGainProved=False)
    return result


def compare_captures(primary, repeat):
    a, b = primary['occurrences'], repeat['occurrences']
    require(len(a) == len(b), 'Capture occurrence counts differ')
    pairs = list(zip(a, b))
    return dict(pairedByOriginalOrdinalAndEye=all((x['ordinal'], x['eye']) == (y['ordinal'], y['eye']) for x, y in pairs),
        originalShadersEqual=primary['verifiedOriginalShaders'] == repeat['verifiedOriginalShaders'],
        plansEqual=all(x['plan'] == y['plan'] for x, y in pairs),
        b4PayloadsEqual=all(x['originalB4Sha256'] == y['originalB4Sha256'] for x, y in pairs),
        t5GpuPayloadsEqual=all(x['payloadSha256'] == y['payloadSha256'] for x, y in pairs),
        candidateSetsEqual=all(x['enumeration']['candidateIndices'] == y['enumeration']['candidateIndices'] for x, y in pairs),
        sparseIntervalsEqual=primary['sparseIntervals'] == repeat['sparseIntervals'],
        sameBackingLabels=all(x['nativeBacking'] == y['nativeBacking'] for x, y in pairs),
        recordingGenerationsEqual=all(x['sourceRecording'] == y['sourceRecording'] for x, y in pairs),
        diagnosticReceiptsEqual=all((x['latestCopy'], x['latestMark']) == (y['latestCopy'], y['latestMark']) for x, y in pairs),
        crossCaptureEpochEquivalenceProved=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append', required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    arguments = parser.parse_args()
    require(1 <= len(arguments.capture) <= 2, 'Provide one capture or a primary/repeat pair')
    audit = json.loads(arguments.audit.read_text())
    validate_audit(audit)
    captures = [analyze_capture(path, audit) for path in arguments.capture]
    output = dict(selectorAudit=str(arguments.audit.resolve()),
                  selectorAuditSha256=sha(arguments.audit.read_bytes()), captures=captures)
    if len(captures) == 2:
        output['crossCapture'] = compare_captures(*captures)
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    arguments.out.write_text(json.dumps(output, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(output=str(arguments.out.resolve()),
                         captures=[{key: item[key] for key in (
                             'scopeSourceCommands', 'multilayerOccurrences', 'gpuBytes',
                             'uniqueCandidateCount', 'outOfRangeSelections', 'zeroIndexSelections',
                             'cpuT5CandidateOccurrences', 'capturedOriginalMultilayerSlots')}
                                   for item in captures],
                         crossCapture=output.get('crossCapture')), indent=2))


if __name__ == '__main__':
    main()
