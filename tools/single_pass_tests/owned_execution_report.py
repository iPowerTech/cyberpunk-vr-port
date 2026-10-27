"""Validate one coherent owned REFERENCE CPU execution export snapshot.

The caller must read all arrays under the same stable OwnedExecutionSeq. These
receipts do not prove GPU completion, whole-scene single-pass, or a speedup.
"""

from collections.abc import Sequence
import struct


UINT64_MAX = (1 << 64) - 1
STATS_WORDS = 24
RESOURCE_WORDS = 12
COMPILER_WORDS = 16
COMPILER_RETURN_RVA = 0xAF5A15
COMPILER_MODE = 2
COMPILED_POOL_STATE = 2
MAX_GRAPH_NODES = 8192
MAX_GRAPH_EDGES = 65536
MAX_TREE_NODES = 4096
MAX_PROTECTED_OWNERS = 128


def decode_schedule(values, stats, resources, source_nodes, source_edges, actual_nodes, actual_edges, base, *, bindings, coupled=False):
    from graph_parent_report import fingerprint, name_hash
    from resource_order_report import graph as parse_graph
    from whole_gbuffer_report import _prefix
    v=_words(values,16,'OwnedExecutionSchedule')
    decode(stats,resources)
    if v[:4]!=(2 if coupled else 1,stats[1],stats[13],resources[6]) or v[14:]!=(1,1):
        raise ValueError('Owned schedule mode/consumer/fingerprint mismatch')
    source,_,_=parse_graph(source_nodes,source_edges,base)
    actual,_,_=parse_graph(actual_nodes,actual_edges,base)
    if v[2]!=fingerprint(source_nodes,source_edges,base) or v[3]!=fingerprint(actual_nodes,actual_edges,base) or v[4:7]!=(len(source['nodes']),len(source['edges']),len(actual['edges'])) or len(source['nodes'])!=len(actual['nodes']):
        raise ValueError('Owned schedule graph identity differs')
    before=list(struct.iter_unpack('<II48sQQ32s',source_nodes));after=list(struct.iter_unpack('<II48sQQ32s',actual_nodes))
    if len(bindings)!=len(source_nodes):raise ValueError('Missing declaration-time source bindings')
    current=list(struct.iter_unpack('<II48sQQ32s',bindings))
    for a,b,c in zip(before,after,current):
        if a[:5]!=c[:5] or c[:2]!=b[:2] or c[2][:28]!=b[2][:28] or c[2][30:32]!=b[2][30:32] or c[3:]!=b[3:]:
            raise ValueError('Owned schedule changed a native node/definition')
    current_source,_,_=parse_graph(bindings,source_edges,base)
    changed=tuple(sum(a[2][28+phase]!=b[2][28+phase] for a,b in zip(before,after)) for phase in range(2))
    edges=lambda graph,phase:{(e['source'],e['target']) for e in graph['edges'] if e['phase']==phase}
    old=[edges(source,i) for i in range(2)];new=[edges(actual,i) for i in range(2)]
    slots={node['id']:node['phaseOrder'][1] for node in actual['nodes']}
    _,_,required,missing=_prefix(actual['nodes'],new[0],slots,base)
    if v[7:9]!=changed or v[9:12]!=(required,required,0) or missing:
        raise ValueError('Owned schedule lost native context publication')
    if not coupled:
        if any(changed) or old!=new or v[12] or v[13]:raise ValueError('Reference schedule changed dependencies')
    else:
        if not all(changed):raise ValueError('Coupled schedule did not change both orders')
        nodes={node['id']:node for node in current_source['nodes']}
        def path(links,start,end):
            seen={start};pending=[start]
            for at in pending:
                if at==end:return True
                for a,b in links:
                    if a==at and b not in seen:seen.add(b);pending.append(b)
            return False
        def find(eye,work,name=None):
            found=[n['id'] for n in nodes.values() if n['view']==eye and int(n['work'],16)==base+work and
                   (name is None or n['definitionWords'][6]|n['definitionWords'][7]<<32==name_hash(name))]
            if len(found)!=1:raise ValueError('Ambiguous coupled graph role')
            return found[0]
        main=find(0,0x1EC1D0,'GBuffer_Discard');peer=find(1,0x1EC1D0,'GBuffer_Discard')
        final=find(0,0x1EC1D0,'RenderFinal2D');collector=find(1,0x79B03C)
        predecessor=[a for a,b in old[1] if b==main];successor=[b for a,b in old[1] if a==peer]
        if len(predecessor)!=1 or len(successor)!=1:raise ValueError('Ambiguous coupled boundary')
        expected=old[1]-{(predecessor[0],main),(final,collector),(peer,successor[0])}|{(predecessor[0],collector),(peer,main),(final,successor[0])}
        if new[1]!=expected or not path(new[0],peer,main):raise ValueError('Coupled GPU boundary or CPU readiness differs')
        early={collector};at=collector
        while at!=peer:
            following=[b for a,b in old[1] if a==at]
            if len(following)!=1 or following[0] in early:raise ValueError('Invalid original peer prefix')
            at=following[0];early.add(at)
        changed_set=True
        while changed_set:
            changed_set=False
            for a,b in old[0]:
                if b in early and a not in early and nodes[a]['view']==1 and int(nodes[a]['work'],16)==0 and int(nodes[a]['object'],16)==0:
                    early.add(a);changed_set=True
        if any(a not in early and b not in early for a,b in old[0]-new[0]):raise ValueError('Coupled schedule removed an unrelated CPU edge')
        shared=reverse=0
        for a in nodes.values():
            if not int(a['object'],16):continue
            for b in nodes.values():
                if a['id']==b['id'] or a['object']!=b['object'] or not path(old[0],a['id'],b['id']):continue
                shared+=1
                if (a['vtable'],a['work'])!=(b['vtable'],b['work']):raise ValueError('Coupled shared object type differs')
                if not path(new[0],a['id'],b['id']):
                    if (a['id'],b['id'])!=(main,peer) or not path(new[0],peer,main):raise ValueError('Coupled shared object became unordered')
                    reverse+=1
        if v[12:14]!=(shared,reverse):raise ValueError('Coupled shared-owner counts differ')
    return dict(mode='coupled' if coupled else 'reference',stats=list(v),changedCpuRanks=changed[0],changedGpuRanks=changed[1],
                nativeContextPairs=required,nativeDrawSuppression=0,sharedScenePreparationProved=False,performanceGainProved=False)


def _words(values, count, name):
    if (not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray))
            or len(values) != count):
        raise ValueError(f'{name} binary ABI/count mismatch: expected {count} uint64 words')
    if any(type(value) is not int or not 0 <= value <= UINT64_MAX for value in values):
        raise ValueError(f'{name} has a non-uint64 field')
    return tuple(values)


def _pointer(value, name):
    if not value or value & 7:
        raise ValueError(f'Missing or unaligned {name} identity')


def _expected(value, actual, name):
    if value is not None:
        if type(value) is not int or not 0 < value <= UINT64_MAX or value != actual:
            raise ValueError(f'Owned execution has a stale or invalid expected {name}')


def decode(stats, resources, expected_epoch=None, expected_pair=None):
    """Return a compact report only for a complete, freshly compiled receipt."""
    stats = _words(stats, STATS_WORDS, 'OwnedExecutionStats')
    resources = _words(resources, RESOURCE_WORDS, 'OwnedExecutionResources')
    if stats[0] != 4 or stats[22] or stats[23]:
        raise ValueError('Owned REFERENCE execution is incomplete, rejected, or quarantined')
    consumer, epoch, pair = stats[1:4]
    if not consumer or not epoch or not pair:
        raise ValueError('Missing owned execution consumer/epoch/pair identity')
    _expected(expected_epoch, epoch, 'epoch')
    _expected(expected_pair, pair, 'pair generation')

    source, execution, frame, execution_group = stats[4:8]
    declaration_group = stats[16]
    for name, pointer in (('source graph', source), ('execution graph', execution),
                          ('frame owner', frame), ('execution group', execution_group),
                          ('declaration group', declaration_group)):
        _pointer(pointer, name)
    if source == execution:
        raise ValueError('Owned execution did not redirect the native source graph')
    if declaration_group == execution_group:
        raise ValueError('Declaration and execution terminals alias')

    nodes, edges, tree_nodes, arrays, strings = stats[8:13]
    if (not 0 < nodes <= MAX_GRAPH_NODES or edges > MAX_GRAPH_EDGES
            or not 0 < tree_nodes <= MAX_TREE_NODES
            or arrays > tree_nodes or strings > tree_nodes or arrays + strings > tree_nodes):
        raise ValueError('Owned execution graph/tree counts are missing or out of bounds')
    if not stats[13]:
        raise ValueError('Missing native source fingerprint')
    for index, name in ((14, 'publication'), (15, 'constructor verification'),
                        (17, 'declaration match'), (18, 'producer closure'),
                        (19, 'original execution return'), (20, 'both terminal completions')):
        if stats[index] != 1:
            raise ValueError(f'Missing exact owned execution {name} proof')
    if not 0 < stats[21] <= MAX_PROTECTED_OWNERS:
        raise ValueError('Owned execution has no bounded protected native owners')

    if resources[0] != 3 or resources[11]:
        raise ValueError('Owned resources have no successful fresh compilation receipt')
    pool, wrapper = resources[1:3]
    _pointer(pool, 'resource pool')
    _pointer(wrapper, 'resource wrapper')
    if (resources[3] != source or resources[4] != execution
            or resources[5] != stats[13] or not resources[6]):
        raise ValueError('Owned resources have a torn graph/fingerprint binding')
    if resources[7] != 1:
        raise ValueError('Owned resources have no exact declaration match')
    if (resources[8] != COMPILER_RETURN_RVA or resources[9] != COMPILER_MODE
            or resources[10] != COMPILED_POOL_STATE):
        raise ValueError('Owned resources used an unaudited compiler call/mode/pool state')

    return dict(
        status='complete', mode='reference', consumerId=consumer, epoch=epoch,
        pairGeneration=pair, sourceGraph=hex(source), executionGraph=hex(execution),
        frameOwner=hex(frame), declarationGroup=hex(declaration_group),
        executionGroup=hex(execution_group), nodes=nodes, edges=edges,
        treeNodes=tree_nodes, arrays=arrays, strings=strings,
        protectedOwners=stats[21], sourceFingerprint=hex(stats[13]),
        resources=dict(state='freshCompiled', pool=hex(pool), wrapper=hex(wrapper),
                       executionFingerprint=hex(resources[6]),
                       compilerReturnRva=hex(resources[8]), mode=resources[9],
                       poolState=resources[10]),
        cpuExecutionProved=True, gpuLifetimeProved=False,
        wholeSceneSinglePassProved=False, performanceGainProved=False,
    )


def decode_compiler(compiler, stats, resources):
    """Require a bound, fully closed D1 CPU compiler terminal receipt."""
    compiler = _words(compiler, COMPILER_WORDS, 'OwnedExecutionCompiler')
    stats = _words(stats, STATS_WORDS, 'OwnedExecutionStats')
    resources = _words(resources, RESOURCE_WORDS, 'OwnedExecutionResources')
    decode(stats, resources)
    if compiler[8]:
        raise ValueError('Owned compiler identity receipt was rejected')
    for index, name in ((2, 'capture'), (3, 'compiler match'), (4, 'dispatch return'),
                        (5, 'producer closure'), (6, 'original compiler return'),
                        (7, 'terminal completion')):
        if compiler[index] != 1:
            raise ValueError(f'Missing exact owned compiler {name} proof')
    if (compiler[0] != stats[1] or compiler[14] != stats[2] or compiler[15] != stats[3]
            or compiler[12] != stats[6] or compiler[13] != stats[5]
            or compiler[10] != resources[2] or compiler[11] != resources[1]):
        raise ValueError('Owned compiler has a torn consumer/epoch/graph/resource binding')
    _pointer(compiler[1], 'compiler D1 group')
    _pointer(compiler[9], 'compiler renderer')
    return dict(
        consumerId=compiler[0], epoch=compiler[14], pairGeneration=compiler[15],
        group=hex(compiler[1]), renderer=hex(compiler[9]), wrapper=hex(compiler[10]),
        pool=hex(compiler[11]), frameOwner=hex(compiler[12]), executionGraph=hex(compiler[13]),
        terminalCompletionProved=True, gpuLifetimeProved=False,
        wholeSceneSinglePassProved=False, performanceGainProved=False,
    )
