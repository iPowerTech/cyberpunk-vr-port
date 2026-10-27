"""Check source/execution/terminal generation bindings, not native ownership."""
import struct


def validate(stats, ticket, rows_raw, source):
    for values, size in ((stats, 16), (ticket, 4)):
        if len(values) != size or any(type(v) is not int or not 0 <= v < 2**64 for v in values):
            raise ValueError('Invalid native generation publication')
    if stats[9] or stats[10] or stats[13] or stats[14] or stats[15]:
        raise ValueError('Native generation observation failed or acquired unexpected ownership')
    if not 0 < stats[1] <= stats[0] or stats[11] > 256 or stats[12] > 4096:
        raise ValueError('Root admission coverage/bounds differ')
    if ticket != [source['consumerId'], source['epoch'], source['pairGeneration'], 7]:
        raise ValueError('Source/execution/terminal binding has a different consumer identity')
    if len(rows_raw) % 96 or len(rows_raw) > 128 * 96:
        raise ValueError('Native generation row ABI/bounds differ')
    expected = {int(owner['header'], 16): owner for owner in source['owners'] if any(owner['definitions'])}
    seen_headers, seen_ids, generations = set(), set(), []
    for row in struct.iter_unpack('<12Q', rows_raw):
        generation_id, header, born, closed, highwater, bound, complete, pending, unsupported, flags, definitions, adopt = row
        owner = expected.get(header)
        if (not generation_id or generation_id in seen_ids or header in seen_headers or owner is None
                or born != owner['epoch'] or not bound or not complete or complete + pending != bound
                or unsupported or adopt or flags & ~1 or definitions != sum(bool(v) for v in owner['definitions'])):
            raise ValueError('Native definition generation has no exact completed owner binding')
        if flags & 1:
            if closed <= born or highwater < ticket[0]:
                raise ValueError('Source closure predates its admitted consumer')
        elif closed or highwater:
            raise ValueError('Open generation contains a source closure')
        seen_headers.add(header)
        seen_ids.add(generation_id)
        generations.append(dict(id=generation_id, header=hex(header), bornEpoch=born,
                                closedEpoch=closed, boundConsumers=bound, completedConsumers=complete,
                                pendingConsumers=pending, definitionCount=definitions))
    if seen_headers != set(expected):
        raise ValueError('A native definition owner is missing its generation binding')
    return dict(consumerId=ticket[0], rootAdmissionProved=True, executionBindingsProved=True,
                terminalCompletionProved=True, generations=generations,
                allPriorConsumersAccounted=False, definitionOwnershipProved=False,
                nativeExclusiveProved=False, executionRedirected=False)
