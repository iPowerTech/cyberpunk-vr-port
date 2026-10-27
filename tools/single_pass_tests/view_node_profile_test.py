import unittest

import view_node_profile as vnp


class NamesTest(unittest.TestCase):
    def test_parses_entries(self):
        self.assertEqual(vnp.node_names('{ 0x23A938, "RenderElements" },\n{0x154610,"RenderLightsIntegrate"}'),
                         {0x23A938: 'RenderElements', 0x154610: 'RenderLightsIntegrate'})


class AggregateTest(unittest.TestCase):
    def test_per_frame_per_side(self):
        frame = 0x154610
        profile = dict(groups=[
            dict(side=0, node=frame, ranges=4, sum_ms=2.0, commands=8),
            dict(side=1, node=frame, ranges=2, sum_ms=1.0, commands=4),
            dict(side=0, node=0x37D5C4, ranges=4, sum_ms=6.0, commands=40),
            dict(side=1, node=0x37D5C4, ranges=2, sum_ms=4.0, commands=20),
            dict(side=2, node=0x37D5C4, ranges=9, sum_ms=9.0, commands=9)])
        nodes, frames = vnp.aggregate([profile, profile], (frame,))
        self.assertEqual(frames, {0: 8, 1: 4})
        self.assertAlmostEqual(nodes[0x37D5C4][0]['ms'], 1.5)
        self.assertAlmostEqual(nodes[0x37D5C4][1]['ms'], 2.0)
        rows = vnp.table(nodes, {0x37D5C4: 'ApplyDLSS'})
        self.assertEqual(rows[0]['name'], 'ApplyDLSS')
        self.assertEqual(rows[1]['node'], hex(frame))

    def test_missing_frame_node(self):
        with self.assertRaises(ValueError):
            vnp.aggregate([dict(groups=[dict(side=0, node=1, ranges=1, sum_ms=1.0)])], (2,))


if __name__ == '__main__':
    unittest.main()
