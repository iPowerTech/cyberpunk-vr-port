import copy
import json
import unittest

import retirement_report as report


CONSUMER, EPOCH, PAIR = 1002, 3, 4
HEADER, GENERATION, BORN = 0x500000, 41, 8
CACHE_BASE, RENDERER = 0x400000, 0x300000


def event(seq, kind, *, old=False, complete=False):
    row = [seq, kind, HEADER, GENERATION, BORN, 0x600000, 8, 4,
           1001 if old else CONSUMER, 2 if old else EPOCH, 3 if old else PAIR,
           7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15]
    if kind == 2:
        row[19:21] = [0 if old else 1, 1]
    if complete:
        row[16:19] = [0x810000 if old else 0x800000, 0x910000 if old else 0x900000,
                      0x810000 if old else 0x800000]
        row[19:22] = [1 if old else 2, 0, report.COMPLETION_FLAGS << 32]
    if kind in (5, 6):
        row[13:16] = [HEADER, 777, 88]
        row[22] = 0x983f1c
    if kind in (7, 8, 9):
        row[13:16] = [HEADER + 0x1000, 778, 89]
        row[22] = 0x986e74
    return row


def sample(events=None, keys=5):
    if events is None:
        events = [event(1, 1, old=True), event(2, 2, old=True), event(3, 3, old=True, complete=True),
                  event(4, 2), event(5, 3, complete=True), event(6, 5, complete=True),
                  event(7, 6, complete=True), event(8, 7, complete=True),
                  event(9, 8, complete=True), event(10, 9, complete=True)]
    common = 0x100000000 + report.COMMON_RVA
    allocator = [1, 7, 0, 0, 0, 1, common, common | 5, common | 3, common | 7,
                 common - report.COMMON_RVA + report.LIVE_VPTR_RVA,
                 0x100000, 0x200000, 0x300000, 0x700000, 15, 10, 5, 0, 6, 0, 0, 0, 0x983f1c]
    totals = [1, 2, 2, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, keys, keys]
    cache = [keys, 0, keys, keys, 0, keys, CACHE_BASE, keys, 2, 3, 1, RENDERER, 9999, 0, 0, 0x219aaa]
    rows = [[CACHE_BASE, i, 2, i + 1, i + 1, 0, 1, RENDERER] for i in range(keys)]
    physical = [[0] * 24 for _ in range(report.EVENT_CAPACITY)]
    last = events[-1][0] if events else 0
    for row in events[-report.EVENT_CAPACITY:]:
        physical[(row[0] - 1) % report.EVENT_CAPACITY] = list(row)
    return dict(sequence=22, pid=10304, dll='native-build', Allocator=allocator,
                RetirementTotals=totals, Cache=cache,
                events=dict(sequence=last, count=min(last, report.EVENT_CAPACITY), rows=physical),
                cacheRows=rows + [[0] * 8 for _ in range(report.CACHE_CAPACITY - len(rows))])


def decoded(raw=None):
    result = report.decode(sample() if raw is None else raw)
    result['admission'] = [CONSUMER, EPOCH, PAIR, CACHE_BASE + 0x10, CACHE_BASE, RENDERER]
    return result


def validate(value, **kwargs):
    return report.validate_retirement(value, CONSUMER, EPOCH, PAIR, **kwargs)


def set_event(raw, seq, index, value):
    raw['events']['rows'][(seq - 1) % report.EVENT_CAPACITY][index] = value


class RetirementReportTest(unittest.TestCase):
    def assert_decode_rejected(self, raw):
        with self.assertRaises(ValueError):
            report.decode(raw)

    def assert_retirement_rejected(self, value, **kwargs):
        with self.assertRaises(ValueError):
            validate(value, **kwargs)

    def test_actual_specific_retirement_with_reused_reservation(self):
        value = decoded()
        proof = validate(value)
        self.assertEqual(proof['header'], hex(HEADER))
        self.assertEqual((proof['generation'], proof['born']), (GENERATION, BORN))
        self.assertEqual(proof['reserveConsumerId'], 1001)
        self.assertEqual(proof['consumerId'], CONSUMER)
        self.assertTrue(proof['nativeDestroyReturned'])
        self.assertTrue(proof['metadataRecycled'])
        self.assertTrue(proof['cacheOwnershipCorrelationProved'])
        self.assertFalse(proof['allExactExtents'])
        self.assertTrue(proof['externalFreshnessRequired'])
        self.assertTrue(proof['ownedReportCorrelationRequired'])
        for key in ('processFreshnessProved', 'frameFreshnessProved', 'gpuLifetimeProved', 'performanceGainProved'):
            self.assertFalse(proof[key])
        self.assertEqual(json.loads(json.dumps(proof)), proof)

    def test_count_only_totals_never_prove_native_retirement(self):
        raw = sample(events=[])
        self.assert_retirement_rejected(decoded(raw))

    def test_metadata_clearing_does_not_erase_terminal_receipts(self):
        raw = sample()
        raw['Allocator'][15:20] = [0, 0, 0, 0, 0]
        proof = validate(decoded(raw))
        self.assertTrue(proof['metadataRecycled'])
        self.assertEqual(proof['classifiedAllocations'], 15)

    def test_native_detach_can_precede_current_consumer_completion(self):
        events = [event(1, 1, old=True), event(2, 2), event(3, 5), event(4, 6),
                  event(5, 3, complete=True), event(6, 7, complete=True),
                  event(7, 8, complete=True), event(8, 9, complete=True)]
        events[2][20] = events[3][20] = 1
        self.assertTrue(validate(decoded(sample(events)))['metadataRecycled'])

    def test_d1_can_alias_d0_without_invented_inequality(self):
        self.assertEqual(validate(decoded())['groups']['D0'], validate(decoded())['groups']['D1'])

    def test_target_generation_not_just_same_header_is_required(self):
        for seq in (4, 5, 6, 7, 8, 9, 10):
            for index in (2, 3, 4):
                raw = sample()
                set_event(raw, seq, index, raw['events']['rows'][seq - 1][index] + 8)
                with self.subTest(sequence=seq, field=index):
                    self.assert_retirement_rejected(decoded(raw))

    def test_current_requested_consumer_epoch_pair_cannot_use_old_complete(self):
        for index in (8, 9, 10):
            raw = sample()
            set_event(raw, 5, index, raw['events']['rows'][4][index] - 1)
            with self.subTest(field=index):
                self.assert_retirement_rejected(decoded(raw))

    def test_native_return_and_recycle_preserve_completed_groups_and_tags(self):
        for seq in (8, 9, 10):
            for index in (8, 9, 10, 16, 17, 18):
                raw = sample()
                set_event(raw, seq, index, raw['events']['rows'][seq - 1][index] + 8)
                with self.subTest(sequence=seq, field=index):
                    self.assert_retirement_rejected(decoded(raw))

    def test_missing_flags_pending_readers_and_failed_results_never_succeed(self):
        for seq in (5, 8, 9, 10):
            for index, value in ((19, 0), (20, 1), (21, 0), (21, report.COMPLETION_FLAGS << 32 | 1), (23, 0)):
                raw = sample()
                set_event(raw, seq, index, value)
                with self.subTest(sequence=seq, field=index, value=value):
                    self.assert_retirement_rejected(decoded(raw))

    def test_each_individual_completion_bit_is_required(self):
        for bit in range(9):
            raw = sample()
            set_event(raw, 5, 21, (report.COMPLETION_FLAGS & ~(1 << bit)) << 32)
            with self.subTest(bit=bit):
                self.assert_retirement_rejected(decoded(raw))

    def test_duplicate_destroy_attempt_or_return_cannot_be_retried(self):
        for kind in (7, 8):
            events = [list(row) for row in sample()['events']['rows'][:10]]
            events.append(event(11, kind, complete=True))
            with self.subTest(kind=kind):
                self.assert_retirement_rejected(decoded(sample(events)))

    def test_wrong_order_and_different_actual_native_scope_are_rejected(self):
        raw = sample()
        raw['events']['rows'][7][1], raw['events']['rows'][8][1] = 8, 7
        self.assert_retirement_rejected(decoded(raw))
        for index in (13, 14, 15, 22):
            raw = sample()
            set_event(raw, 9, index, raw['events']['rows'][8][index] + 1)
            with self.subTest(scope_field=index):
                self.assert_retirement_rejected(decoded(raw))

    def test_current_service_header_can_reuse_the_original_physical_address(self):
        raw = sample()
        for seq in (8, 9, 10):
            set_event(raw, seq, 13, HEADER)
        self.assertTrue(validate(decoded(raw))['metadataRecycled'])

    def test_failed_native_return_never_recycles_even_if_later_counts_increase(self):
        raw = sample()
        set_event(raw, 9, 21, report.COMPLETION_FLAGS << 32 | 1)
        raw['RetirementTotals'][7:9] = [100, 100]
        self.assert_retirement_rejected(decoded(raw))

    def test_optional_recycle_gate_does_not_remove_native_return_requirement(self):
        rows = [list(row) for row in sample()['events']['rows'][:9]]
        value = decoded(sample(rows))
        self.assert_retirement_rejected(value)
        self.assertFalse(validate(value, require_recycled=False)['metadataRecycled'])
        rows.pop()
        self.assert_retirement_rejected(decoded(sample(rows)), require_recycled=False)

    def test_quarantine_cancel_and_abandon_are_not_completion(self):
        for kind in (4, 10, 11):
            rows = [list(row) for row in sample()['events']['rows'][:10]]
            rows.append(event(11, kind))
            with self.subTest(kind=kind):
                self.assert_retirement_rejected(decoded(sample(rows)))

    def test_unchanged_allocator_epoch_and_supported_profile_are_required(self):
        for index, invalid in ((0, 0), (0, 2), (0, 3), (1, 8), (2, 1), (3, 1), (4, 1), (21, 1)):
            raw = sample()
            raw['Allocator'][index] = invalid
            with self.subTest(index=index, invalid=invalid):
                self.assert_retirement_rejected(decoded(raw))

    def test_allocator_globals_vptr_and_ranges_cannot_be_forged(self):
        for index, invalid in ((5, 0), (6, 0), (7, 0), (8, 0), (9, 0), (10, 0),
                               (11, 0), (12, 0), (13, 0x1fffff), (20, 2)):
            raw = sample()
            raw['Allocator'][index] = invalid
            with self.subTest(index=index, invalid=invalid):
                self.assert_decode_rejected(raw)

    def test_more_than_four_native_target_tuples_not_setting_labels_are_required(self):
        self.assert_retirement_rejected(decoded(sample(keys=4)))
        raw = sample()
        raw['cacheRows'][4][0] += 0x1000
        self.assert_decode_rejected(raw)
        raw = sample()
        raw['cacheRows'][4][1:3] = raw['cacheRows'][3][1:3]
        self.assert_decode_rejected(raw)
        raw = sample()
        raw['Cache'][4] = 1
        self.assert_retirement_rejected(decoded(raw))

    def test_same_key_with_different_view_counts_is_a_distinct_native_tuple(self):
        raw = sample()
        for i in range(5):
            raw['cacheRows'][i][1:3] = [0, i + 1]
        self.assertTrue(validate(decoded(raw))['cacheOwnershipCorrelationProved'])

    def test_exact_native_admission_not_mutable_cache_labels_binds_source(self):
        value = decoded()
        value['cache']['lastConsumer'] = 987654
        self.assertTrue(validate(value)['cacheOwnershipCorrelationProved'])
        for index in range(6):
            bad = decoded()
            bad['admission'][index] += 8
            with self.subTest(index=index):
                if index == 5:
                    self.assertEqual(validate(bad)['renderer'], hex(bad['admission'][5]))
                else:
                    self.assert_retirement_rejected(bad)
        bad = decoded()
        bad.pop('admission')
        self.assert_retirement_rejected(bad)
        for invalid in (0, RENDERER + 1):
            bad = decoded()
            bad['admission'][5] = invalid
            self.assert_retirement_rejected(bad)

    def test_all_four_native_cache_slots_are_valid_source_graph_labels(self):
        for slot in range(4):
            value = decoded()
            value['admission'][3] = CACHE_BASE + 0x10 + slot * 0x90
            with self.subTest(slot=slot):
                self.assertTrue(validate(value)['cacheOwnershipCorrelationProved'])

    def test_pid_build_and_frame_freshness_are_explicitly_external(self):
        raw = sample()
        raw['pid'], raw['dll'] = 99999, 'another-build'
        proof = validate(decoded(raw))
        self.assertFalse(proof['processFreshnessProved'])
        self.assertFalse(proof['frameFreshnessProved'])
        self.assertTrue(proof['externalFreshnessRequired'])
        for key, invalid in (('pid', 0), ('pid', True), ('pid', 10304.0), ('dll', '')):
            bad = sample()
            bad[key] = invalid
            self.assert_decode_rejected(bad)

    def test_fixed_abi_lengths_and_exact_unsigned_types(self):
        for name in ('Allocator', 'RetirementTotals', 'Cache'):
            for mode in ('short', 'long', 'bad'):
                raw = sample()
                if mode == 'short':
                    raw[name].pop()
                elif mode == 'long':
                    raw[name].append(0)
                else:
                    raw[name][0] = True
                with self.subTest(name=name, mode=mode):
                    self.assert_decode_rejected(raw)
        for name in ('Allocator', 'RetirementTotals', 'Cache'):
            for invalid in (-1, 1 << 64, 1.0, None):
                raw = sample()
                raw[name][1] = invalid
                self.assert_decode_rejected(raw)
        for invalid in (1, -2, 1 << 32, True):
            raw = sample()
            raw['sequence'] = invalid
            self.assert_decode_rejected(raw)

    def test_ring_bounds_unused_slots_and_publication_are_strict(self):
        for field, value in (('count', 33), ('count', 9), ('sequence', 9), ('sequence', 1 << 64)):
            raw = sample()
            raw['events'][field] = value
            self.assert_decode_rejected(raw)
        for block, width in (('events', 24), ('cacheRows', 8)):
            raw = sample()
            rows = raw['events']['rows'] if block == 'events' else raw['cacheRows']
            rows.pop()
            self.assert_decode_rejected(raw)
            raw = sample()
            rows = raw['events']['rows'] if block == 'events' else raw['cacheRows']
            rows[-1] = [0] * (width - 1)
            self.assert_decode_rejected(raw)
            raw = sample()
            rows = raw['events']['rows'] if block == 'events' else raw['cacheRows']
            rows[-1][1] = 1
            self.assert_decode_rejected(raw)

    def test_corrupted_decoded_labels_cannot_bypass_raw_words(self):
        value = decoded()
        value['events'][4]['header'] += 8
        self.assert_retirement_rejected(value)
        value = decoded()
        value['cacheRows'][4]['key'] = value['cacheRows'][3]['key']
        self.assert_retirement_rejected(value)


class RetirementHistoryTest(unittest.TestCase):
    def test_overlapping_samples_merge_without_duplicate_events(self):
        events = decoded()['events']
        merged = report.merge_event_history(events[:6], events[3:])
        self.assertEqual(merged, events)
        self.assertEqual(report.merge_event_history(merged, events), events)

    def test_ring_wrap_and_gathered_history_preserve_old_target_receipts(self):
        first = decoded()
        rows = [value['raw'] for value in first['events']]
        for seq in range(11, 41):
            row = event(seq, 12)
            row[2:11] = [0] * 9
            rows.append(row)
        last = decoded(sample(rows))
        self.assert_retirement_missing(last)
        middle = report.decode(sample(rows[:25]))
        history = report.merge_event_history(first['events'], middle['events'])
        last['history'] = report.merge_event_history(history, last['events'])
        self.assertTrue(validate(last)['metadataRecycled'])

    def assert_retirement_missing(self, value):
        with self.assertRaises(ValueError):
            validate(value)

    def test_unobserved_gap_conflict_and_duplicate_order_are_rejected(self):
        events = decoded()['events']
        for previous, current in ((events[:3], events[4:]), ([], [events[0], events[0]]), ([], list(reversed(events)))):
            with self.assertRaises(ValueError):
                report.merge_event_history(previous, current)
        changed = copy.deepcopy(events)
        changed[0]['raw'][8] += 1
        changed[0]['consumerId'] += 1
        with self.assertRaises(ValueError):
            report.merge_event_history(events, changed)

    def test_history_capacity_never_silently_discards_old_proof(self):
        events = [report._event(event(seq, 12)) for seq in range(1, report.HISTORY_CAPACITY + 1)]
        self.assertEqual(len(report.merge_event_history([], events)), report.HISTORY_CAPACITY)
        with self.assertRaises(ValueError):
            report.merge_event_history(events, [report._event(event(report.HISTORY_CAPACITY + 1, 12))])

    def test_history_must_include_and_end_at_current_ring_tail(self):
        value = decoded()
        value['history'] = value['events'][:-1]
        self.assert_retirement_missing(value)
        value = decoded()
        value['history'] = value['events'] + [report._event(event(11, 12))]
        self.assert_retirement_missing(value)


if __name__ == '__main__':
    unittest.main()
