import base64
import unittest
from select_constant_pass import select_pass


class SelectPassTests(unittest.TestCase):
    def setUp(self):
        self.draws = [dict(sequence=i+1, family=9, flags=7, count=2, frame=1, side=1 if i<3 else 0) for i in range(6)]
        self.rows = [dict(frame=1, node='0x23a938', argumentPlane=3, modes=[0,0,1],
                          groupBase64=base64.b64encode(bytes(48)).decode(), side=1-i, offset=i*3, count=3) for i in range(2)]
        self.pairs = [dict(ambiguous=False, draws=[[i+1,i+4]], packets=[i,i+3]) for i in range(3)]

    def select(self):
        return select_pass(self.draws, {9}, self.pairs, self.rows)

    def test_one_pass_and_duplicate_draw_pairs(self):
        self.pairs.append(self.pairs[0])
        result = self.select()
        self.assertEqual(len(result['pairs']), 3)
        self.assertEqual(result['frame'], 1)
        self.assertFalse(result['runtimeEligible'])

    def test_incomplete_or_ambiguous_pairs_not_selected(self):
        self.pairs[0]['ambiguous'] = True
        self.draws[1]['flags'] = 3
        self.draws[2]['family'] = 8
        self.assertIsNone(self.select())

    def test_eye_frame_and_family_identity_required(self):
        for field, value in (('side', 0), ('frame', 2), ('family', 8), ('count', 0)):
            changed = [dict(d, **{field:value}) if d['side']==1 else d.copy() for d in self.draws]
            self.assertIsNone(select_pass(changed, {9}, self.pairs, self.rows))

    def test_cross_pass_mapping_rejected(self):
        self.rows[1]['argumentPlane'] = 4
        with self.assertRaises(ValueError):
            self.select()

    def test_padding_is_not_part_of_native_pass_identity(self):
        group = bytearray(48)
        group[13:16] = b'abc'
        self.rows[1]['groupBase64'] = base64.b64encode(group).decode()
        self.assertEqual(len(self.select()['pairs']), 3)

    def test_different_passes_get_separate_banks(self):
        other_rows = [dict(r, frame=2, offset=r['offset']+6) for r in self.rows]
        other_draws = [dict(d, frame=2, sequence=d['sequence']+6) for d in self.draws]
        other_pairs = [dict(p, draws=[[v+6 for v in p['draws'][0]]], packets=[v+6 for v in p['packets']]) for p in self.pairs[:2]]
        result = select_pass(self.draws+other_draws, {9}, self.pairs+other_pairs, self.rows+other_rows)
        self.assertEqual(result['frame'], 1)
        self.assertEqual(len(result['pairs']), 3)
        self.assertEqual(result['eligiblePairsAcrossPasses'], 5)


if __name__ == '__main__':
    unittest.main()
