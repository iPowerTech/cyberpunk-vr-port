import unittest
from read_families import validate_input_contracts


class FamilyContractDecodeTests(unittest.TestCase):
    def test_known_combinations(self):
        for skin, alpha in ((0, 0), (1, 0), (2, 0), (0, 1), (2, 2)):
            validate_input_contracts(skin, alpha)

    def test_unknown_and_incompatible(self):
        for skin, alpha in ((3, 0), (0, 3), (-1, 0), (0, -1), (True, 0),
                            (1.0, 0), (0, False), (1, 1), (2, 1), (0, 2), (1, 2)):
            with self.subTest(skin=skin, alpha=alpha), self.assertRaises(ValueError):
                validate_input_contracts(skin, alpha)


if __name__ == '__main__':
    unittest.main()
