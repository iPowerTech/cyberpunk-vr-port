import copy
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from graph_parent_report import fingerprint
import resource_order_report as audit


BASE = 0x140000000


def encode_graph(order, dependencies, count=4):
    raw_nodes = []
    for index in range(count):
        raw = bytearray(struct.pack('<4IQ6I', index, 0, 1, 0, 0x10000+index, 1 if index == 2 else 0,
                                    order.index(index) | order.index(index) << 8, *([audit.EMPTY]*4)))
        raw_nodes.append(raw)
    edges = []
    for phase in (0, 1):
        for source, target in dependencies:
            index = len(edges)
            handle = index | 0x40000000
            outgoing = struct.unpack_from('<I', raw_nodes[source], 40+phase*4)[0]
            incoming = struct.unpack_from('<I', raw_nodes[target], 32+phase*4)[0]
            edges.append(struct.pack('<II6I', handle, index, index, phase, source | 0x20000000,
                                     target | 0x20000000, outgoing, incoming))
            struct.pack_into('<I', raw_nodes[source], 40+phase*4, handle)
            struct.pack_into('<I', raw_nodes[target], 32+phase*4, handle)
    nodes = b''.join(struct.pack('<II48sQQ32s', i | 0x20000000, i, raw, 0x20000+i,
                                0x30000+i, bytes([i])*32) for i, raw in enumerate(raw_nodes))
    return nodes, b''.join(edges)


class ResourceOrderTests(unittest.TestCase):
    def fixture(self, declarations=None, duplicates=False):
        source = encode_graph([0, 1, 2, 3], [(0, 1), (1, 2), (2, 3)])
        candidate = encode_graph([0, 2, 1, 3], [(0, 2), (2, 1), (1, 3)])
        declarations = declarations if declarations is not None else [(1, 4, 1, 0, 0), (1, 5, 1, 0, 0),
                                                                     (2, 4, 2, 0, 0), (2, 5, 2, 0, 0)]
        rows, ids, aux, boundaries, indices = [], [], [], [], {}
        for node, kind, key, payload, callback in declarations:
            index = indices.get(node, 0)
            indices[node] = index+1
            raw = bytearray(48)
            raw[0:2] = bytes([kind, node])
            struct.pack_into('<II', raw, 4, key, payload)
            struct.pack_into('<Q', raw, 16, callback)
            rows.append(struct.pack('<6I48s', node, index, key, kind, node | 0x20000000, node, raw))
            ids.append(key << 32 | node << 16 | index)
            if kind in (2, 7, 8):aux.append(key)
            if kind == 7:aux.append(payload)
            if kind == 11 and payload in (1, 2):boundaries.append(node << 16 | index)
        if duplicates and ids:ids.append(ids[0])
        # Native append order is intentionally not slot order.
        ids.reverse()
        encoded = (*source, *candidate, b''.join(rows), b''.join(struct.pack('<Q', x) for x in ids),
                   b''.join(struct.pack('<I', x) for x in reversed(aux)), b''.join(struct.pack('<Q', x) for x in boundaries))
        source_hash = fingerprint(*source, BASE)
        provenance = dict(pid=277, dll='a'*64, imageBase=BASE, sourceFingerprint=source_hash,
                          resourceStats=[3, 0x10000000, 0x20000000, source_hash, len(ids), len(rows), len(aux), len(boundaries), 72, 1, 0, 1],
                          ownedGraphStats=[3, 2, 4, 6, 1, 1, 1, 2, 2, 3, 3, 1, 1, 0, 0, source_hash])
        return list(encoded), provenance

    def run_fixture(self, declarations=None, duplicates=False):
        raw, provenance = self.fixture(declarations, duplicates)
        return audit.analyze(*raw, provenance)

    def crossing(self, report):
        return [w for w in report['witnesses'] if (w['sourceNode'], w['targetNode']) == (1, 2)]

    def test_complete_lifted_witnesses_and_no_admission(self):
        report = self.run_fixture()
        self.assertEqual(report['counts']['lostEventRelations'], 3)
        self.assertEqual(len(self.crossing(report)), 3)
        self.assertTrue(report['witnessComplete'])
        for witness in self.crossing(report):
            self.assertEqual(witness['classification'], 'no-represented-resource-conflict')
            self.assertTrue(witness['removedEventEdges'])
            self.assertEqual(witness['originalPath'][0], witness['sourceEvent'])
            self.assertEqual(witness['originalPath'][-1], witness['targetEvent'])
        for name in ('executionPermitted','runtimeAdmitted','readWriteAccessProved','cpuIndependenceProved','resourceOrderRecompiled'):
            self.assertIs(report[name], False)
        self.assertEqual([r['candidateSlot'] for r in report['declarations']], [2, 2, 1, 1])

    def test_same_key_is_potential_conflict_not_write_read_claim(self):
        report = self.run_fixture([(1,4,1,0,0),(1,5,1,0,0),(2,4,1,0,0),(2,5,1,0,0)])
        self.assertTrue(all(w['classification'] == 'potential-resource-conflict' for w in self.crossing(report)))
        self.assertFalse(report['components'][0]['readWriteAccessProved'])

    def test_kind7_connects_two_keys(self):
        report = self.run_fixture([(0,7,1,2,0),(1,4,1,0,0),(1,5,1,0,0),(2,4,2,0,0),(2,5,2,0,0)])
        self.assertEqual(len(report['components']), 1)
        self.assertTrue(all(w['sharedComponents'] for w in self.crossing(report)))
        self.assertEqual(report['relations'][0]['meaning'], 'native-lifetime-relation')

    def test_kind2_descriptor_source_and_callback_taint(self):
        report = self.run_fixture([(1,2,1,2,0x11223344),(2,4,2,0,0),(2,5,2,0,0)])
        self.assertEqual(len(report['components']), 1)
        self.assertIn('unverified-descriptor-callback', report['components'][0]['unsupported'])
        self.assertTrue(all(w['sharedComponents'] and w['opaqueComponents'] for w in self.crossing(report)))
        self.assertEqual(report['relations'][0]['meaning'], 'descriptor-source')

    def test_kind8_handle_is_not_resource_key(self):
        report = self.run_fixture([(1,8,1,2,0),(2,4,2,0,0),(2,5,2,0,0)])
        self.assertEqual(len(report['components']), 2)
        self.assertTrue(all(not w['sharedComponents'] and w['classification'] == 'opaque-unproved' for w in self.crossing(report)))
        self.assertIn('external-native-handle-unknown-domain:00000002', report['relations'][0]['target'])
        report = self.run_fixture([(1,8,1,9,0),(2,8,2,9,0)])
        self.assertEqual(len(report['components']), 1)
        self.assertTrue(all(w['sharedComponents'] for w in self.crossing(report)))

    def marker_fixture(self, extra=(), contract=1):
        raw, provenance = self.fixture([(1,9,audit.MARKER_KEY,1,0),(1,10,audit.MARKER_KEY,0xabcdef00,0),
                                        (2,9,audit.MARKER_KEY,0x12345602,0),(2,10,audit.MARKER_KEY,0xffffffff,0),*extra])
        for at in (0,2):
            data=bytearray(raw[at])
            for node in (1,2):struct.pack_into('<Q',data,node*104+64,BASE+0x1ec1d0)
            raw[at]=bytes(data)
        source_hash=fingerprint(raw[0],raw[1],BASE)
        provenance.update(sourceFingerprint=source_hash,markerContract=contract)
        provenance['resourceStats'][3]=provenance['ownedGraphStats'][15]=source_hash
        return raw,provenance

    def test_verified_markers_are_not_a_shared_physical_resource(self):
        raw,provenance=self.marker_fixture()
        report=audit.analyze(*raw,provenance)
        self.assertEqual(report['components'],[])
        self.assertEqual(report['counts']['nonAllocationMarkers'],4)
        self.assertEqual([r['payload'] for r in report['nonAllocationMarkers']],[1,None,2,None])
        self.assertTrue(all(w['classification']=='marker-order-unproved' and not w['sharedComponents'] for w in self.crossing(report)))
        self.assertTrue(report['markerOrderRequired']);self.assertFalse(report['markerOrderProved'])
        self.assertFalse(report['executionPermitted'])

    def test_marker_contract_and_exact_source_work_required(self):
        for contract in (0,2):
            raw,provenance=self.marker_fixture(contract=contract)
            report=audit.analyze(*raw,provenance)
            self.assertEqual(report['nonAllocationMarkers'],[])
            self.assertTrue(all(w['sharedComponents'] for w in self.crossing(report)))
        for contract in (True,3,'1'):
            raw,provenance=self.marker_fixture(contract=contract)
            with self.assertRaises(ValueError):audit.analyze(*raw,provenance)
        raw,provenance=self.fixture([(1,9,audit.MARKER_KEY,1,0),(2,10,audit.MARKER_KEY,0,0)])
        provenance['markerContract']=1
        self.assertEqual(audit.analyze(*raw,provenance)['nonAllocationMarkers'],[])

    def test_marker_key_import_or_mixed_resource_is_not_excluded(self):
        for extra in ([(0,7,5,audit.MARKER_KEY,0)],[(0,2,5,audit.MARKER_KEY,0)],[(1,4,audit.MARKER_KEY,0,0)],
                      [(2,9,audit.MARKER_KEY,3,0)]):
            raw,provenance=self.marker_fixture(extra)
            report=audit.analyze(*raw,provenance)
            self.assertEqual(report['nonAllocationMarkers'],[])
            self.assertTrue(all(w['sharedComponents'] for w in self.crossing(report)))

    def test_boundary_marker_requires_definition_payload_and_native_work(self):
        raw,provenance=self.marker_fixture([(0,11,audit.MARKER_KEY,2,0)])
        for at in (0,2):
            data=bytearray(raw[at]);struct.pack_into('<Q',data,64,BASE+0x58da9c);struct.pack_into('<I',data,96,2);raw[at]=bytes(data)
        source_hash=fingerprint(raw[0],raw[1],BASE)
        provenance['sourceFingerprint']=provenance['resourceStats'][3]=provenance['ownedGraphStats'][15]=source_hash
        report=audit.analyze(*raw,provenance)
        self.assertEqual(report['counts']['nonAllocationMarkers'],5)
        self.assertEqual(report['nonAllocationMarkers'][-1]['meaning'],'native-command-boundary')
        raw,provenance=self.marker_fixture([(0,11,audit.MARKER_KEY,3,0)])
        self.assertEqual(audit.analyze(*raw,provenance)['nonAllocationMarkers'],[])

    def test_duplicates_preserved_but_rows_deduplicated(self):
        report = self.run_fixture(duplicates=True)
        self.assertEqual(report['counts']['duplicateIds'], 1)
        self.assertEqual(report['counts']['uniqueRows'], 4)
        self.assertEqual(len(report['useIds']), 5)

    def test_unknown_kind_and_scope_ambiguity_are_opaque(self):
        for declarations, reason in (([(1,250,1,0,0)], 'unsupported-declaration-kind:250'),
                                     ([(1,4,1,0,0)], 'unmatched-lifetime-begin'),
                                     ([(1,5,1,0,0)], 'unmatched-lifetime-end'),
                                     ([(1,4,1,0,0),(1,4,1,0,0),(1,5,1,0,0),(1,5,1,0,0)], 'nested-lifetime-scope')):
            with self.subTest(reason=reason):
                self.assertIn(reason, self.run_fixture(declarations)['components'][0]['unsupported'])

    def test_actual_auxiliary_and_boundary_arrays_required(self):
        raw, provenance = self.fixture([(1,7,1,2,0),(2,11,2,1,0)])
        audit.analyze(*raw, provenance)
        for index, replacement in ((6, struct.pack('<2I',1,3)), (7, struct.pack('<Q',2<<16|1))):
            bad = raw.copy();bad[index] = replacement
            with self.subTest(index=index),self.assertRaises(ValueError):audit.analyze(*bad, provenance)

    def test_malformed_rows_ids_and_binary_bounds(self):
        for index in range(8):
            raw, provenance = self.fixture()
            raw[index] += b'x'
            with self.subTest(index=index),self.assertRaises(ValueError):audit.analyze(*raw, provenance)
        for offset, value in ((0,219),(4,500),(8,5),(12,8),(16,0),(20,2),(24+1,0)):
            raw, provenance = self.fixture();row = bytearray(raw[4])
            if offset == 25:row[offset] = value
            else:struct.pack_into('<I',row,offset,value)
            raw[4] = bytes(row)
            with self.subTest(offset=offset),self.assertRaises(ValueError):audit.analyze(*raw, provenance)
        raw, provenance = self.fixture();raw[5] = struct.pack('<Q', 99<<32|1<<16)+raw[5][8:]
        with self.assertRaises(ValueError):audit.analyze(*raw, provenance)
        raw, provenance = self.fixture();raw[5] = b'\0'*(4097*8)
        with self.assertRaises(ValueError):audit.analyze(*raw, provenance)

    def test_source_provenance_and_candidate_identity(self):
        raw, provenance = self.fixture()
        for name, value in (('sourceFingerprint',1),('imageBase',True),('pid',0),('dll','not-a-hash')):
            with self.subTest(name=name),self.assertRaises(ValueError):audit.analyze(*raw, dict(provenance, **{name:value}))
        for key, at, value in (('resourceStats',0,2),('resourceStats',9,0),('resourceStats',11,0),
                               ('resourceStats',4,0),('ownedGraphStats',15,1),('ownedGraphStats',7,0)):
            bad = copy.deepcopy(provenance);bad[key][at] = value
            with self.subTest(key=key,at=at),self.assertRaises(ValueError):audit.analyze(*raw, bad)
        mutated = raw.copy();candidate = bytearray(raw[2]);candidate[8+16] ^= 1;mutated[2] = bytes(candidate)
        with self.assertRaisesRegex(ValueError, 'identity'):audit.analyze(*mutated, provenance)

    def test_graph_bad_phase_rank_adjacency_and_cycle(self):
        raw, provenance = self.fixture()
        for at, value in ((8+29,1),(8+40,12345)):
            candidate = bytearray(raw[2])
            if at == 37:candidate[at] = value
            else:struct.pack_into('<I',candidate,at,value)
            bad = raw.copy();bad[2] = bytes(candidate)
            with self.subTest(at=at),self.assertRaises(ValueError):audit.analyze(*bad, provenance)
        nodes, edges = encode_graph([0,1,2,3], [(0,1),(1,2),(2,1)])
        with self.assertRaisesRegex(ValueError,'ranks'):audit.graph(nodes, edges, BASE)
        parsed = audit.decode_graph(nodes, edges, BASE)
        with self.assertRaisesRegex(ValueError,'Cyclic'):audit.lifted(parsed)

    def test_edge_pool_handles_are_not_publication_ordinals(self):
        nodes, edges = encode_graph([0,1,2,3], [(0,1),(1,2),(2,3)])
        nodes, edges = bytearray(nodes), bytearray(edges)
        for at in range(0,len(edges),32):
            handle = struct.unpack_from('<I',edges,at)[0]
            struct.pack_into('<I',edges,at,handle+17)
            for link in (24,28):
                value = struct.unpack_from('<I',edges,at+link)[0]
                if value != audit.EMPTY:struct.pack_into('<I',edges,at+link,value+17)
        for at in range(0,len(nodes),104):
            for link in (40,44,48,52):
                value = struct.unpack_from('<I',nodes,at+link)[0]
                if value != audit.EMPTY:struct.pack_into('<I',nodes,at+link,value+17)
        parsed, _, _ = audit.graph(bytes(nodes),bytes(edges),BASE)
        self.assertEqual(parsed['edges'][0]['id'],17)
        self.assertEqual(parsed['edges'][0]['ordinal'],0)

    def test_witness_limit_is_explicit_without_losing_totals(self):
        with patch.object(audit, 'MAX_WITNESSES', 1):report = self.run_fixture()
        self.assertFalse(report['witnessComplete'])
        self.assertEqual((report['counts']['lostEventRelations'],report['counts']['omittedWitnesses']), (3,2))
        self.assertEqual(sum(report['classifications'].values()), 3)

    def test_cli_uses_saved_files_only(self):
        raw, provenance = self.fixture()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);capture = root/'preparation.json'
            capture.write_text(json.dumps(dict(pid=provenance['pid'],dll=provenance['dll'],
                pairedFrontier=dict(fingerprint=hex(provenance['sourceFingerprint'])),
                resourceUses=dict(stats=provenance['resourceStats']),ownedGraphSort=dict(stats=provenance['ownedGraphStats']))))
            (root/'preparation-graph.json').write_text(json.dumps(dict(meta=[1,BASE,2,1])))
            suffixes = ('-graph-nodes.bin','-graph-edges.bin','-owned-graph-nodes.bin','-owned-graph-edges.bin',
                        '-resourceusesrows.bin','-resourceuseids.bin','-resourceauxkeys.bin','-resourceboundaryids.bin')
            for suffix, data in zip(suffixes, raw):(root/('preparation'+suffix)).write_bytes(data)
            with patch('sys.stdout',io.StringIO()):self.assertEqual(audit.main(['--capture',str(capture),'--out',str(root/'report.json')]),0)
            result = json.loads((root/'report.json').read_text());self.assertFalse(result['executionPermitted'])


if __name__ == '__main__':unittest.main()
