"""Hermetic pure evaluator tests, plus optional original-DXIL integration."""

from dataclasses import FrozenInstanceError, replace
import json
from pathlib import Path
import struct
import tempfile
import unittest

from source_selector_report import (
    BindingMismatch, CbvKey, EvidenceCatalog, EvidenceMismatch, FamilyKey, FamilyPlan,
    OwnedCbvBytes, SelectorOutOfRange, SelectorSite, SrvTableKey, UnsupportedFamily,
    U32Operation, Visibility, evaluate, load_evidence,
)


def hermetic_catalog():
    """Small in-memory arithmetic fixtures, not native/source-IR certificates."""
    pixel_offsets = {
        1: (48, 96, 144, 176, 0), 2: (0, 32, 80, 128, 160, 176),
        3: (48, 80, 128, 176), 4: (48, 0, 32, 16), 5: (48, 0, 32, 16),
        6: (48, 0, 32, 16), 7: (160, 0, 96, 112, 208),
        8: (0, 0, 0, 64, 112, 64, 112, 192, 32, 16),
        9: (48, 96, 144, 176, 0), 10: (48, 0, 32, 16), 11: (48, 0, 32, 16),
        12: (0, 32, 80, 128, 160), 14: (0, 32, 80, 128, 160),
    }
    vertex_offsets = (32, 32, 16, 16, 16, 16, 16, 16, 16, 16, 0)
    plans = []
    for number in range(1, 15):
        key = FamilyKey('a' * 64, f'{number * 2:064x}', f'{number * 2 + 1:064x}')
        if number == 13:
            plans.append(FamilyPlan(key, number, (), 'Hermetic fixture requires actual t5 inputs',
                                    tuple(f'ps:fixture:site{index}:t5 space0 field{(100,104,108)[index % 3]}'
                                          for index in range(10))))
            continue
        sites = []
        stages = ((Visibility.VS, vertex_offsets),) if number in (4, 10, 11) else ()
        for visibility, offsets in (*stages, (Visibility.PS, pixel_offsets[number])):
            cbv = CbvKey(key.root_sha256, visibility, 4, 0, 1 if visibility == Visibility.VS else 19, 0)
            shader = key.vs_sha256 if visibility == Visibility.VS else key.ps_sha256
            for index, offset in enumerate(offsets):
                space3 = number == 7 and visibility == Visibility.PS and offset == 208
                table = SrvTableKey(key.root_sha256, visibility, 33 if space3 else 13 if visibility == Visibility.VS else 31,
                                    3 if space3 else 1, 0, 0, 32768)
                sites.append(SelectorSite(shader, f'%handle{index}', index + 1, f'%index{index}', cbv, offset,
                                          (U32Operation('add', 0),), table))
        plans.append(FamilyPlan(key, number, tuple(sites)))
    return EvidenceCatalog(tuple(plans), 'hermetic-fixture-not-native-evidence')


AUDIT_ROOT = Path(__file__).resolve().parents[2] / 'build' / 'single-pass-20261005'
BOUNDS_PATH = AUDIT_ROOT / 'source-selector-bounds-audit' / 'bounds.json'
REFLECTION_PATH = AUDIT_ROOT / 'source-table-reflection-audit' / 'matrix.json'
REAL_ARTIFACTS_AVAILABLE = BOUNDS_PATH.is_file() and REFLECTION_PATH.is_file()


class SourceSelectorReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence = hermetic_catalog()

    def family(self, number):
        return next(plan for plan in self.evidence.families if plan.number == number)

    def inputs(self, plan, value=7):
        result = []
        for key in plan.required_cbvs:
            data = bytearray(plan.required_byte_extent(key))
            for offset in {site.byte_offset for site in plan.sites if site.cbv == key}:
                struct.pack_into('<I', data, offset, value)
            result.append(OwnedCbvBytes(key, bytes(data)))
        return result

    def test_catalog_has_13_supported_and_one_explicit_multilayer_family(self):
        self.assertEqual(len(self.evidence.families), 14)
        self.assertEqual(sum(plan.supported for plan in self.evidence.families), 13)
        unsupported = self.family(13)
        self.assertFalse(unsupported.supported)
        self.assertEqual(len(unsupported.unresolved_inputs), 10)
        self.assertTrue(all('t5 space0' in entry for entry in unsupported.unresolved_inputs))
        with self.assertRaises(UnsupportedFamily):
            evaluate(unsupported, [])
        with self.assertRaises(UnsupportedFamily):
            evaluate(unsupported, self.inputs(self.family(14)))

    def test_every_supported_family_and_all_sites_are_evaluated(self):
        total = 0
        for plan in self.evidence.families:
            if not plan.supported:
                continue
            with self.subTest(family=plan.number):
                result = evaluate(plan, self.inputs(plan))
                self.assertEqual(len(result.selections), len(plan.sites))
                self.assertTrue(all(entry.shader_index == 7 for entry in result.selections))
                self.assertTrue(all(table.offsets == (7,) and table.count == 8 for table in result.tables))
                self.assertEqual(self.evidence.select(plan.key), plan)
                total += len(result.selections)
        self.assertEqual(total, 98)

    def test_known_per_family_offsets_and_exact_stage_root_slots(self):
        expected = {
            1: {Visibility.PS: (0, 48, 96, 144, 176)},
            2: {Visibility.PS: (0, 32, 80, 128, 160, 176)},
            3: {Visibility.PS: (48, 80, 128, 176)},
            4: {Visibility.VS: (0, 16, 32), Visibility.PS: (0, 16, 32, 48)},
            5: {Visibility.PS: (0, 16, 32, 48)},
            6: {Visibility.PS: (0, 16, 32, 48)},
            7: {Visibility.PS: (0, 96, 112, 160, 208)},
            8: {Visibility.PS: (0, 16, 32, 64, 112, 192)},
            9: {Visibility.PS: (0, 48, 96, 144, 176)},
            10: {Visibility.VS: (0, 16, 32), Visibility.PS: (0, 16, 32, 48)},
            11: {Visibility.VS: (0, 16, 32), Visibility.PS: (0, 16, 32, 48)},
            12: {Visibility.PS: (0, 32, 80, 128, 160)},
            14: {Visibility.PS: (0, 32, 80, 128, 160)},
        }
        for number, wanted in expected.items():
            plan = self.family(number)
            actual = {key.visibility: tuple(sorted({site.byte_offset for site in plan.sites if site.cbv == key}))
                      for key in plan.required_cbvs}
            self.assertEqual(actual, wanted)
            for key in plan.required_cbvs:
                self.assertEqual((key.shader_register, key.space, key.parameter, key.table_offset),
                                 (4, 0, 1 if key.visibility == Visibility.VS else 19, 0))

    def test_duplicate_sites_preserved_and_descriptor_slots_deduplicated(self):
        plan = self.family(10)
        snapshots = self.inputs(plan, 3)
        result = evaluate(plan, snapshots)
        self.assertEqual(len(result.selections), 15)
        self.assertEqual(sum(site.site.cbv.visibility == Visibility.VS for site in result.selections), 11)
        self.assertEqual([table.offsets for table in result.tables], [(3,), (3,)])
        self.assertEqual([table.count for table in result.tables], [4, 4])
        with self.assertRaises(BindingMismatch):
            evaluate(plan, [*snapshots, snapshots[0]])

    def test_exact_input_identity_missing_extra_wrong_root_stage_register_space_slot(self):
        plan = self.family(10)
        snapshots = self.inputs(plan)
        for field, value in (('root_sha256', '0' * 64), ('visibility', Visibility.PS),
                             ('shader_register', 3), ('space', 1), ('parameter', 19), ('table_offset', 1)):
            with self.subTest(field=field):
                bad = replace(snapshots[0], key=replace(snapshots[0].key, **{field: value}))
                with self.assertRaises(BindingMismatch):
                    evaluate(plan, [bad, snapshots[1]])
        with self.assertRaises(BindingMismatch):
            evaluate(plan, snapshots[:1])
        with self.assertRaises(BindingMismatch):
            evaluate(self.family(1), snapshots)
        with self.assertRaises(BindingMismatch):
            self.evidence.select(replace(plan.key, ps_sha256='f' * 64))

    def test_short_and_mutable_bytes_rejected_without_reads(self):
        for number in (1, 4, 7, 10):
            plan = self.family(number)
            snapshots = self.inputs(plan)
            bad = replace(snapshots[-1], data=snapshots[-1].data[:-1])
            with self.assertRaises(BindingMismatch):
                evaluate(plan, [*snapshots[:-1], bad])
        plan = self.family(1)
        snapshot = self.inputs(plan)[0]
        for data in (bytearray(snapshot.data), memoryview(snapshot.data), None, 'bytes'):
            with self.assertRaises(BindingMismatch):
                evaluate(plan, [replace(snapshot, data=data)])
        for field, value in (('visibility', 5), ('shader_register', True), ('space', False), ('table_offset', False)):
            with self.assertRaises(BindingMismatch):
                evaluate(plan, [replace(snapshot, key=replace(snapshot.key, **{field: value}))])

    def test_out_of_range_values_are_rejected_not_masked(self):
        for value in (32768, 65535, 65536, 0x80000000, 0xFFFFFFFF):
            with self.subTest(value=value):
                with self.assertRaises(SelectorOutOfRange):
                    evaluate(self.family(7), self.inputs(self.family(7), value))
        for value in (0, 32767):
            result = evaluate(self.family(7), self.inputs(self.family(7), value))
            self.assertTrue(all(entry.source_dword == value == entry.shader_index for entry in result.selections))
            self.assertTrue(all(table.count == value + 1 for table in result.tables))

    def test_sparse_and_contiguous_typed_slot_intervals(self):
        plan = self.family(1)
        snapshot = self.inputs(plan)[0]
        data = bytearray(snapshot.data)
        for offset, value in zip((0, 48, 96, 144, 176), (0, 1, 2, 9, 11)):
            struct.pack_into('<I', data, offset, value)
        result = evaluate(plan, [replace(snapshot, data=bytes(data))])
        table, = result.tables
        self.assertEqual(table.offsets, (0, 1, 2, 9, 11))
        self.assertEqual([(use.offset, use.count, use.type) for use in table.uses], [(0, 3, 0), (9, 1, 0), (11, 1, 0)])
        self.assertEqual(table.count, 12)

    def test_family7_space3_uses_only_its_exact_208_byte_selector(self):
        plan = self.family(7)
        snapshot = self.inputs(plan, 3)[0]
        data = bytearray(snapshot.data)
        struct.pack_into('<I', data, 208, 17)
        result = evaluate(plan, [replace(snapshot, data=bytes(data))])
        self.assertEqual([(table.key.parameter, table.key.space, table.offsets, table.count) for table in result.tables],
                         [(31, 1, (3,), 4), (33, 3, (17,), 18)])

    def test_no_permission_upgrade_from_selected_known_slots(self):
        report = evaluate(self.family(10), self.inputs(self.family(10))).report()
        self.assertTrue(report['selectorEvaluationCompleteForKnownSites'])
        for field in ('selectorByteCaptureEpochIndependentlyVerified', 'completeShaderInputsProved',
                      'inputContentsFrozen', 'copyPermission', 'captureExtentAdmitted', 'nativeExecutionPermission',
                      'nativeExecutionPermitted', 'performanceGainProved'):
            self.assertIs(report[field], False)
        self.assertEqual((report['nativeSuppression'], report['nativeCommandsSuppressed']), (0, 0))

    def test_unknown_operation_and_empty_plan_fail_closed(self):
        with self.assertRaises(EvidenceMismatch):
            U32Operation('and', 32767).apply(0xFFFFFFFF)
        with self.assertRaises(EvidenceMismatch):
            U32Operation('add', False).apply(0)
        with self.assertRaises(EvidenceMismatch):
            evaluate(replace(self.family(1), sites=()), [])

    def test_nonzero_original_table_offset_is_not_lost_or_inferred_as_shader_index(self):
        plan = self.family(1)
        shifted = replace(plan, sites=tuple(replace(site, table=replace(site.table, original_table_offset=9)) for site in plan.sites))
        result = evaluate(shifted, self.inputs(shifted, 7))
        self.assertTrue(all(entry.shader_index == 7 and entry.descriptor_offset == 16 for entry in result.selections))
        table, = result.tables
        self.assertEqual((table.offsets, table.count), ((16,), 17))

    def test_fixture_plans_operations_and_snapshots_are_immutable(self):
        plan = self.family(1)
        snapshot = self.inputs(plan)[0]
        for value, field, changed in ((plan, 'sites', ()), (plan.sites[0], 'byte_offset', 0),
                                      (plan.sites[0].operations[0], 'operand', 32767), (snapshot, 'data', b'')):
            with self.assertRaises(FrozenInstanceError):
                setattr(value, field, changed)


@unittest.skipUnless(REAL_ARTIFACTS_AVAILABLE, 'Original DXIL audit artifacts absent; hermetic evaluator tests still run')
class SourceSelectorArtifactIntegrationTest(unittest.TestCase):
    def test_actual_14_family_23_shader_catalog_and_known_offsets(self):
        evidence = load_evidence(BOUNDS_PATH, REFLECTION_PATH)
        self.assertEqual(len(evidence.families), 14)
        self.assertEqual(sum(plan.supported for plan in evidence.families), 13)
        self.assertEqual(sum(len(plan.sites) for plan in evidence.families), 98)
        fixture = hermetic_catalog()
        for plan in evidence.families:
            expected = next(model for model in fixture.families if model.number == plan.number)
            self.assertEqual(plan.supported, expected.supported)
            self.assertEqual(len(plan.unresolved_inputs), len(expected.unresolved_inputs))
            def cells(model):
                return sorted((int(site.cbv.visibility), site.cbv.shader_register, site.cbv.space, site.cbv.parameter,
                               site.cbv.table_offset, site.byte_offset, site.table.parameter, site.table.space)
                              for site in model.sites)
            self.assertEqual(cells(plan), cells(expected))
            if plan.supported:
                inputs = []
                for key in plan.required_cbvs:
                    data = bytearray(plan.required_byte_extent(key))
                    for site in plan.sites:
                        if site.cbv == key:
                            struct.pack_into('<I', data, site.byte_offset, 7)
                    inputs.append(OwnedCbvBytes(key, bytes(data)))
                result = evaluate(plan, inputs)
                self.assertEqual(len(result.selections), len(plan.sites))
                self.assertTrue(all(entry.shader_index == 7 for entry in result.selections))
            else:
                with self.assertRaises(UnsupportedFamily):
                    evaluate(plan, [])

    def test_original_ir_artifact_hash_and_operation_tampering_fail_closed(self):
        for field, value in (('irTextSha256', '0' * 64), ('operation', 'and i32 %182, 32767')):
            with tempfile.TemporaryDirectory() as temporary:
                bounds = json.loads(BOUNDS_PATH.read_text(encoding='utf-8'))
                shader = next(shader for shader in bounds['shaders'] if shader['selectors'] and shader['stage'] == 'ps')
                if field == 'operation':
                    shader['selectors'][0]['chain'][0]['rhs'] = value
                else:
                    shader[field] = value
                changed = Path(temporary) / 'bounds.json'
                changed.write_text(json.dumps(bounds), encoding='utf-8')
                with self.assertRaises(EvidenceMismatch):
                    load_evidence(changed, REFLECTION_PATH)


if __name__ == '__main__':
    unittest.main()
