import unittest

import node_pso_profile as npp


class ByPsoTest(unittest.TestCase):
    def test_sums_and_pairs(self):
        profile = dict(groups=[
            dict(side=0, pso=0xA, detail=0, ranges=2, commands=4, sum_ms=0.2, max_ms=0.12),
            dict(side=1, pso=0xA, detail=0, ranges=2, commands=4, sum_ms=0.4, max_ms=0.25),
            dict(side=1, pso=0xB, detail=1, ranges=1, commands=1, sum_ms=0.1, max_ms=0.1),
            dict(side=2, pso=0xC, detail=0, ranges=1, commands=1, sum_ms=9.0, max_ms=9.0)])
        rows = npp.by_pso([profile, profile])
        self.assertEqual(len(rows), 3)
        main = next(r for r in rows if r['side'] == 0)
        self.assertEqual((main['ranges'], main['commands']), (4, 8))
        self.assertAlmostEqual(main['msPerRange'], 0.1)
        self.assertAlmostEqual(main['commandsPerRange'], 2.0)
        pairs = npp.pair(rows)
        self.assertEqual([(p, d) for p, d, _, _ in pairs], [('0xa', 0), ('0xb', 1)])
        self.assertIsNone(pairs[1][2])
        self.assertAlmostEqual(pairs[0][3]['msPerRange'], 0.2)


if __name__ == '__main__':
    unittest.main()
