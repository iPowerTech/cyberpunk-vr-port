"""Hermetic package checks and read-only original-evidence integration."""

import copy
from dataclasses import replace
import json
from pathlib import Path
import tempfile
import unittest

from package_source_selectors import MAX_BYTES, PURPOSE, build_package, package, read_json, serialize
from source_selector_report import CbvKey, EvidenceCatalog, U32Operation, Visibility
from source_selector_report_test import hermetic_catalog


def fixture():
    original = hermetic_catalog()
    plans = tuple(replace(plan, sites=tuple(replace(site, create_handle_ssa=f'%{100 + index}',
                                                  index_ssa=f'%{1000 + index}')
                                            for index, site in enumerate(plan.sites))) for plan in original.families)
    catalog = EvidenceCatalog(plans, 'f' * 64)
    shaders = {}
    for plan in plans:
        for stage, sha in (('vs', plan.key.vs_sha256), ('ps', plan.key.ps_sha256)):
            shaders[sha] = dict(sha256=sha, stage=stage,
                                resources=[dict(type=2, space=0, register=4, count=1, bytes=256)])
    return catalog, dict(shaders=shaders)


def changed_site(catalog, **changes):
    plan = catalog.families[0]
    site = replace(plan.sites[0], **changes)
    return replace(catalog, families=(replace(plan, sites=(site, *plan.sites[1:])), *catalog.families[1:]))


class PackageSourceSelectorsTest(unittest.TestCase):
    def reject(self, catalog, reflection):
        with self.assertRaises((ValueError, TypeError)):
            build_package(catalog, reflection)

    def test_all_families_exact_qualified_sites_and_no_execution_fields(self):
        catalog, reflection = fixture()
        result = build_package(catalog, reflection)
        self.assertEqual(set(result), {'schema', 'purpose', 'evidenceSha256', 'families'})
        self.assertEqual(result['purpose'], PURPOSE)
        self.assertEqual(len(result['families']), 14)
        self.assertEqual(sum(family['supported'] for family in result['families']), 13)
        self.assertEqual(sum(len(family['sites']) for family in result['families']), 98)
        self.assertLess(len(serialize(result)), MAX_BYTES)
        self.assertEqual(json.loads(serialize(result)), result)
        for family in result['families']:
            for requirement in family['cbvs']:
                self.assertEqual(requirement['root'], family['root'])
                self.assertEqual((requirement['register'], requirement['space'], requirement['tableOffset']), (4, 0, 0))
                self.assertEqual(requirement['reflectedBytes'], 256)
                self.assertEqual(requirement['requiredByteExtent'], max(requirement['dwordOffsets']) + 4)
            for site in family['sites']:
                self.assertEqual(site['operations'], [dict(opcode='add', operand=0)])
                self.assertEqual(site['table']['root'], site['cbv']['root'])
                self.assertEqual(site['table']['visibility'], site['cbv']['visibility'])
                self.assertEqual(site['table']['declaredExtent'], 32768)

    def test_multilayer_stays_explicitly_unsupported(self):
        catalog, reflection = fixture()
        family = build_package(catalog, reflection)['families'][12]
        self.assertFalse(family['supported'])
        self.assertEqual(family['sites'], [])
        self.assertEqual(family['cbvs'], [])
        self.assertTrue(family['unsupportedReason'])
        self.assertEqual(len(family['unresolvedInputs']), 10)
        plans = list(catalog.families)
        plans[12] = replace(plans[12], sites=catalog.families[0].sites)
        self.reject(replace(catalog, families=tuple(plans)), reflection)
        plans = list(catalog.families)
        plans[0] = replace(plans[0], unresolved_inputs=('t5 missing',))
        self.reject(replace(catalog, families=tuple(plans)), reflection)

    def test_hashes_duplicate_families_numbers_sites_and_reflection(self):
        catalog, reflection = fixture()
        for digest in ('', 'f' * 63, 'F' * 64, 'g' * 64):
            self.reject(replace(catalog, evidence_sha256=digest), reflection)
        self.reject(replace(catalog, families=(catalog.families[0], *catalog.families)), reflection)
        plans = list(catalog.families)
        plans[1] = replace(plans[1], number=plans[0].number)
        self.reject(replace(catalog, families=tuple(plans)), reflection)
        plans = list(catalog.families)
        plans[0] = replace(plans[0], sites=(*plans[0].sites, plans[0].sites[0]))
        self.reject(replace(catalog, families=tuple(plans)), reflection)
        duplicate = copy.deepcopy(reflection)
        duplicate['shaders']['duplicate'] = next(iter(duplicate['shaders'].values()))
        self.reject(catalog, duplicate)
        self.reject(changed_site(catalog, shader_sha256=catalog.families[0].key.vs_sha256), reflection)

    def test_original_cbv_stage_slot_root_and_table_coordinates(self):
        catalog, reflection = fixture()
        site = catalog.families[0].sites[0]
        for changes in (dict(root_sha256='b' * 64), dict(visibility=1), dict(shader_register=5),
                        dict(space=1), dict(parameter=1), dict(table_offset=1)):
            self.reject(changed_site(catalog, cbv=replace(site.cbv, **changes)), reflection)
        for changes in (dict(root_sha256='b' * 64), dict(visibility=Visibility.VS), dict(parameter=13),
                        dict(space=3), dict(shader_register_base=1), dict(original_table_offset=1), dict(declared_extent=32769)):
            self.reject(changed_site(catalog, table=replace(site.table, **changes)), reflection)

    def test_only_original_add0_ssa_and_integer_dword_loads(self):
        catalog, reflection = fixture()
        for operation in (U32Operation('add', 1), U32Operation('and', 0), U32Operation('add', False)):
            self.reject(changed_site(catalog, operations=(operation,)), reflection)
        self.reject(changed_site(catalog, operations=()), reflection)
        self.reject(changed_site(catalog, operations=(U32Operation('add', 0), U32Operation('add', 0))), reflection)
        for changes in (dict(create_handle_ssa='%named'), dict(index_ssa='%named'), dict(create_handle_line=0),
                        dict(create_handle_line=True), dict(byte_offset=1), dict(byte_offset=-4),
                        dict(byte_offset=65532), dict(byte_offset=0.0), dict(byte_offset=False)):
            self.reject(changed_site(catalog, **changes), reflection)

    def test_exact_original_reflected_size_and_capacity(self):
        catalog, reflection = fixture()
        sha = catalog.families[0].key.ps_sha256
        for size in (0, 179, 65537, True, 256.0):
            matrix = copy.deepcopy(reflection)
            matrix['shaders'][sha]['resources'][0]['bytes'] = size
            self.reject(catalog, matrix)
        matrix = copy.deepcopy(reflection)
        matrix['shaders'][sha]['resources'].append(matrix['shaders'][sha]['resources'][0].copy())
        self.reject(catalog, matrix)
        matrix = copy.deepcopy(reflection)
        matrix['shaders'][sha]['resources'][0]['register'] = 4.0
        self.reject(catalog, matrix)
        self.reject(replace(catalog, families=()), reflection)
        self.reject(replace(catalog, families=catalog.families * 5), reflection)
        plans = list(catalog.families)
        plans[0] = replace(plans[0], sites=plans[0].sites * 52)
        self.reject(replace(catalog, families=tuple(plans)), reflection)

    def test_source_json_duplicates_and_nonfinite_numbers_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'evidence.json'
            for raw in ('{"same":1,"same":2}', '{"value":NaN}', '{"value":Infinity}', '{bad}'):
                path.write_text(raw)
                with self.assertRaises(ValueError):
                    read_json(path)
            path.write_text('{"field":1}')
            self.assertEqual(read_json(path), {'field': 1})

    def test_read_only_actual_original_evidence_package(self):
        root = Path(__file__).resolve().parents[2] / 'build' / 'single-pass-20261005'
        bounds, reflection = root / 'source-selector-bounds-audit' / 'bounds.json', root / 'source-table-reflection-audit' / 'matrix.json'
        if not bounds.is_file() or not reflection.is_file():
            self.skipTest('Original evidence artifacts unavailable')
        result = package(bounds, reflection)
        self.assertEqual(len(result['families']), 14)
        self.assertEqual(sum(family['supported'] for family in result['families']), 13)
        self.assertEqual(sum(len(family['sites']) for family in result['families']), 98)
        self.assertEqual([family['number'] for family in result['families'] if not family['supported']], [13])
        self.assertEqual(result['families'][11]['cbvs'][0]['reflectedBytes'], 292)
        self.assertEqual(result['families'][6]['cbvs'][0]['reflectedBytes'], 228)


if __name__ == '__main__':
    unittest.main()
