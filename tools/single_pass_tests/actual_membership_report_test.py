import copy
import io
import struct
import sys
import unittest
from unittest.mock import patch

import actual_membership_report as report


def fixture(rejected=False):
    sources = [[94, 0], [271, 1]]
    rows = []
    for i, (ordinal, eye) in enumerate(sources):
        row = [ordinal, 1-eye, 77, 0x10000+i, 256*i, 0x20000+i, 1024*1024*i, 1024*1024,
               16, 3, 2, 1, 1, 1, 7, 1]
        row += [1, 0, 2, 1 if i == 0 else 3, 1 if i == 0 else 2, 5 if i == 0 else 10, report.U32, report.U32, 1, 0]+[0]*6
        rows.append(row)
    stats = [1, 1, 77, 2, 2, 4, 2, 2, 96, 1, 2, 2, 0, 3, 15, 0, 0, 0, 0, 2, 2, 0, 0, 0]
    inputs = [2, 2, 16, 2, 16, 1, 0, 16]
    if rejected:
        rows[0][17] = 16;rows[0][22:26] = [0, 5, 0, 1]
        rows[1][19:22] = [0, 0, 0]
        stats[11:15] = [1, 1, 1, 5];stats[18] = 1;inputs[5:7] = [0, 1]
    return report.decode(stats, rows), sources, inputs


class ActualMembershipReportTests(unittest.TestCase):
    def checked(self, value=None, *, rejected=False, sources=None, inputs=None, frame=77, shared=1):
        original, selected, predicates = fixture(rejected)
        return report.validate(value if value is not None else original, requested=True, frame=frame,
            sources=sources if sources is not None else selected, shared=shared,
            inputs=inputs if inputs is not None else predicates, rejected=rejected)

    def test_positive_actual_inputs_and_predicate_and_are_explicit(self):
        value = self.checked()
        self.assertTrue(value['verified']);self.assertEqual(value['summary']['acceptedSources'], 2)
        self.assertIn('no expected shader-byte', value['inputPolicy'])
        self.assertIn('literal one QWORDs', value['aggregateGatePolicy'])

    def test_fault_allows_zero_record_source_but_requires_real_missing_descriptor(self):
        value = self.checked(rejected=True)
        self.assertTrue(value['verified']);self.assertEqual(value['summary']['rejectedSources'], 1)
        self.assertEqual(value['rows'][1][24:26], [1, 0])
        self.assertIn('actual B4/T5 data unchanged', value['faultPolicy'])
        value, _, _ = fixture(True);value['rows'][0][17] = 8
        with self.assertRaises(ValueError):self.checked(value, rejected=True)
        value, _, _ = fixture(True);value['rows'][0][17] = 24
        with self.assertRaises(ValueError):self.checked(value, rejected=True)
        with self.assertRaises(ValueError):self.checked(fixture()[0], rejected=True)
        with self.assertRaises(ValueError):self.checked(fixture(True)[0])
        value, _, _ = fixture(True);value['rows'][1][19:22] = [1, 1, 5]
        with self.assertRaises(ValueError):self.checked(value, rejected=True)

    def test_partial_preparation_does_not_claim_completion(self):
        value, _, _ = fixture();value['stats'][4] = 1;value['stats'][9] = 0;value['rows'] = value['rows'][:1]
        observed = report.validate_partial(value, requested=True, frame=77)
        self.assertFalse(observed['verified'])
        with self.assertRaises(ValueError):self.checked(value)
        for index in (15, 16, 17, 21, 22, 23):
            changed = copy.deepcopy(value);changed['stats'][index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):report.validate_partial(changed, requested=True, frame=77)

    def test_required_terminal_stats_are_not_optional(self):
        for index in range(24):
            value, _, _ = fixture();value['stats'][index] += 1
            if index==0:value['stats'][index]=3
            with self.subTest(index=index), self.assertRaises(ValueError):self.checked(value)
        for index in (0, 1, 2, 3, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26):
            value, _, _ = fixture();value['rows'][0][index] += 1
            # maxBit can vary without changing any bitmap size counter; it is
            # bounded, not reconstructible from aggregate telemetry.
            if index in (14, 18):continue
            if index in (3, 5):value['rows'][0][index] = 0
            if index == 8:value['rows'][0][index] = 0
            if index == 19:value['rows'][0][index] = 3
            with self.subTest(row=index), self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['rows'][0][14] = 32768
        with self.assertRaises(ValueError):self.checked(value)

    def test_cpu_refusal_raw_evidence_remains_unchanged(self):
        value, _, _ = fixture();value['stats'][15:18] = [2, 95, 6]
        before = copy.deepcopy(value)
        with self.assertRaises(ValueError):self.checked(value)
        self.assertEqual(value, before)

    def test_grouped_schema_uses_exact_distinct_resource_pairs(self):
        value, _, _ = fixture();value['stats'][0]=2
        self.assertTrue(self.checked(value)['verified'])
        value['rows'][1][3]=value['rows'][0][3]
        value['rows'][1][5]=value['rows'][0][5]
        with self.assertRaises(ValueError):self.checked(value)
        value['stats'][7]=1
        self.assertTrue(self.checked(value)['verified'])
        value['stats'][0]=1
        with self.assertRaises(ValueError):self.checked(value)
        idle=report.decode([2]+[0]*23,[])
        self.assertTrue(report.validate(idle,requested=False,frame=77,sources=[],shared=0,inputs=[])['verified'])

    def test_scope_order_predicate_and_and_reserved_fields(self):
        _, sources, inputs = fixture()
        for wrong in (list(reversed(sources)), [[94, 0], [94, 1]], [[94, 2], [271, 1]]):
            with self.subTest(sources=wrong), self.assertRaises(ValueError):self.checked(sources=wrong)
        for index in range(8):
            changed = inputs.copy();changed[index] += 1
            # Two through N+1 aggregate dispatches are legitimate.
            if index == 3:changed[index] = 4
            with self.subTest(inputs=index), self.assertRaises(ValueError):self.checked(inputs=changed)
        with self.assertRaises(ValueError):self.checked(frame=78)
        with self.assertRaises(ValueError):self.checked(shared=2)
        value, _, _ = fixture();value['rows'][0][18] = 8192
        with self.assertRaises(ValueError):self.checked(value)

    def test_empty_bitmap_reports_uint_max_not_an_invented_slot(self):
        value, _, _ = fixture(True);row = value['rows'][0]
        row[10:14] = [0, 0, 0, 3];row[14] = report.U32;value['stats'][5:7] = [2, 4]
        self.assertTrue(self.checked(value, rejected=True)['verified'])
        row[14] = 0
        with self.assertRaises(ValueError):self.checked(value, rejected=True)

    def test_disabled_mode_exactly_idle_and_abi_strict(self):
        idle = report.decode([1]+[0]*23, [])
        self.assertTrue(report.validate(idle, requested=False, frame=77, sources=[], shared=0, inputs=[])['verified'])
        for index in range(1, 24):
            value = copy.deepcopy(idle);value['stats'][index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):report.validate_partial(value, requested=False)
        for word in (True, False, -1, 2**64, None, '1'):
            value, _, _ = fixture();value['stats'][1] = word
            with self.subTest(word=word), self.assertRaises(ValueError):self.checked(value)
        value, _, _ = fixture();value['rows'][0].pop()
        with self.assertRaises(ValueError):self.checked(value)

    def test_double_read_covers_fixed_rows_and_metadata(self):
        value, _, _ = fixture()
        exports = {report.PREFIX+'Stats':struct.pack('<24Q', *value['stats']),
            report.PREFIX+'Count':struct.pack('<I', 2), report.PREFIX+'RowBytes':struct.pack('<I', 256),
            report.PREFIX+'Rows':b''.join(struct.pack('<32Q', *row) for row in value['rows'])}
        read = lambda name, size:exports[name][:size]
        self.assertEqual(report.read_report(read), value)
        exports[report.PREFIX+'Count'] = struct.pack('<I', 513)
        with self.assertRaises(ValueError):report.read_report(read)
        exports[report.PREFIX+'Count'] = struct.pack('<I', 2);calls = 0
        def changing(name, size):
            nonlocal calls
            if name.endswith('Rows'):
                calls += 1;rows = copy.deepcopy(value['rows']);rows[0][18] += calls
                return b''.join(struct.pack('<32Q', *row) for row in rows)
            return read(name, size)
        with self.assertRaises(ValueError):report.read_report(changing, attempts=2)
        exports[report.PREFIX+'Rows'] = b''
        with self.assertRaises(ValueError):report.read_report(read)

    def test_capture_join_matches_complete_t5_not_only_frame(self):
        from read_prepared_pair import INPUT_FIELDS
        value, sources, inputs = fixture();terminal = self.checked(value)
        capture = dict(actualMembershipRequested=True, actualMembershipAvailable=True, sourceStream=dict(stats=[3,77]+[0]*14),
            actualMembership=report.validate_partial(value, requested=True, frame=77), fullT5=dict(rows=[]))
        for row in value['rows']:
            full = [0]*16;full[0:3] = row[:3];full[7:10] = row[5:8];capture['fullT5']['rows'].append(full)
        comparison = dict(actualMembershipRequested=True, actualMembershipAvailable=True, actualMembership=terminal,
            sources=sources, batch=dict(sharedDraws=1), gpuInputs=dict(zip(INPUT_FIELDS, inputs)))
        self.assertEqual(report.validate_join(capture, comparison, requested=True), terminal)
        capture['fullT5']['rows'][0][7] += 1
        with self.assertRaises(ValueError):report.validate_join(capture, comparison, requested=True)

    def test_cli_prerequisites_and_flag_forwarding(self):
        import read_preparation
        import read_prepared_pair
        import run_launch_tests as suite
        import live_symbols
        flags = ['--actual-membership', '--full-t5', '--prepared-current', '--prepared-infix', '--prepared-tail']
        for missing in flags[1:]:
            args = [flag for flag in flags if flag != missing]
            with self.subTest(missing=missing), patch('sys.stderr', io.StringIO()), patch.object(live_symbols, 'resolve') as resolve:
                with self.assertRaises(SystemExit) as error:read_preparation.main(['--pid','1','--out','unused',*args])
                self.assertEqual(error.exception.code, 2);resolve.assert_not_called()
            with patch('sys.stderr', io.StringIO()), patch.object(sys,'argv',['run_launch_tests.py','--out','unused',*args]), patch.object(suite.Path,'mkdir') as mkdir:
                with self.assertRaises(SystemExit) as error:suite.main()
                self.assertEqual(error.exception.code, 2);mkdir.assert_not_called()
        with patch('sys.stderr', io.StringIO()), patch.object(sys,'argv',['read_prepared_pair.py','--pid','1','--out','unused','--actual-membership']), patch.object(read_prepared_pair,'resolve') as resolve:
            with self.assertRaises(SystemExit) as error:read_prepared_pair.main()
            self.assertEqual(error.exception.code, 2);resolve.assert_not_called()
        capture = dict(sourceStream=dict(stats=[3,77]+[0]*14), actualMembershipRequested=True)
        for rejected in (False,True):self.assertIn('--actual-membership',suite.current_observer_flags(capture,rejected=rejected))


if __name__ == '__main__':unittest.main()
