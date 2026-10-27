"""Decode native retirement receipts, never infer native calls from counters.

The live caller owns stable seqlock sampling and PID/build/frame freshness.
Native event labels survive metadata recycling; they are not live pointers.
"""

from collections.abc import Mapping, Sequence

UINT64_MAX = (1 << 64) - 1
UINT32_MAX = (1 << 32) - 1
EVENT_CAPACITY = 32
CACHE_CAPACITY = 64
HISTORY_CAPACITY = 512
COMPLETION_FLAGS = 0x1ff
COMMON_RVA = 0x3438B00
LIVE_VPTR_RVA = 0x2ff9bf8
MAX_ALLOCATIONS = 1 + 4096 + 8192 + 4096 + 4096

ALLOCATOR_FIELDS = ('state', 'incarnation', 'resetEpoch', 'operationsInFlight', 'servicesActive',
                    'guardsInstalled', 'common', 'objectRaw', 'controlRaw', 'stringRaw', 'poolVptr',
                    'smallBegin', 'smallEnd', 'middleBegin', 'middleEnd', 'savedAllocationCount',
                    'smallCount', 'middleCount', 'unsupportedCount', 'treeNodes', 'allExactExtents',
                    'bindingFailure', 'closeResetEvents', 'lastEntryRva')
TOTAL_FIELDS = ('reserved', 'extraAdmitted', 'extraCompleted', 'unescapedCanceled', 'nativeIntents',
                'detached', 'savedDestroyBegun', 'savedDestroyReturned', 'metadataRecycled',
                'unusedAbandoned', 'quarantined', 'allocatorBound', 'allocatorClosed', 'allocatorReset',
                'cacheCalls', 'distinctCacheTuples')
EVENT_FIELDS = ('sequence', 'kind', 'header', 'generation', 'born', 'data', 'capacity', 'count',
                'consumerId', 'epoch', 'pairGeneration', 'allocatorIncarnation', 'resetEpoch',
                'currentHeader', 'currentStorage', 'currentIntentEpoch', 'D0', 'E', 'D1',
                'completedReaders', 'pendingReaders', 'resultFailure', 'callerRva', 'classifiedAllocations')
CACHE_FIELDS = ('calls', 'hits', 'misses', 'distinctTuples', 'overflow', 'lastSeq', 'targetCacheBase',
                'lastKey', 'lastViewCount', 'lastSlot', 'lastUse', 'lastRenderer', 'lastConsumer',
                'lastEpoch', 'lastPair', 'lastCaller')
CACHE_ROW_FIELDS = ('cacheBase', 'key', 'viewCount', 'firstSeq', 'lastSeq', 'hits', 'misses', 'renderer')


def _uint(value, name, maximum=UINT64_MAX):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError(f'{name} is not an exact bounded unsigned integer')
    return value


def _words(values, count, name):
    if not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray)) or len(values) != count:
        raise ValueError(f'{name} ABI/count mismatch')
    return tuple(_uint(value, name) for value in values)


def _rows(values, count, width, name):
    if not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray)) or len(values) != count:
        raise ValueError(f'{name} row count mismatch')
    return [_words(row, width, name) for row in values]


def _pointer(value, name):
    if not value or value & 7:
        raise ValueError(f'{name} is absent or unaligned')


def _event(row):
    row = _words(row, 24, 'retirement event')
    event = dict(zip(EVENT_FIELDS, row))
    if not event['sequence'] or not 1 <= event['kind'] <= 14:
        raise ValueError('Unknown or missing retirement event sequence/kind')
    if event['capacity'] > UINT32_MAX or event['count'] > event['capacity']:
        raise ValueError('Retirement event native header bounds mismatch')
    flags = event['resultFailure'] >> 32
    if flags & ~COMPLETION_FLAGS:
        raise ValueError('Retirement event has unknown completion proof bits')
    event.update(completionFlags=flags, failureCode=event['resultFailure'] & UINT32_MAX, raw=list(row))
    return event


def _canonical_event(value):
    if not isinstance(value, Mapping) or 'raw' not in value:
        raise ValueError('Retirement history lacks its exact native event words')
    event = _event(value['raw'])
    if any(value.get(key) != item for key, item in event.items()):
        raise ValueError('Retirement event labels differ from its native words')
    return event


def merge_event_history(history, events):
    """Merge overlapping observations without losing or inventing event links."""
    for values in (history, events):
        if not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray)) or len(values) > HISTORY_CAPACITY:
            raise ValueError('Retirement event history exceeds its bounded capacity')
    merged = {}
    for values in (history, events):
        previous = None
        for value in values:
            event = _canonical_event(value)
            seq = event['sequence']
            if previous is not None and seq != previous + 1:
                raise ValueError('Retirement observation has a sequence gap or duplicate')
            previous = seq
            if seq in merged and merged[seq] != event:
                raise ValueError('Conflicting retirement rows for the same event sequence')
            merged[seq] = event
    if len(merged) > HISTORY_CAPACITY:
        raise ValueError('Retirement history would discard an older receipt')
    ordered = [merged[seq] for seq in sorted(merged)]
    if ordered and ordered[-1]['sequence'] - ordered[0]['sequence'] + 1 != len(ordered):
        raise ValueError('Retirement event history missed native events')
    return ordered


def decode(raw):
    """Decode one reader-owned coherent ABI snapshot and its physical rings."""
    if not isinstance(raw, Mapping):
        raise ValueError('Missing retirement snapshot envelope')
    sequence = _uint(raw.get('sequence'), 'publication sequence', UINT32_MAX)
    if sequence & 1:
        raise ValueError('Retirement snapshot is being published')
    pid = _uint(raw.get('pid'), 'external PID')
    if not pid or not isinstance(raw.get('dll'), str) or not raw['dll']:
        raise ValueError('Missing external process/build labels')
    allocator = dict(zip(ALLOCATOR_FIELDS, _words(raw.get('Allocator'), 24, 'Allocator')))
    totals = dict(zip(TOTAL_FIELDS, _words(raw.get('RetirementTotals'), 16, 'RetirementTotals')))
    cache = dict(zip(CACHE_FIELDS, _words(raw.get('Cache'), 16, 'Cache')))
    if allocator['state'] > 3 or allocator['guardsInstalled'] not in (0, 1) or allocator['allExactExtents'] not in (0, 1):
        raise ValueError('Unknown allocator state/flag')
    if allocator['savedAllocationCount'] > MAX_ALLOCATIONS or allocator['treeNodes'] > 4096:
        raise ValueError('Unbounded allocator classification inventory')
    if allocator['state'] == 1:
        if not allocator['incarnation'] or allocator['guardsInstalled'] != 1:
            raise ValueError('Bound allocator lacks tracked incarnation or installed guards')
        common = allocator['common']
        _pointer(common, 'common allocator')
        if common <= COMMON_RVA or any(allocator[name] & ~7 != common for name in ('objectRaw', 'controlRaw', 'stringRaw')):
            raise ValueError('Allocator category globals do not bind the common allocator')
        if allocator['poolVptr'] != common - COMMON_RVA + LIVE_VPTR_RVA:
            raise ValueError('Allocator component has no audited live vptr identity')
        small = (allocator['smallBegin'], allocator['smallEnd'])
        middle = (allocator['middleBegin'], allocator['middleEnd'])
        if not 0 < small[0] < small[1] or not 0 < middle[0] < middle[1] or not (small[1] <= middle[0] or middle[1] <= small[0]):
            raise ValueError('Allocator backing reservations are incomplete or overlapping')
        if sum(allocator[name] for name in ('smallCount', 'middleCount', 'unsupportedCount')) != allocator['savedAllocationCount']:
            raise ValueError('Allocator classification counts do not match its inventory')

    ring = raw.get('events')
    if not isinstance(ring, Mapping):
        raise ValueError('Missing retirement event ring control')
    event_sequence = _uint(ring.get('sequence'), 'event sequence')
    event_count = _uint(ring.get('count'), 'event count', EVENT_CAPACITY)
    if event_count != min(event_sequence, EVENT_CAPACITY):
        raise ValueError('Retirement event count is stale or overflowed')
    physical = _rows(ring.get('rows'), EVENT_CAPACITY, 24, 'RetirementEvents')
    events = []
    expected_sequences = set(range(event_sequence - event_count + 1, event_sequence + 1))
    for index, row in enumerate(physical):
        if not row[0]:
            if any(row):
                raise ValueError('Unused event ring slot contains stale data')
            continue
        event = _event(row)
        if event['sequence'] not in expected_sequences or (event['sequence'] - 1) % EVENT_CAPACITY != index:
            raise ValueError('Retirement event ring has a stale or misplaced sequence')
        events.append(event)
    events.sort(key=lambda event: event['sequence'])
    if len(events) != event_count:
        raise ValueError('Retirement event ring lost a valid row')
    merge_event_history([], events)

    if cache['distinctTuples'] > CACHE_CAPACITY or cache['overflow'] not in (0, 1):
        raise ValueError('Unbounded cache key inventory')
    if cache['hits'] + cache['misses'] != cache['calls'] or cache['lastSeq'] != cache['calls']:
        raise ValueError('Cache call/sequence counters are inconsistent')
    if cache['calls'] != totals['cacheCalls'] or cache['distinctTuples'] != totals['distinctCacheTuples']:
        raise ValueError('Cache counters disagree with retirement totals')
    cache_rows = []
    tuples = set()
    for row in _rows(raw.get('cacheRows'), CACHE_CAPACITY, 8, 'CacheRows'):
        if not row[0]:
            if any(row):
                raise ValueError('Unused cache row contains stale data')
            continue
        entry = dict(zip(CACHE_ROW_FIELDS, row))
        if entry['cacheBase'] != cache['targetCacheBase'] or not entry['viewCount'] or not 0 < entry['firstSeq'] <= entry['lastSeq'] <= cache['lastSeq']:
            raise ValueError('Cache row is foreign to its sealed target or sequence window')
        _pointer(entry['cacheBase'], 'sealed cache base')
        _pointer(entry['renderer'], 'observed cache renderer')
        key = (entry['key'], entry['viewCount'])
        if key in tuples:
            raise ValueError('Duplicate native cache tuple was counted twice')
        tuples.add(key)
        cache_rows.append(entry)
    if len(cache_rows) != cache['distinctTuples']:
        raise ValueError('Cache distinct count is not backed by exact native rows')
    if not cache['overflow'] and (sum(row['hits'] for row in cache_rows) != cache['hits'] or sum(row['misses'] for row in cache_rows) != cache['misses']):
        raise ValueError('Native cache rows do not account for observed calls')
    if cache['calls'] and (not cache['targetCacheBase'] or cache['lastSlot'] >= 4 or not cache['lastViewCount']):
        raise ValueError('Observed cache call has no sealed target/slot/view identity')
    return dict(sequence=sequence, pid=pid, dll=raw['dll'], allocator=allocator, totals=totals,
                events=events, eventSequence=event_sequence, eventCount=event_count,
                cache=cache, cacheRows=cache_rows, externalFreshnessRequired=True)


def _owner(event):
    return tuple(event[name] for name in ('header', 'generation', 'born'))


def _checked_report(report):
    try:
        physical = [[0] * 24 for _ in range(EVENT_CAPACITY)]
        events = [_canonical_event(event) for event in report['events']]
        for event in events:
            physical[(event['sequence'] - 1) % EVENT_CAPACITY] = event['raw']
        rows = [[report_row[name] for name in CACHE_ROW_FIELDS] for report_row in report['cacheRows']]
        if len(rows) > CACHE_CAPACITY:
            raise ValueError('Unbounded decoded cache rows')
        return decode(dict(sequence=report['sequence'], pid=report['pid'], dll=report['dll'],
                           Allocator=[report['allocator'][name] for name in ALLOCATOR_FIELDS],
                           RetirementTotals=[report['totals'][name] for name in TOTAL_FIELDS],
                           Cache=[report['cache'][name] for name in CACHE_FIELDS],
                           events=dict(sequence=report['eventSequence'], count=report['eventCount'], rows=physical),
                           cacheRows=rows + [[0] * 8 for _ in range(CACHE_CAPACITY - len(rows))]))
    except (KeyError, TypeError, IndexError) as error:
        raise ValueError('Incomplete decoded retirement report') from error


def _success(event, complete=False):
    if event['failureCode'] or event['pendingReaders']:
        raise ValueError('Retirement target has a failed result or pending readers')
    if complete:
        if event['completionFlags'] != COMPLETION_FLAGS or not event['classifiedAllocations'] or not event['completedReaders']:
            raise ValueError('Retirement target has no full completion/classification receipt')
        for group in ('D0', 'E', 'D1'):
            _pointer(event[group], f'retirement {group}')
        if event['D0'] == event['E']:
            raise ValueError('Retirement declaration and execution receipts alias')


def validate_retirement(report, consumerId, epoch, pair, require_recycled=True):
    """Prove a specific admitted owner generation's observed native retirement."""
    for name, value in (('consumer', consumerId), ('epoch', epoch), ('pair', pair)):
        if not _uint(value, name):
            raise ValueError('Missing expected retirement consumer identity')
    if type(require_recycled) is not bool or not isinstance(report, Mapping):
        raise ValueError('Invalid retirement validation arguments')
    checked = _checked_report(report)
    history = merge_event_history([], report.get('history', checked['events']))
    if history and history[-1]['sequence'] != checked['eventSequence']:
        raise ValueError('Retirement history is stale relative to its current event ring')
    if merge_event_history(history, checked['events']) != history:
        raise ValueError('Retirement history omits the current event-ring tail')
    allocator = checked['allocator']
    if (allocator['state'] != 1 or allocator['guardsInstalled'] != 1 or allocator['operationsInFlight']
            or allocator['servicesActive'] or allocator['bindingFailure'] or allocator['unsupportedCount']):
        raise ValueError('Allocator is not in an unchanged guarded structural profile')
    cache = checked['cache']
    if cache['overflow'] or not cache['targetCacheBase'] or len(checked['cacheRows']) <= 4:
        raise ValueError('Sealed target cache has no observed pressure beyond its four native slots')
    requested = (consumerId, epoch, pair)
    admission = _words(report.get('admission'), 6, 'OwnedExecutionAdmission')
    if admission[:3] != requested or admission[4] != cache['targetCacheBase']:
        raise ValueError('Owned admission does not bind the requested consumer to the sealed cache')
    for name, pointer in (('admitted source graph', admission[3]), ('admitted source cache', admission[4]),
                          ('admitted renderer', admission[5])):
        _pointer(pointer, name)
    offset = admission[3] - admission[4] - 0x10
    if offset < 0 or offset % 0x90 or offset // 0x90 >= 4:
        raise ValueError('Admitted source graph is not one of the sealed cache slots')
    admits = [event for event in history if event['kind'] == 2 and tuple(event[name] for name in ('consumerId', 'epoch', 'pairGeneration')) == requested]
    if not admits:
        raise ValueError('Requested consumer has no actual owner-generation admission event')
    for admit in admits:
        identity = _owner(admit)
        if not identity[1] or not identity[2]:
            raise ValueError('Admitted native owner lacks generation/birth identity')
        _pointer(identity[0], 'retained native owner header')
        owner_events = [event for event in history if _owner(event) == identity]
        reserves = [event for event in owner_events if event['kind'] == 1 and event['sequence'] < admit['sequence']]
        completes = [event for event in owner_events if event['kind'] == 3 and event['sequence'] > admit['sequence']
                     and tuple(event[name] for name in ('consumerId', 'epoch', 'pairGeneration')) == requested]
        if len(reserves) != 1 or len(completes) != 1:
            continue
        reserve, complete = reserves[0], completes[0]
        _pointer(reserve['data'], 'saved native vector allocation')
        if not reserve['count'] or not reserve['capacity'] or reserve['failureCode']:
            raise ValueError('Retirement reserve has no successful nonempty saved native header')
        _success(complete, True)
        terminal = {}
        for kind in (5, 6, 7, 8, 9):
            rows = [event for event in owner_events if event['kind'] == kind]
            if len(rows) != 1:
                break
            terminal[kind] = rows[0]
        if not all(kind in terminal for kind in (5, 6, 7, 8)) or (require_recycled and 9 not in terminal):
            continue
        if any(event['kind'] in (4, 10, 11) for event in owner_events if event['sequence'] > admit['sequence']):
            raise ValueError('Target retirement was canceled, abandoned, or quarantined')
        ordered = [reserve['sequence'], admit['sequence'], terminal[5]['sequence'], terminal[6]['sequence'],
                   terminal[7]['sequence'], terminal[8]['sequence']]
        if ordered != sorted(set(ordered)) or complete['sequence'] >= terminal[7]['sequence']:
            raise ValueError('Target native intent/detach/destroy receipts are out of order')
        if 9 in terminal and terminal[9]['sequence'] <= terminal[8]['sequence']:
            raise ValueError('Metadata recycled before native destructor returned')
        groups = tuple(complete[name] for name in ('D0', 'E', 'D1'))
        saved = tuple(reserve[name] for name in ('data', 'capacity', 'count'))
        for event in [reserve, admit, complete] + list(terminal.values()):
            if tuple(event[name] for name in ('data', 'capacity', 'count')) != saved:
                raise ValueError('Retirement events do not refer to the same saved native header')
            if event['allocatorIncarnation'] != allocator['incarnation'] or event['resetEpoch'] != allocator['resetEpoch']:
                raise ValueError('Retirement target has a stale allocator incarnation/reset epoch')
            if event['failureCode']:
                raise ValueError('Retirement target recorded an unsuccessful native operation')
        for kind in (7, 8, 9):
            if kind not in terminal:
                continue
            event = terminal[kind]
            _success(event, True)
            if tuple(event[name] for name in ('consumerId', 'epoch', 'pairGeneration')) != requested or tuple(event[name] for name in ('D0', 'E', 'D1')) != groups:
                raise ValueError('Retirement native service lost the actual completed consumer/groups')
            for name in ('currentHeader', 'currentStorage', 'currentIntentEpoch', 'callerRva'):
                if not event[name]:
                    raise ValueError('Retirement native service has no actual current intent scope labels')
        scope = tuple(terminal[7][name] for name in ('currentHeader', 'currentStorage', 'currentIntentEpoch', 'callerRva'))
        if any(tuple(terminal[kind][name] for name in ('currentHeader', 'currentStorage', 'currentIntentEpoch', 'callerRva')) != scope for kind in (8, 9) if kind in terminal):
            raise ValueError('Native destructor return/recycle belongs to another service interval')
        return dict(consumerId=consumerId, epoch=epoch, pairGeneration=pair,
                    header=hex(identity[0]), generation=identity[1], born=identity[2],
                    reserveConsumerId=reserve['consumerId'], cacheBase=hex(cache['targetCacheBase']),
                    sourceGraph=hex(admission[3]), renderer=hex(admission[5]), cacheOwnershipCorrelationProved=True,
                    observedDistinctCacheTuples=len(checked['cacheRows']),
                    eventSequences=dict(reserve=reserve['sequence'], admit=admit['sequence'], complete=complete['sequence'],
                                        intent=terminal[5]['sequence'], detach=terminal[6]['sequence'],
                                        destroyBegin=terminal[7]['sequence'], destroyReturn=terminal[8]['sequence'],
                                        recycle=terminal[9]['sequence'] if 9 in terminal else None),
                    groups={name: hex(complete[name]) for name in ('D0', 'E', 'D1')},
                    classifiedAllocations=terminal[8]['classifiedAllocations'], nativeDestroyReturned=True,
                    metadataRecycled=9 in terminal, allExactExtents=bool(allocator['allExactExtents']),
                    externalFreshnessRequired=True, ownedReportCorrelationRequired=True,
                    processFreshnessProved=False, frameFreshnessProved=False,
                    gpuLifetimeProved=False, performanceGainProved=False)
    raise ValueError('Specific admitted owner generation has no complete native retirement/recycling event chain')
