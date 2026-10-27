"""Bounded offline resource-order witnesses; never authorizes a native reorder."""
import argparse
from collections import Counter, defaultdict, deque
import hashlib
import json
from pathlib import Path
import struct

from graph_parent_report import fingerprint
from graph_plan import decode_graph

MAX_NODES, MAX_EDGES, MAX_ROWS, MAX_AUX, MAX_BOUNDARIES = 220, 65536, 4096, 64, 24
MAX_WITNESSES = 4096
EMPTY = 0x80000000
MARKER_KEY = 0xfd3dde00


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def uint(value, bits=64):
    return type(value) is int and 0 <= value < 1 << bits


def values(raw, size, maximum, format_):
    require(isinstance(raw, bytes) and len(raw) % size == 0 and len(raw) // size <= maximum, 'Invalid bounded binary ABI/count')
    return list(struct.iter_unpack(format_, raw))


def graph(nodes_raw, edges_raw, base):
    nodes = values(nodes_raw, 104, MAX_NODES, '<II48sQQ32s')
    edge_rows = values(edges_raw, 32, MAX_EDGES, '<II24s')
    require(nodes, 'Empty source/candidate graph')
    result = decode_graph(nodes_raw, edges_raw, base)
    node_map = {n['id']: n for n in result['nodes']}
    for index, node in enumerate(result['nodes']):
        require(node['id'] < 65536 and node['handle'] in (node['id'], node['id'] | 0x20000000) and
                node['ordinal'] == index, 'Invalid native node identity/ordinal')
    for phase in (0, 1):
        require({n['phaseOrder'][phase] for n in result['nodes']} == set(range(len(nodes))), 'Phase slots are not a unique bounded permutation')
    edge_map, seen = {}, set()
    for edge in result['edges']:
        identity = edge['id']
        require(identity < 262144 and edge['handle'] in (identity, identity | 0x40000000) and edge['ordinal'] == edge['order'] and
                edge['phase'] in (0, 1) and edge['source'] in node_map and edge['target'] in node_map and
                edge['source'] != edge['target'], 'Invalid native edge identity/endpoint/phase')
        for name in ('source', 'target'):
            require(edge[name+'Raw'] in (edge[name], edge[name] | 0x20000000), 'Invalid edge node handle flags')
        require(node_map[edge['source']]['phaseOrder'][edge['phase']] < node_map[edge['target']]['phaseOrder'][edge['phase']],
                'Native phase ranks disagree with dependencies')
        triple = edge['phase'], edge['source'], edge['target']
        require(triple not in seen, 'Duplicate dependency edge')
        seen.add(triple)
        edge_map[identity] = edge
    # Check each stored adjacency chain against the same exported edge set.
    for phase in (0, 1):
        for direction in (0, 1):
            visited = set()
            for node in result['nodes']:
                at = node['links'][phase + 2 * direction]
                chain = set()
                while at != EMPTY:
                    identity = at & 0xbfffffff
                    require(identity in edge_map and at in (identity, identity | 0x40000000) and identity not in chain,
                            'Malformed native adjacency chain')
                    edge = edge_map[identity]
                    require(edge['phase'] == phase and edge['source' if direction else 'target'] == node['id'] and
                            identity not in visited, 'Adjacency endpoint or ownership mismatch')
                    chain.add(identity)
                    visited.add(identity)
                    at = edge['next'][0 if direction else 1]
            require(visited == {e['id'] for e in result['edges'] if e['phase'] == phase}, 'Incomplete native adjacency')
    return result, nodes, edge_rows


def lifted(parsed):
    events = sorted(2*n['id']+phase for n in parsed['nodes'] for phase in (0, 1))
    edges = {(2*n['id'], 2*n['id']+1) for n in parsed['nodes']}
    edges.update((2*e['source']+e['phase'], 2*e['target']+e['phase']) for e in parsed['edges'])
    following = {event: [] for event in events}
    indegree = dict.fromkeys(events, 0)
    for a, b in sorted(edges):
        following[a].append(b)
        indegree[b] += 1
    pending = deque(n for n in events if not indegree[n])
    order = []
    while pending:
        at = pending.popleft()
        order.append(at)
        for child in following[at]:
            indegree[child] -= 1
            if not indegree[child]:
                pending.append(child)
    require(len(order) == len(events), 'Cyclic lifted event graph')
    indices = {event: index for index, event in enumerate(events)}
    reachable = dict.fromkeys(events, 0)
    for at in reversed(order):
        for child in following[at]:
            reachable[at] |= (1 << indices[child]) | reachable[child]
    return events, edges, following, reachable


def validate_provenance(provenance, raw, parsed, candidate):
    require(isinstance(provenance, dict), 'Missing capture provenance')
    for name in ('imageBase', 'sourceFingerprint', 'pid'):
        require(uint(provenance.get(name)) and provenance[name], 'Invalid capture provenance number: '+name)
    dll = provenance.get('dll')
    require(isinstance(dll, str) and len(dll) == 64 and all(c in '0123456789abcdef' for c in dll), 'Invalid capture DLL identity')
    actual = fingerprint(raw[0], raw[1], provenance['imageBase'])
    require(actual == provenance['sourceFingerprint'], 'Source fingerprint mismatch')
    stats, owned = provenance.get('resourceStats'), provenance.get('ownedGraphStats')
    require(isinstance(stats, list) and len(stats) == 12 and all(uint(x) for x in stats), 'Invalid resource provenance ABI')
    require(isinstance(owned, list) and len(owned) == 16 and all(uint(x) for x in owned), 'Invalid candidate provenance ABI')
    require(stats[0] == 3 and stats[1] and stats[2] and stats[3] == actual and stats[8:] == [72, 1, 0, 1],
            'Incomplete native resource capture/compiler receipt')
    require(stats[4:8] == [len(raw[5])//8, len(raw[4])//72, len(raw[6])//4, len(raw[7])//8], 'Resource counts differ from binaries')
    changed = [sum(a['phaseOrder'][phase] != b['phaseOrder'][phase] for a, b in zip(parsed['nodes'], candidate['nodes'])) for phase in (0, 1)]
    phase_counts = [sum(e['phase'] == phase for e in candidate['edges']) for phase in (0, 1)]
    require(owned[0:2] == [3, 2] and owned[2:4] == [len(parsed['nodes']), len(parsed['edges'])] and
            owned[4:7] == [1, 1, 1] and owned[7:9] == changed and owned[9:11] == phase_counts and
            owned[11] <= len(parsed['nodes']) and owned[12] <= len(parsed['nodes']) and owned[13:15] == [0, 0] and
            owned[15] == actual, 'Candidate sort receipt differs from source or candidate binaries')
    return actual


def decode_uses(rows_raw, ids_raw, aux_raw, bounds_raw, source):
    raw_rows = values(rows_raw, 72, MAX_ROWS, '<6I48s')
    ids = [v[0] for v in values(ids_raw, 8, MAX_ROWS, '<Q')]
    auxiliary = [v[0] for v in values(aux_raw, 4, MAX_AUX, '<I')]
    boundaries = [v[0] for v in values(bounds_raw, 8, MAX_BOUNDARIES, '<Q')]
    slots = {n['phaseOrder'][1]: n for n in source['nodes']}
    rows, by_location = [], {}
    for slot, index, key, kind, handle, order, raw in raw_rows:
        node = slots.get(slot)
        require(node and index < 500 and handle == node['handle'] and order == node['order'], 'Resource row source slot/node/index mismatch')
        require(raw[0] == kind and raw[1] == slot and struct.unpack_from('<I', raw, 4)[0] == key, 'Resource row metadata differs from raw declaration')
        require((slot, index) not in by_location, 'Duplicate unique declaration row')
        row = dict(row=len(rows), slot=slot, index=index, key=key, kind=kind, node=node['id'], nodeHandle=handle,
                   raw=raw.hex(), payloadWord=struct.unpack_from('<I', raw, 8)[0], callback=struct.unpack_from('<Q', raw, 16)[0] if kind == 2 else 0)
        by_location[slot, index] = row
        rows.append(row)
    represented = set()
    for value in ids:
        location = (value >> 16) & 65535, value & 65535
        require(location in by_location and by_location[location]['key'] == value >> 32, 'Use ID has missing/mismatched declaration')
        represented.add(location)
    require(represented == set(by_location), 'Unreferenced declaration row')
    expected_aux = []
    expected_bounds = []
    for row in rows:
        if row['kind'] in (2, 7, 8):
            expected_aux.append(row['key'])
        if row['kind'] == 7:
            expected_aux.append(row['payloadWord'])
        if row['kind'] == 11 and row['payloadWord'] in (1, 2):
            expected_bounds.append((row['slot'] << 16) | row['index'])
    require(Counter(auxiliary) == Counter(expected_aux), 'Auxiliary key array differs from declarations')
    require(Counter(boundaries) == Counter(expected_bounds), 'Boundary ID array differs from declarations')
    return rows, ids, auxiliary, boundaries


def verified_markers(rows, source, contract):
    require(uint(contract,32) and contract in (0,1,2), 'Invalid marker contract receipt')
    if contract != 1:
        return []
    nodes = {n['id']: n for n in source['nodes']}
    candidates = [r for r in rows if r['key'] == MARKER_KEY]
    if not candidates or any(r['kind'] in (2,7) and r['payloadWord'] == MARKER_KEY for r in rows):
        return []
    for row in candidates:
        node, kind = nodes[row['node']], row['kind']
        if kind in (9,10):
            if node['workRva'] != '0x1ec1d0' or (kind == 9 and bytes.fromhex(row['raw'])[8] > 2):
                return []
        elif kind == 11:
            if node['workRva'] != '0x58da9c' or row['payloadWord'] > 2 or node['definitionWords'][6] != row['payloadWord']:
                return []
        else:
            return []
    return [dict(row=r['row'], node=r['node'], slot=r['slot'], index=r['index'], kind=r['kind'],
                 payload=bytes.fromhex(r['raw'])[8] if r['kind']==9 else r['payloadWord'] if r['kind']==11 else None,
                 meaning={9:'scene-driver-begin',10:'scene-driver-end',11:'native-command-boundary'}[r['kind']]) for r in candidates]


def components(rows, marker_rows=()):
    parent = {}

    def find(value):
        parent.setdefault(value, value)
        while parent[value] != value:
            parent[value] = parent[parent[value]]
            value = parent[value]
        return value

    def join(a, b):
        a, b = find(a), find(b)
        parent[max(a, b)] = min(a, b)

    relations = []
    taints = defaultdict(set)
    scopes = defaultdict(list)
    marker_ids = {row['row'] for row in marker_rows}
    key_name = lambda key: f'key:{key:08x}'
    for row in rows:
        if row['row'] in marker_ids:
            continue
        name, kind = key_name(row['key']), row['kind']
        find(name)
        if kind in (2, 7):
            other = key_name(row['payloadWord'])
            join(name, other)
            relations.append(dict(row=row['row'], kind=kind, source=name, target=other,
                                  meaning='descriptor-source' if kind == 2 else 'native-lifetime-relation'))
            if kind == 2:
                taints[name].add('descriptor-import')
                if row['callback']:
                    taints[name].add('unverified-descriptor-callback')
        elif kind == 8:
            other = f'external-native-handle-unknown-domain:{row["payloadWord"]:08x}'
            join(name, other)
            taints[name].add('external-identity-placement-access-unproved')
            relations.append(dict(row=row['row'], kind=kind, source=name, target=other, meaning='native-handle-import'))
        elif kind not in (1, 4, 5, 11):
            taints[name].add('unsupported-declaration-kind:'+str(kind))
        if kind == 11:
            taints[name].add('native-boundary-semantics-unproved')
        if kind in (4, 5):
            scopes[name].append(row)
    for name, scope in scopes.items():
        depth = 0
        for row in sorted(scope, key=lambda r: (r['slot'], r['index'])):
            if row['kind'] == 4:
                if depth:
                    taints[name].add('nested-lifetime-scope')
                depth += 1
            elif not depth:
                taints[name].add('unmatched-lifetime-end')
            else:
                depth -= 1
        if depth:
            taints[name].add('unmatched-lifetime-begin')
    grouped = defaultdict(lambda: dict(members=[], rows=[], nodes=set(), unsupported=set()))
    for name in parent:
        group = grouped[find(name)]
        group['members'].append(name)
        group['unsupported'].update(taints[name])
    for row in rows:
        if row['row'] in marker_ids:
            continue
        group = grouped[find(key_name(row['key']))]
        group['rows'].append(row['row'])
        group['nodes'].add(row['node'])
    output, per_node, opaque = [], defaultdict(set), set()
    for index, (_, item) in enumerate(sorted(grouped.items())):
        output.append(dict(id=index, members=sorted(item['members']), rows=item['rows'], nodes=sorted(item['nodes']),
                           unsupported=sorted(item['unsupported']), readWriteAccessProved=False))
        if item['unsupported']:
            opaque.add(index)
        for node in item['nodes']:
            per_node[node].add(index)
    return output, per_node, opaque, relations


def analyze(source_nodes, source_edges, candidate_nodes, candidate_edges, rows_raw, ids_raw, aux_raw, bounds_raw, provenance):
    raw = (source_nodes, source_edges, candidate_nodes, candidate_edges, rows_raw, ids_raw, aux_raw, bounds_raw)
    require(isinstance(provenance, dict) and uint(provenance.get('imageBase')) and provenance['imageBase'], 'Missing image provenance')
    base = provenance['imageBase']
    original, nodes, _ = graph(source_nodes, source_edges, base)
    candidate, next_nodes, _ = graph(candidate_nodes, candidate_edges, base)
    require(len(nodes) == len(next_nodes), 'Candidate node count changed')
    for a, b in zip(nodes, next_nodes):
        require(a[:2] == b[:2] and a[3:] == b[3:] and a[2][:28] == b[2][:28] and a[2][30:32] == b[2][30:32],
                'Candidate changed native node identity or definition')
    source_hash = validate_provenance(provenance, raw, original, candidate)
    rows, ids, auxiliary, boundaries = decode_uses(rows_raw, ids_raw, aux_raw, bounds_raw, original)
    markers = verified_markers(rows, original, provenance.get('markerContract',0))
    marker_nodes = {row['node'] for row in markers}
    groups, per_node, opaque, relations = components(rows, markers)
    events, old_edges, following, before = lifted(original)
    next_events, new_edges, _, after = lifted(candidate)
    require(events == next_events, 'Candidate events differ')
    witnesses, classifications, total = [], Counter(), 0
    for start in events:
        lost = before[start] & ~after[start]
        previous = None
        while lost:
            bit = lost & -lost
            target = events[bit.bit_length()-1]
            lost ^= bit
            total += 1
            common = sorted(per_node[start//2] & per_node[target//2])
            affected_opaque = sorted((per_node[start//2] | per_node[target//2]) & opaque)
            marker_order = start//2 in marker_nodes or target//2 in marker_nodes
            status = ('potential-resource-conflict' if common else 'opaque-unproved' if affected_opaque else
                      'marker-order-unproved' if marker_order else 'no-represented-resource-conflict')
            classifications[status] += 1
            if len(witnesses) >= MAX_WITNESSES:
                continue
            if previous is None:
                previous = {start: None}
                pending = deque([start])
                while pending:
                    at = pending.popleft()
                    for child in following[at]:
                        if child not in previous:
                            previous[child] = at
                            pending.append(child)
            path, at = [], target
            while at is not None:
                path.append(at)
                at = previous[at]
            path.reverse()
            witnesses.append(dict(sourceEvent=start, targetEvent=target, sourceNode=start//2, targetNode=target//2,
                                  classification=status, sharedComponents=common, opaqueComponents=affected_opaque,
                                  markerOrderRequired=marker_order,markerOrderProved=False,
                                  originalPath=path, removedEventEdges=[list(e) for e in zip(path, path[1:]) if e not in new_edges]))
    candidate_slots = {n['id']: n['phaseOrder'][1] for n in candidate['nodes']}
    for row in rows:
        row['candidateSlot'] = candidate_slots[row['node']]
    return dict(schema=1, purpose='offline-resource-order-audit', provenance=provenance,
                sourceFingerprint=source_hash, sourceGraphSHA256=hashlib.sha256(source_nodes+source_edges).hexdigest(),
                candidateGraphSHA256=hashlib.sha256(candidate_nodes+candidate_edges).hexdigest(),
                binarySHA256={name: hashlib.sha256(data).hexdigest() for name, data in zip(
                    ('sourceNodes','sourceEdges','candidateNodes','candidateEdges','rows','useIds','auxiliaryKeys','boundaryIds'), raw)},
                counts=dict(nodes=len(nodes), originalEdges=len(original['edges']), candidateEdges=len(candidate['edges']),
                            useIds=len(ids), uniqueRows=len(rows), duplicateIds=len(ids)-len(rows), auxiliaryKeys=len(auxiliary),
                            boundaryIds=len(boundaries), components=len(groups), lostEventRelations=total,
                            nonAllocationMarkers=len(markers),markerNodes=len(marker_nodes),
                            witnessCount=len(witnesses), omittedWitnesses=total-len(witnesses)),
                eventModel='2*node=CPU-ready; 2*node+1=ordered-emission; no reverse wait inferred',
                classifications=dict(classifications), witnessComplete=total==len(witnesses),
                removedEventEdges=[list(e) for e in sorted(old_edges-new_edges)],
                addedEventEdges=[list(e) for e in sorted(new_edges-old_edges)],
                kinds=dict(sorted(Counter(r['kind'] for r in rows).items())), declarations=rows,
                useIds=ids, auxiliaryKeys=auxiliary, boundaryIds=boundaries, components=groups, relations=relations, witnesses=witnesses,
                nonAllocationMarkers=markers,markerOrderRequired=bool(markers),markerOrderProved=False,
                cpuIndependenceProved=False, readWriteAccessProved=False, resourceOrderRecompiled=False,
                runtimeAdmitted=False, executionPermitted=False,
                limitation='Lifetime declarations omit CPU scene/proxy/global state and exact GPU read/write access. No represented conflict is not reorder permission.')


def bounded_file(path, maximum):
    require(path.stat().st_size <= maximum, 'Input file exceeds bounded size: '+str(path))
    return path.read_bytes()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(argv)
    capture = json.loads(bounded_file(args.capture, 64*1024*1024))
    stem = args.capture.with_suffix('')
    graph_meta = json.loads(bounded_file(Path(str(stem)+'-graph.json'), 32*1024*1024))['meta']
    require(isinstance(graph_meta, list) and len(graph_meta) == 4 and all(uint(x) for x in graph_meta), 'Invalid graph meta provenance')
    source_hash = capture['pairedFrontier']['fingerprint']
    require(isinstance(source_hash, str) and source_hash.startswith('0x') and len(source_hash) <= 18, 'Invalid published source fingerprint')
    provenance = dict(pid=capture['pid'], dll=capture['dll'], imageBase=graph_meta[1], sourceFingerprint=int(source_hash, 16),
                      resourceStats=capture['resourceUses']['stats'], ownedGraphStats=capture['ownedGraphSort']['stats'],
                      markerContract=capture['resourceUses'].get('markerContract',0),
                      graphAddress=graph_meta[0], contextRoot=graph_meta[2], captureFile=str(args.capture.resolve()))
    suffixes = ('-graph-nodes.bin','-graph-edges.bin','-owned-graph-nodes.bin','-owned-graph-edges.bin',
                '-resourceusesrows.bin','-resourceuseids.bin','-resourceauxkeys.bin','-resourceboundaryids.bin')
    limits = (MAX_NODES*104, MAX_EDGES*32, MAX_NODES*104, MAX_EDGES*32, MAX_ROWS*72, MAX_ROWS*8, MAX_AUX*4, MAX_BOUNDARIES*8)
    report = analyze(*(bounded_file(Path(str(stem)+suffix), limit) for suffix, limit in zip(suffixes, limits)), provenance)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(**report['counts'], classifications=report['classifications'], witnessComplete=report['witnessComplete'],
                          executionPermitted=False, report=str(args.out)), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
