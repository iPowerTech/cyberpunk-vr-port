"""Bounded scene-child operand observations, never a reordering permission."""
import struct
from collections import Counter, defaultdict

PREFIX = 'CyberpunkVR_SceneAccess'
U32, U64 = 2**32 - 1, 2**64 - 1
FIELDS = ('schema', 'armed', 'finished', 'epoch', 'recordings', 'resources', 'events', 'children', 'selected',
          'unknownShaderOperands', 'unattributedEvents', 'missingMetadata', 'resets', 'overflow', 'failure', 'permission')
BLOCKS = (('recordings', 'Recordings', '<16Q', 256), ('resources', 'Resources', '<16Q', 4096),
          ('events', 'Events', '<4Q12I', 65536), ('children', 'Children', '<13Q8I', 4096))
KINDS = {1: 'read', 2: 'write', 3: 'transition', 4: 'alias', 5: 'uavBarrier', 6: 'shader', 7: 'unknown', 8: 'targets'}
CONTROLS = (PREFIX+'Enable', 'CyberpunkVR_SceneChildEnable')


def validate_options(enabled, *, eyes, retained_geometry, pass_targets):
    if enabled and (eyes != 'both' or retained_geometry != 'reference' or not pass_targets):
        raise ValueError('--scene-access requires --scene-split-eyes both, retained reference geometry and pass targets')


def restore_controls(read, write):
    result = dict(values={}, errors={}, restored=False)
    for name in CONTROLS:
        try:
            write(name, 0)
            result['values'][name] = read(name)
            if type(result['values'][name]) is not int or result['values'][name] != 0:
                raise ValueError('Scene access control was not restored')
        except Exception as error:result['errors'][name] = str(error)
    result['restored'] = not result['errors'] and result['values'] == dict.fromkeys(CONTROLS, 0)
    return result


def arm_controls(read, write):
    initial = {name: read(name) for name in CONTROLS}
    if any(type(value) is not int or value != 0 for value in initial.values()):
        raise ValueError('Scene access controls were not initially idle')
    try:
        write(CONTROLS[0], 1)
        armed = read(CONTROLS[0])
        if type(armed) is not int or armed != 1:raise ValueError('Scene access control did not arm')
    except Exception:
        restore_controls(read, write)
        raise
    return initial


def validate_capture(capture, *, requested):
    if type(requested) is not bool or capture.get('sceneAccessRequested', False) is not requested:
        raise ValueError('Scene access capture changed its requested mode')
    if not requested:return None
    cleanup = capture.get('sceneAccessCleanup', {})
    if (capture.get('passed') is not True or capture.get('sceneSplitEyes') != 'both'
            or cleanup.get('restored') is not True or cleanup.get('errors') != {}
            or cleanup.get('values') != dict.fromkeys(CONTROLS, 0)
            or any(type(cleanup['values'][name]) is not int for name in CONTROLS)):
        raise ValueError('Scene access capture or control restoration is incomplete')
    execution = capture.get('ownedExecution', {})
    result = validate(capture.get('sceneAccess', {}), execution.get('sceneSplit', {}).get('publication'),
                      owned_stats=execution.get('stats'))
    if capture.get('sceneAccess') != result:
        raise ValueError('Scene access capture lost its independently validated observation')
    return result


def _words(value, count, narrow=0):
    if (not isinstance(value, (list, tuple)) or len(value) != count or any(
            type(word) is not int or not 0 <= word <= (U32 if narrow and index >= narrow else U64)
            for index, word in enumerate(value))):
        raise ValueError('Scene access ABI requires bounded integer words')
    return list(value)


def decode(stats, recordings, resources, events, children):
    stats = _words(stats, 16)
    if (stats[0] != 1 or stats[1] not in (0, 1) or stats[2] not in (0, 1)
            or stats[8] > stats[4] or stats[11] > stats[5]):
        raise ValueError('Scene access schema or counters differ')
    result = dict(stats=stats, summary=dict(zip(FIELDS, stats)), verified=False,
                  accessCoverageComplete=False, nativeExecutionPermission=False, reorderPermission=False,
                  performanceGainProved=False)
    for index, ((name, _, layout, limit), rows) in enumerate(zip(BLOCKS, (recordings, resources, events, children)), 4):
        if not isinstance(rows, (list, tuple)) or len(rows) != stats[index] or len(rows) > limit:
            raise ValueError('Scene access rows exceed the declared bounded ABI')
        count, narrow = (16, 0) if index < 6 else (16, 4) if index == 6 else (21, 13)
        result[name] = [_words(row, count, narrow) for row in rows]
    return result


def read_report(read_export, read_memory, attempts=4):
    if type(attempts) is not int or not 0 < attempts <= 16:
        raise ValueError('Scene access read attempts exceed bounds')
    def header():
        sequence, = struct.unpack('<I', read_export(PREFIX+'Seq', 4))
        stats = struct.unpack('<16Q', read_export(PREFIX+'Stats', 128))
        pointers = tuple(struct.unpack('<Q', read_export(PREFIX+suffix, 8))[0] for _, suffix, _, _ in BLOCKS)
        return sequence, stats, pointers
    for _ in range(attempts):
        before = header()
        sequence, stats, pointers = before
        if sequence & 1:
            continue
        blocks = []
        for index, ((_, _, layout, limit), pointer) in enumerate(zip(BLOCKS, pointers), 4):
            count, stride = stats[index], struct.calcsize(layout)
            if count > limit or (count and (not pointer or pointer & 7 or pointer > U64-count*stride)):
                raise ValueError('Scene access pointer/count exceeds bounded storage')
            raw = read_memory(pointer, count*stride) if count else b''
            if not isinstance(raw, bytes) or len(raw) != count*stride:
                raise ValueError('Scene access row payload was truncated')
            blocks.append(list(struct.iter_unpack(layout, raw)))
        if before == header():
            return decode(stats, *blocks)
    raise ValueError('Scene access publication changed during bounded capture')


def _intervals(events):
    if any(event[2] == U64 for event in events):
        return None
    result = []
    for begin, end in sorted((event[1], event[1]+event[2]) for event in events if event[2]):
        if result and begin <= result[-1][1]:
            result[-1][1] = max(result[-1][1], end)
        else:
            result.append([begin, end])
    return result


def _overlap(first, second):
    if first is None or second is None:
        return 'unknown-range'
    left = right = 0
    while left < len(first) and right < len(second):
        if first[left][0] < second[right][1] and second[right][0] < first[left][1]:
            return 'observed-byte-overlap'
        if first[left][1] <= second[right][1]:left += 1
        else:right += 1
    return None


def validate(report, publication, *, owned_stats):
    if not isinstance(report, dict):raise ValueError('Scene access report is missing')
    result = decode(report.get('stats'), *(report.get(name) for name, _, _, _ in BLOCKS))
    stats = result['stats']
    owned = _words(owned_stats, 24)
    identity = [owned[1], owned[2], owned[3], owned[6], owned[4], owned[5]]
    if (stats[1:3] != [0, 1] or not stats[3] or stats[8] != 2 or any(stats[13:])
            or owned[0] != 4 or not all(identity) or owned[4] == owned[5]):
        raise ValueError('Scene access observation did not finish without overflow/failure')
    if (not isinstance(publication, dict) or publication.get('verified') is not True
            or publication.get('nativePublicationProved') is not True or publication.get('nativeDrawSuppression') != 0
            or not isinstance(publication.get('lists'), list) or len(publication['lists']) != 4):
        raise ValueError('Scene access requires the four-list native publication')
    lists = publication['lists']
    for row in lists:
        if (not isinstance(row, dict) or any(type(row.get(key)) is not int or not 0 < row[key] <= U64
                for key in ('list', 'nativeGeneration', 'token', 'nativeContext', 'workContext'))
                or type(row.get('nodeHandle')) is not int or not 0 <= row['nodeHandle'] <= U32
                or not isinstance(row.get('object'), str)):
            raise ValueError('Scene access native publication identity is missing')
        try:
            if not 0 < int(row['object'], 16) <= U64:raise ValueError()
        except ValueError as error:
            raise ValueError('Scene access native publication root pointer is invalid') from error
    if ([row.get('section') for row in lists] != ['prefixPeer', 'tailPeer', 'prefixMain', 'tailMain']
            or [row.get('viewIndex') for row in lists] != [1, 1, 0, 0]
            or [row.get('position') for row in lists] != [1, 2, 3, 4]
            or len({row.get('list') for row in lists}) != 4):
        raise ValueError('Scene access publication order or eye mapping differs')
    expected = {row['list']: row for row in (lists[1], lists[2])}
    resources, recordings, events, children = (result[key] for key in ('resources', 'recordings', 'events', 'children'))
    if len({row[0] for row in resources}) != len(resources):
        raise ValueError('Scene access duplicates a held resource identity')
    cookies = [row[1] for row in resources if row[1]]
    if len(set(cookies)) != len(cookies):
        raise ValueError('Scene access duplicates a resource cookie')
    for row in resources:
        if (not row[0] or row[0] & 7 or row[6] not in (1, 2, 3, 4) or not all(row[7:11])
                or row[13] not in (0, 1) or row[14] not in (0, 1) or bool(row[1]) != bool(row[13]) or row[15]
                or any(word > U32 for word in row[8:13])):
            raise ValueError('Scene access resource description or cookie differs')
        if row[6] == 1 and (row[8:11] != [1, 1, 1] or row[11]):
            raise ValueError('Scene access buffer description differs')
        if (not row[14] and any(row[2:6])) or (row[14] and row[2] not in (1, 2)):
            raise ValueError('Scene access creation metadata differs')
        if row[2] == 1 and any(row[3:6]):
            raise ValueError('Committed scene resource unexpectedly has placement metadata')
        if row[2] == 2 and (not row[3] or row[3] & 7 or not row[5] or row[4] > U64-row[5]):
            raise ValueError('Placed scene resource extent is invalid')
    if stats[11] != sum(not row[13] or not row[14] for row in resources):
        raise ValueError('Scene access missing-metadata count differs')

    selected, covered, generations, sequences, child_owners = {}, 0, set(), set(), {}
    child_events = defaultdict(list)
    for index, row in enumerate(recordings):
        if (not row[0] or row[0] & 7 or not row[1] or row[1] in generations or row[2] not in (0, 1)
                or row[3] not in (0, 1) or row[4] > U32 or (row[3] and row[4] & 0x80000000)
                or (not row[3] and row[4] != 0x8000000A) or row[14] != covered or row[15] > len(events)-covered):
            raise ValueError('Scene access recording boundaries or Close result differ')
        generations.add(row[1]);covered += row[15]
        if row[6]:
            target = expected.get(row[0])
            if (target is None or row[0] in selected or row[2:5] != [1, 1, 0] or not all(row[5:9])
                    or row[5:8] != [target.get('nativeGeneration'), target.get('token'), target.get('nativeContext')]
                    or row[9:11] != [target.get('nodeHandle'), target.get('viewIndex')] or row[11:14] != owned[1:4]):
                raise ValueError('Scene access selected recording differs from its native publication')
            selected[row[0]] = index
        elif any(row[5:14]):
            raise ValueError('Scene access unselected recording has a partial native identity')
        prior = 0
        for event in events[row[14]:covered]:
            if (event[4] != index or not event[0] or event[0] <= prior or event[0] in sequences or event[6] not in KINDS
                    or event[14] not in (0, 1) or event[15] or event[3]):
                raise ValueError('Scene access event sequence, owner or ABI differs')
            sequences.add(event[0]);prior = event[0]
            for resource in event[7:9]:
                if resource != U32 and resource >= len(resources):
                    raise ValueError('Scene access event references an absent resource')
            if event[5] != U32:
                if event[5] >= len(children) or event[14] or not row[6]:
                    raise ValueError('Scene access child attribution differs')
                previous = child_owners.setdefault(event[5], index)
                if previous != index:raise ValueError('Scene access child spans native recordings')
                child_events[event[5]].append(event)
            if event[6] in (1, 2, 3, 8) and event[7] == U32:
                raise ValueError('Scene access operand has no resource')
            if event[6] != 4 and event[8] != U32:
                raise ValueError('Scene access event has an unexpected second resource')
            if event[2] != U64 and event[1] > U64-event[2]:
                raise ValueError('Scene access operand byte extent overflows')
            if event[6] in (1, 2):
                resource = resources[event[7]]
                if event[2] != U64 and (event[1] > U64-event[2] or
                        (resource[6] == 1 and (event[1] > resource[7] or event[2] > resource[7]-event[1]))):
                    raise ValueError('Scene access operand byte range exceeds its buffer')
            elif event[6] != 7 and (event[1] or event[2] != U64):
                raise ValueError('Scene access non-operand contains a byte extent')
            if event[6] == 6 and (event[7] != U32 or event[12] not in (0, 1)):
                raise ValueError('Scene access shader marker differs')
            if event[6] == 8 and event[13] > 8:
                raise ValueError('Scene access target slot exceeds the native bindings')
    if covered != len(events) or set(selected) != set(expected) or set(child_owners) != set(range(len(children))):
        raise ValueError('Scene access recording/child coverage differs')
    if not set(selected.values()) <= set(child_owners.values()):
        raise ValueError('Scene access selected recording has no attributed child')
    if len({recordings[index][8] for index in selected.values()}) != 1:
        raise ValueError('Scene access selected recordings use different native registries')
    if (stats[9] < sum(event[6] == 6 for event in events)
            or stats[10] < sum(event[5] == U32 and not event[14] for event in events)
            or stats[12] < sum(row[2] for row in recordings)):
        raise ValueError('Scene access cumulative counters omit retained events')

    summaries = []
    for index, child in enumerate(children):
        record = recordings[child_owners[index]];target = expected[record[0]]
        section = 2 if target['viewIndex'] else 1
        if (child[:6] != identity or not all(child[6:12]) or child[6] != int(target['object'], 16)
                or child[10] != target['workContext'] or child[12] >= child[11]
                or child[13:16] != [target['nodeHandle'], target['viewIndex'], section]
                or child[16] not in (range(6, 11) if section == 2 else range(6)) or not child[17]
                or child[18] not in (0, 1) or any(child[19:])):
            raise ValueError('Scene access child differs from its graph/root/section identity')
        group = child_events[index]
        counts = Counter(KINDS[event[6]] for event in group)
        summaries.append(dict(child=index, recording=child_owners[index], section=target['section'], slot=child[16],
                              object=hex(child[7]), work=hex(child[9]), invocation=child[11], phase=child[18],
                              events=len(group), kinds=dict(counts), tags=dict(Counter(str(event[13]) for event in group)),
                              observedReadOperands=counts['read'], observedWriteOperands=counts['write'],
                              observedCopyReadOperands=sum(event[6] == 1 and event[13] == 4 for event in group),
                              observedCopyWriteOperands=sum(event[6] == 2 and event[13] == 4 for event in group),
                              observedBarriers=counts['transition']+counts['alias']+counts['uavBarrier'], observedShaders=counts['shader']))
    if len({child[11] for child in children}) != len(children):
        raise ValueError('Scene access child invocation is duplicated')

    access = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    for event in events:
        if event[6] in (1, 2) and event[4] in selected.values() and resources[event[7]][6] == 1:
            access[event[7]][event[4]][event[6]].append(event)
    overlaps = []
    first, second = (selected[lists[index]['list']] for index in (1, 2))
    for resource, recordings_used in access.items():
        if first not in recordings_used or second not in recordings_used:continue
        hazards = []
        for a, b in ((1, 2), (2, 1), (2, 2)):
            left, right = recordings_used[first].get(a, []), recordings_used[second].get(b, [])
            if not left or not right:continue
            overlap = _overlap(_intervals(left), _intervals(right))
            if overlap:hazards.append(dict(peer=KINDS[a], main=KINDS[b], extent=overlap))
        if hazards:overlaps.append(dict(resource=resource, pointer=hex(resources[resource][0]), hazards=hazards))
    result.update(verified=True, selectedRecordings=[first, second], childSummary=summaries,
                  sameResourcePotentialOverlaps=overlaps,
                  limitation='Observed operands only. Shader operands, CPU writes and cross-resource physical aliases are not complete; '
                             'UINT64_MAX byte extents are unknown, not exact whole-resource accesses. No reordering or execution permission.')
    return result
