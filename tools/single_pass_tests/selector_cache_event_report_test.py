import struct
import unittest
from selector_cache_event_report import analyze
from selector_coverage_report_test import data as coverage_data


def data():
    source, cbvs, selectors, _ = coverage_data()
    rows = []
    for index, cbv in enumerate(cbvs['rows']):
        header = [0x4345565430303031, cbv[12], 2, 1, 512, 228, 2, 100]
        published = [1, 123, 456, 512, 228, 7, 1, 20]
        evicted = [2, 123, 456, 512, 228, 7, 12, 21]
        rows.append([index, index, source['rows'][index][1], 1, *header, *published, *evicted, *evicted])
    return source, cbvs, selectors, rows


def check(values):
    source, cbvs, selectors, rows = values
    return analyze(b''.join(struct.pack('<36Q', *r) for r in rows), source, cbvs, selectors)


class SelectorCacheEventTests(unittest.TestCase):
    def test_exact_published_and_evicted_metadata_has_no_permission(self):
        result = check(data())
        self.assertEqual((result['queries'], result['published'], result['evicted']), (4, 4, 4))
        self.assertFalse(result['copyPermission'])

    def test_missing_owner_and_truncated_history_remain_unknown(self):
        values = data()
        values[3][0][7] = 0
        values[3][0][10:] = [0] * 26
        values[3][1][10:] = [0, 120, *([0] * 24)]
        result = check(values)
        self.assertEqual(result['published'], 2)

    def test_unknown_query_cannot_return_history(self):
        values = data()
        values[3][0][3] = 0
        with self.assertRaises(ValueError):
            check(values)
        values[3][0][4:] = [0] * 32
        self.assertEqual(check(values)['queries'], 3)

    def test_disjoint_and_malformed_receipts_reject(self):
        for field, value in ((0, 99), (1, 99), (3, 2), (4, 0), (5, 0), (8, 513),
                             (9, 65537), (10, 65), (12, 3), (15, 1000), (16, 0),
                             (17, 0), (18, 12), (19, 0), (26, 1), (34, 5), (35, 22)):
            with self.subTest(field=field):
                values = data()
                values[3][0][field] = value
                with self.assertRaises(ValueError):
                    check(values)

    def test_rejected_observation_and_full_invalidation_are_metadata_only(self):
        values = data()
        values[3][0][20:28] = [2, 123, 456, 512, 228, 0, 37, 22]
        values[3][1][20:28] = [2, 0, 0, 0, 2**64 - 1, 0, 4, 22]
        self.assertEqual(check(values)['invalidated'], 4)


if __name__ == '__main__':
    unittest.main()
