import unittest
from analyze_constant_pairs import compare


class ConstantPairTests(unittest.TestCase):
    def test_owned_payloads_compared_without_cbv_padding(self):
        draws = [dict(family=9, sequence=i+1, first=i, count=1, flags=7, side=1-i, frame=1) for i in range(2)]
        bindings = [dict(visibility=1, shaderRegister=1, payload=i+1, expectedBytes=3) for i in range(2)]
        payloads = [{}, dict(offset=0), dict(offset=4)]
        geometry = [dict(ambiguous=False, draws=[[1, 2]], packets=[4, 5])]
        result = compare(draws, bindings, payloads, b'abcXabcY', geometry)
        self.assertEqual(result['totals']['verifiedConstantPairs'], 1)
        self.assertEqual(result['slots']['1:b1'], dict(equal=1))
        result = compare(draws, bindings, payloads, b'abcXabZY', geometry)
        self.assertEqual(result['slots']['1:b1'], dict(different=1))
        self.assertFalse(result['runtimeEligible'])

    def test_ambiguity_missing_and_bad_identity_are_not_proof(self):
        draw = dict(family=1, frame=1, side=1, sequence=1, first=0, count=0, flags=7)
        item = dict(ambiguous=False, draws=[[1, 2]], packets=[0, 1])
        result = compare([draw], [], [], b'', [item, dict(item, ambiguous=True)])
        self.assertEqual(result['totals'], dict(noLinkedFamily=1, ambiguousGeometry=1))
        result = compare([draw, dict(draw, sequence=2)], [], [], b'', [item])
        self.assertEqual(result['totals'], dict(familyFrameOrEyeMismatch=1))
        result = compare([draw, dict(draw, sequence=2, side=0, flags=3)], [], [], b'', [item])
        self.assertEqual(result['totals'], dict(unstableOrMissingConstants=1))


if __name__ == '__main__':
    unittest.main()
