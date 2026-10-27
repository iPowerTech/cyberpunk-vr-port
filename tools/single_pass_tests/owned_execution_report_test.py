import copy
import json
import unittest

import owned_execution_report as report
from run_launch_tests import validate_owned_reference_repeat


def complete_receipt():
    stats = [4, 6087, 1, 2, 0x100000, 0x200000, 0x300000, 0x400000,
             155, 928, 352, 72, 7, 0x123456789, 1, 1, 0x500000,
             1, 1, 1, 1, 2, 0, 0]
    resources = [3, 0x600000, 0x700000, stats[4], stats[5], stats[13],
                 0x987654321, 1, report.COMPILER_RETURN_RVA, report.COMPILER_MODE,
                 report.COMPILED_POOL_STATE, 0]
    return stats, resources


def complete_compiler(stats, resources):
    return [stats[1], 0xa00000, 1, 1, 1, 1, 1, 1, 0, 0xb00000,
            resources[2], resources[1], stats[6], stats[5], stats[2], stats[3]]


class OwnedExecutionReportTest(unittest.TestCase):
    def test_reference_schedule_requires_actual_graph_and_context_prefix(self):
        from whole_gbuffer_report_test import fixture,BASE
        from whole_gbuffer_report import _prefix
        from resource_order_report import graph
        from graph_parent_report import fingerprint
        data=fixture();nodes,edges=data[3:5];parsed,_,_=graph(nodes,edges,BASE)
        cpu={(e['source'],e['target']) for e in parsed['edges'] if not e['phase']}
        _,_,required,_=_prefix(parsed['nodes'],cpu,{n['id']:n['phaseOrder'][1] for n in parsed['nodes']},BASE)
        stats,resources=complete_receipt();stats[8:10]=[10,18]
        stats[13]=resources[5]=resources[6]=fingerprint(nodes,edges,BASE)
        schedule=[1,stats[1],stats[13],stats[13],10,18,18,0,0,required,required,0,0,0,1,1]
        def check(v):return report.decode_schedule(v,stats,resources,nodes,edges,nodes,edges,BASE,bindings=nodes)
        self.assertEqual(check(schedule)['mode'],'reference')
        for field in range(16):
            invalid=schedule.copy();invalid[field]+=1
            with self.subTest(field=field),self.assertRaises(ValueError):check(invalid)
        changed=bytearray(nodes);changed[16]^=1
        with self.assertRaises(ValueError):report.decode_schedule(schedule,stats,resources,bytes(changed),edges,nodes,edges,BASE,bindings=nodes)
        with self.assertRaises(ValueError):report.decode_schedule(schedule,stats,resources,nodes,edges,nodes,edges,BASE,bindings=nodes,coupled=True)

    def assert_rejected(self, stats, resources, **kwargs):
        with self.assertRaises(ValueError):
            report.decode(stats, resources, **kwargs)

    def test_complete_cpu_reference_receipt(self):
        stats, resources = complete_receipt()
        result = report.decode(stats, resources, expected_epoch=1, expected_pair=2)
        self.assertEqual(result['consumerId'], stats[1])
        self.assertEqual(result['sourceGraph'], hex(stats[4]))
        self.assertEqual(result['executionGraph'], hex(stats[5]))
        self.assertEqual(result['protectedOwners'], 2)
        self.assertEqual(result['resources']['compilerReturnRva'], '0xaf5a15')
        self.assertTrue(result['cpuExecutionProved'])
        for name in ('gpuLifetimeProved', 'wholeSceneSinglePassProved', 'performanceGainProved'):
            self.assertFalse(result[name])
        self.assertEqual(json.loads(json.dumps(result)), result)

    def test_decoder_does_not_modify_input(self):
        stats, resources = complete_receipt()
        before = (list(stats), list(resources))
        report.decode(stats, resources)
        self.assertEqual((stats, resources), before)

    def test_tuple_arrays_are_supported(self):
        stats, resources = complete_receipt()
        self.assertEqual(report.decode(tuple(stats), tuple(resources))['status'], 'complete')

    def test_exact_abi_counts(self):
        stats, resources = complete_receipt()
        for malformed in (stats[:-1], stats + [0], [], None, '0' * 24, bytes(24)):
            with self.subTest(stats=malformed):
                self.assert_rejected(malformed, resources)
        for malformed in (resources[:-1], resources + [0], [], None, '0' * 12, bytes(12)):
            with self.subTest(resources=malformed):
                self.assert_rejected(stats, malformed)

    def test_all_fields_are_exact_uint64(self):
        for which, count in ((0, report.STATS_WORDS), (1, report.RESOURCE_WORDS)):
            for index in range(count):
                for invalid in (-1, 1 << 64, True, 1.0, '1', None):
                    stats, resources = complete_receipt()
                    (stats, resources)[which][index] = invalid
                    with self.subTest(array=which, index=index, invalid=invalid):
                        self.assert_rejected(stats, resources)

    def test_only_complete_execution_status_is_accepted(self):
        for status in (0, 1, 2, 3, 5, 6, 7, report.UINT64_MAX):
            stats, resources = complete_receipt()
            stats[0] = status
            with self.subTest(status=status):
                self.assert_rejected(stats, resources)

    def test_only_fresh_compiled_resource_state_is_accepted(self):
        for state in (0, 1, 2, 4, 5, report.UINT64_MAX):
            stats, resources = complete_receipt()
            resources[0] = state
            with self.subTest(state=state):
                self.assert_rejected(stats, resources)

    def test_failure_and_detail_cannot_hide_behind_complete_status(self):
        for which, index in ((0, 22), (0, 23), (1, 11)):
            stats, resources = complete_receipt()
            (stats, resources)[which][index] = 1
            with self.subTest(array=which, index=index):
                self.assert_rejected(stats, resources)

    def test_missing_identity(self):
        for index in (1, 2, 3, 4, 5, 6, 7, 13, 16, 21):
            stats, resources = complete_receipt()
            stats[index] = 0
            with self.subTest(index=index):
                self.assert_rejected(stats, resources)

    def test_expected_epoch_and_pair_must_match(self):
        stats, resources = complete_receipt()
        for name, wrong_values in (('expected_epoch', (0, 2, -1, True, 1.0, '1', 1 << 64)),
                                   ('expected_pair', (0, 1, -1, True, 2.0, '2', 1 << 64))):
            for wrong in wrong_values:
                with self.subTest(name=name, wrong=wrong):
                    self.assert_rejected(stats, resources, **{name: wrong})

    def test_source_execution_alias_is_not_an_owned_execution(self):
        stats, resources = complete_receipt()
        stats[5] = stats[4]
        resources[4] = stats[4]
        self.assert_rejected(stats, resources)

    def test_declaration_execution_terminal_alias_is_rejected(self):
        stats, resources = complete_receipt()
        stats[16] = stats[7]
        self.assert_rejected(stats, resources)

    def test_unaligned_or_absent_resource_pointers(self):
        for index in (1, 2):
            for invalid in (0, 0x100001):
                stats, resources = complete_receipt()
                resources[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(stats, resources)

    def test_unaligned_graph_owner_and_terminal_pointers(self):
        for index in (4, 5, 6, 7, 16):
            stats, resources = complete_receipt()
            stats[index] += 1
            if index == 4:
                resources[3] = stats[4]
            if index == 5:
                resources[4] = stats[5]
            with self.subTest(index=index):
                self.assert_rejected(stats, resources)

    def test_every_cpu_proof_flag_is_exactly_one(self):
        for index in (14, 15, 17, 18, 19, 20):
            for invalid in (0, 2, report.UINT64_MAX):
                stats, resources = complete_receipt()
                stats[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(stats, resources)

    def test_graph_and_tree_counts_are_bounded(self):
        invalid_fields = ((8, 0), (8, report.MAX_GRAPH_NODES + 1),
                          (9, report.MAX_GRAPH_EDGES + 1), (10, 0),
                          (10, report.MAX_TREE_NODES + 1), (11, 353),
                          (12, 353), (21, report.MAX_PROTECTED_OWNERS + 1))
        for index, invalid in invalid_fields:
            stats, resources = complete_receipt()
            stats[index] = invalid
            with self.subTest(index=index, invalid=invalid):
                self.assert_rejected(stats, resources)

    def test_owned_array_and_string_inventory_cannot_exceed_tree(self):
        stats, resources = complete_receipt()
        stats[11], stats[12] = 350, 3
        self.assert_rejected(stats, resources)

    def test_valid_zero_edges_arrays_and_strings(self):
        stats, resources = complete_receipt()
        stats[8:13] = [1, 0, 1, 0, 0]
        stats[21] = 1
        result = report.decode(stats, resources)
        self.assertEqual((result['edges'], result['arrays'], result['strings']), (0, 0, 0))

    def test_valid_capture_limits(self):
        stats, resources = complete_receipt()
        stats[8:13] = [report.MAX_GRAPH_NODES, report.MAX_GRAPH_EDGES,
                       report.MAX_TREE_NODES, report.MAX_TREE_NODES, 0]
        stats[21] = report.MAX_PROTECTED_OWNERS
        self.assertEqual(report.decode(stats, resources)['protectedOwners'], report.MAX_PROTECTED_OWNERS)

    def test_torn_resource_bindings(self):
        for index in (3, 4, 5):
            stats, resources = complete_receipt()
            resources[index] += 8
            with self.subTest(index=index):
                self.assert_rejected(stats, resources)

    def test_execution_resource_fingerprint_is_required(self):
        stats, resources = complete_receipt()
        resources[6] = 0
        self.assert_rejected(stats, resources)

    def test_reference_semantic_fingerprint_may_match_source(self):
        stats, resources = complete_receipt()
        resources[6] = resources[5]
        self.assertEqual(report.decode(stats, resources)['resources']['executionFingerprint'], hex(stats[13]))

    def test_resource_declaration_flag_is_exactly_one(self):
        for invalid in (0, 2):
            stats, resources = complete_receipt()
            resources[7] = invalid
            with self.subTest(invalid=invalid):
                self.assert_rejected(stats, resources)

    def test_unverified_compiler_caller_mode_and_pool_state(self):
        for index in (8, 9, 10):
            for invalid in (0, 1, report.UINT64_MAX):
                stats, resources = complete_receipt()
                resources[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(stats, resources)


def complete_repeat():
    stats, resources = complete_receipt()
    first = dict(passed=True, pid=10304, dll='reference-build',
                 ownedExecution=dict(stats=stats, resources=resources, compiler=complete_compiler(stats, resources),
                                     sequence=12, closing=0))
    second = copy.deepcopy(first)
    second['ownedExecution']['stats'][1:4] = [stats[1] + 1, stats[2] + 1, stats[3] + 1]
    second['ownedExecution']['compiler'] = complete_compiler(second['ownedExecution']['stats'], second['ownedExecution']['resources'])
    second['ownedExecution']['sequence'] = 14
    return first, second


class OwnedReferenceRepeatTest(unittest.TestCase):
    def assert_rejected(self, first, second):
        with self.assertRaises(ValueError):
            validate_owned_reference_repeat(first, second)

    def test_complete_repeat_allows_same_allocations(self):
        first, second = complete_repeat()
        self.assertIsNone(validate_owned_reference_repeat(first, second))
        for index in (4, 5, 6, 7, 16):
            self.assertEqual(first['ownedExecution']['stats'][index], second['ownedExecution']['stats'][index])
        self.assertEqual(first['ownedExecution']['resources'], second['ownedExecution']['resources'])

    def test_repeat_does_not_mutate_receipts(self):
        first, second = complete_repeat()
        before = copy.deepcopy((first, second))
        validate_owned_reference_repeat(first, second)
        self.assertEqual((first, second), before)

    def test_process_or_build_change_is_rejected(self):
        for name, invalid in (('pid', 12345), ('dll', 'different-build')):
            for which in (0, 1):
                captures = complete_repeat()
                captures[which][name] = invalid
                with self.subTest(field=name, capture=which):
                    self.assert_rejected(*captures)

    def test_missing_process_or_build_identity_is_rejected(self):
        for name in ('pid', 'dll'):
            for which in (0, 1, 2):
                first, second = complete_repeat()
                for index in ((0, 1) if which == 2 else (which,)):
                    (first, second)[index].pop(name)
                with self.subTest(field=name, capture=which):
                    self.assert_rejected(first, second)

    def test_process_id_is_a_positive_exact_integer_in_both_runs(self):
        for which in (0, 1):
            for invalid in (0, -1, True, 10304.0, '10304', None):
                first, second = complete_repeat()
                (first, second)[which]['pid'] = invalid
                with self.subTest(capture=which, invalid=invalid):
                    self.assert_rejected(first, second)

    def test_build_identity_must_be_a_nonempty_string(self):
        for invalid in ('', None, 1234, True):
            first, second = complete_repeat()
            first['dll'] = second['dll'] = invalid
            with self.subTest(invalid=invalid):
                self.assert_rejected(first, second)

    def test_both_runs_must_have_passed(self):
        for which in (0, 1):
            for invalid in (False, 1, 'passed', None):
                first, second = complete_repeat()
                (first, second)[which]['passed'] = invalid
                with self.subTest(capture=which, invalid=invalid):
                    self.assert_rejected(first, second)

    def test_epoch_must_advance_exactly_once(self):
        first, second = complete_repeat()
        before = first['ownedExecution']['stats'][2]
        for epoch in (0, before, before + 2, 0xffffffff):
            bad = copy.deepcopy(second)
            bad['ownedExecution']['stats'][2] = epoch
            bad['ownedExecution']['compiler'][14] = epoch
            with self.subTest(epoch=epoch):
                self.assert_rejected(first, bad)

    def test_epoch_wrap_skips_zero(self):
        first, second = complete_repeat()
        first['ownedExecution']['stats'][2] = 0xffffffff
        second['ownedExecution']['stats'][2] = 1
        first['ownedExecution']['compiler'][14] = 0xffffffff
        second['ownedExecution']['compiler'][14] = 1
        self.assertIsNone(validate_owned_reference_repeat(first, second))
        for invalid in (0, 2, 0xffffffff):
            bad = copy.deepcopy(second)
            bad['ownedExecution']['stats'][2] = invalid
            bad['ownedExecution']['compiler'][14] = invalid
            with self.subTest(epoch=invalid):
                    self.assert_rejected(first, bad)

    def test_epoch_cannot_exceed_uint32_in_either_run(self):
        for which in (0, 1):
            first, second = complete_repeat()
            (first, second)[which]['ownedExecution']['stats'][2] += 1 << 32
            (first, second)[which]['ownedExecution']['compiler'][14] += 1 << 32
            with self.subTest(capture=which):
                self.assert_rejected(first, second)

    def test_consumer_must_be_fresh_and_monotonic(self):
        first, second = complete_repeat()
        before = first['ownedExecution']['stats'][1]
        for consumer in (0, before - 1, before):
            bad = copy.deepcopy(second)
            bad['ownedExecution']['stats'][1] = consumer
            bad['ownedExecution']['compiler'][0] = consumer
            with self.subTest(consumer=consumer):
                self.assert_rejected(first, bad)

    def test_pair_generation_must_be_nonzero_and_fresh(self):
        first, second = complete_repeat()
        for pair in (0, first['ownedExecution']['stats'][3]):
            bad = copy.deepcopy(second)
            bad['ownedExecution']['stats'][3] = pair
            bad['ownedExecution']['compiler'][15] = pair
            with self.subTest(pair=pair):
                self.assert_rejected(first, bad)

    def test_wrapped_pair_generation_can_be_fresh(self):
        first, second = complete_repeat()
        first['ownedExecution']['stats'][3] = 0xffffffff
        second['ownedExecution']['stats'][3] = 1
        first['ownedExecution']['compiler'][15] = 0xffffffff
        second['ownedExecution']['compiler'][15] = 1
        self.assertIsNone(validate_owned_reference_repeat(first, second))

    def test_pair_generation_retains_uint64_precision(self):
        first, second = complete_repeat()
        first['ownedExecution']['stats'][3] = (1 << 40) + 1
        second['ownedExecution']['stats'][3] = (1 << 40) + 2
        first['ownedExecution']['compiler'][15] = (1 << 40) + 1
        second['ownedExecution']['compiler'][15] = (1 << 40) + 2
        self.assertIsNone(validate_owned_reference_repeat(first, second))

    def test_both_runs_require_complete_execution_status(self):
        for which in (0, 1):
            for status in (0, 1, 2, 3, 5, 6):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['stats'][0] = status
                with self.subTest(capture=which, status=status):
                    self.assert_rejected(first, second)

    def test_each_run_requires_all_exact_cpu_completion_proofs(self):
        for which in (0, 1):
            for index in (14, 15, 17, 18, 19, 20):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['stats'][index] = 0
                with self.subTest(capture=which, proof=index):
                    self.assert_rejected(first, second)

    def test_each_run_requires_fresh_resource_compilation(self):
        for which in (0, 1):
            for state in (0, 1, 2, 4):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['resources'][0] = state
                with self.subTest(capture=which, state=state):
                    self.assert_rejected(first, second)

    def test_each_run_requires_exact_resource_binding(self):
        for which in (0, 1):
            for index in (3, 4, 5):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['resources'][index] += 8
                with self.subTest(capture=which, binding=index):
                    self.assert_rejected(first, second)

    def test_sticky_closing_in_either_run_is_rejected(self):
        for which in (0, 1):
            for closing in (1, 2):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['closing'] = closing
                with self.subTest(capture=which, closing=closing):
                    self.assert_rejected(first, second)

    def test_odd_sequence_in_either_run_is_rejected(self):
        for which in (0, 1):
            for sequence in (1, 13, 0xffffffff):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['sequence'] = sequence
                with self.subTest(capture=which, sequence=sequence):
                    self.assert_rejected(first, second)

    def test_even_sequence_wrap_is_valid(self):
        first, second = complete_repeat()
        first['ownedExecution']['sequence'] = 0xfffffffe
        second['ownedExecution']['sequence'] = 0
        self.assertIsNone(validate_owned_reference_repeat(first, second))

    def test_sequence_is_an_exact_uint32_in_both_runs(self):
        for which in (0, 1):
            for invalid in (-2, 1 << 32, True, 12.0, '12', None):
                first, second = complete_repeat()
                (first, second)[which]['ownedExecution']['sequence'] = invalid
                with self.subTest(capture=which, invalid=invalid):
                    self.assert_rejected(first, second)


class OwnedCompilerReportTest(unittest.TestCase):
    def assert_rejected(self, compiler, stats, resources):
        with self.assertRaises(ValueError):
            report.decode_compiler(compiler, stats, resources)

    def test_complete_bound_compiler_terminal(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        result = report.decode_compiler(compiler, stats, resources)
        self.assertEqual(result['consumerId'], stats[1])
        self.assertEqual(result['epoch'], stats[2])
        self.assertEqual(result['pairGeneration'], stats[3])
        self.assertEqual(result['group'], hex(compiler[1]))
        self.assertEqual(result['renderer'], hex(compiler[9]))
        self.assertEqual(result['wrapper'], hex(resources[2]))
        self.assertEqual(result['pool'], hex(resources[1]))
        self.assertEqual(result['frameOwner'], hex(stats[6]))
        self.assertEqual(result['executionGraph'], hex(stats[5]))
        self.assertTrue(result['terminalCompletionProved'])
        for name in ('gpuLifetimeProved', 'wholeSceneSinglePassProved', 'performanceGainProved'):
            self.assertFalse(result[name])
        self.assertEqual(json.loads(json.dumps(result)), result)

    def test_compiler_decoder_does_not_modify_input(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        before = copy.deepcopy((compiler, stats, resources))
        report.decode_compiler(compiler, stats, resources)
        self.assertEqual((compiler, stats, resources), before)

    def test_tuple_arrays_are_supported(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        self.assertTrue(report.decode_compiler(tuple(compiler), tuple(stats), tuple(resources))['terminalCompletionProved'])

    def test_compiler_word_count_is_exact(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        for invalid in (None, [], compiler[:-1], compiler + [0], '0' * 16, bytes(16)):
            with self.subTest(invalid=invalid):
                self.assert_rejected(invalid, stats, resources)

    def test_every_compiler_word_is_exact_uint64(self):
        for index in range(report.COMPILER_WORDS):
            for invalid in (-1, 1 << 64, True, 1.0, '1', None):
                stats, resources = complete_receipt()
                compiler = complete_compiler(stats, resources)
                compiler[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(compiler, stats, resources)

    def test_each_compiler_flag_is_exactly_one(self):
        for index in range(2, 8):
            for invalid in (0, 2, report.UINT64_MAX):
                stats, resources = complete_receipt()
                compiler = complete_compiler(stats, resources)
                compiler[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(compiler, stats, resources)

    def test_rejection_cannot_hide_behind_complete_flags(self):
        for rejection in (1, report.UINT64_MAX):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            compiler[8] = rejection
            with self.subTest(rejection=rejection):
                self.assert_rejected(compiler, stats, resources)

    def test_same_pool_is_not_enough_for_foreign_consumer_or_frame(self):
        for index in (0, 12, 13, 14, 15):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            compiler[index] += 8
            self.assertEqual(compiler[11], resources[1])
            with self.subTest(foreign_binding=index):
                self.assert_rejected(compiler, stats, resources)

    def test_missing_or_stale_bound_identity_is_rejected(self):
        for index in (0, 10, 11, 12, 13, 14, 15):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            compiler[index] = 0
            with self.subTest(index=index):
                self.assert_rejected(compiler, stats, resources)

    def test_wrapper_and_pool_match_the_exact_resource_receipt(self):
        for index in (10, 11):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            compiler[index] += 8
            with self.subTest(index=index):
                self.assert_rejected(compiler, stats, resources)

    def test_d1_and_renderer_are_positive_aligned_identities(self):
        for index in (1, 9):
            for invalid in (0, 0xa00001, report.UINT64_MAX):
                stats, resources = complete_receipt()
                compiler = complete_compiler(stats, resources)
                compiler[index] = invalid
                with self.subTest(index=index, invalid=invalid):
                    self.assert_rejected(compiler, stats, resources)

    def test_d1_may_be_the_nonrotated_d0(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        compiler[1] = stats[16]
        result = report.decode_compiler(compiler, stats, resources)
        self.assertEqual(result['group'], hex(stats[16]))

    def test_d1_has_no_unproven_execution_group_inequality_requirement(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        compiler[1] = stats[7]
        self.assertEqual(report.decode_compiler(compiler, stats, resources)['group'], hex(stats[7]))

    def test_complete_compiler_cannot_override_failed_owned_execution(self):
        for index, invalid in ((0, 3), (14, 0), (15, 0), (17, 0), (18, 0), (19, 0), (20, 0), (21, 0), (22, 1)):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            stats[index] = invalid
            with self.subTest(index=index, invalid=invalid):
                self.assert_rejected(compiler, stats, resources)

    def test_complete_compiler_cannot_override_unverified_resources(self):
        for index, invalid in ((0, 2), (4, 0x800000), (5, 0), (6, 0), (7, 0), (8, 0), (9, 1), (10, 0), (11, 1)):
            stats, resources = complete_receipt()
            compiler = complete_compiler(stats, resources)
            resources[index] = invalid
            with self.subTest(index=index, invalid=invalid):
                self.assert_rejected(compiler, stats, resources)

    def test_stats_and_resources_keep_their_own_abi_validation(self):
        stats, resources = complete_receipt()
        compiler = complete_compiler(stats, resources)
        for malformed in (None, stats[:-1], stats + [0], ['bad'] * 24):
            with self.subTest(stats=malformed):
                self.assert_rejected(compiler, malformed, resources)
        for malformed in (None, resources[:-1], resources + [0], ['bad'] * 12):
            with self.subTest(resources=malformed):
                self.assert_rejected(compiler, stats, malformed)

    def test_compiler_binding_preserves_uint64_pair_and_consumer(self):
        stats, resources = complete_receipt()
        stats[1] = (1 << 50) + 3
        stats[3] = (1 << 40) + 2
        compiler = complete_compiler(stats, resources)
        result = report.decode_compiler(compiler, stats, resources)
        self.assertEqual(result['consumerId'], stats[1])
        self.assertEqual(result['pairGeneration'], stats[3])


if __name__ == '__main__':
    unittest.main()
