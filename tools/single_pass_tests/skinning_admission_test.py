import unittest
from early_depth_report import validate_skinning


class SkinningAdmissionTest(unittest.TestCase):
    def test_reference_and_gpu_proofs(self):
        validate_skinning(dict(enabled=True, stats=[5, 5, 5, 0, 0, 0], proofs=[0, 0, 0, 0], bindingChecks=[5,5,0,0]), False)
        validate_skinning(dict(enabled=True, stats=[5, 5, 5, 0, 0, 0], proofs=[5, 10, 61440, 16128], bindingChecks=[5,5,0,0]), True)
        validate_skinning(dict(enabled=False, stats=[0]*6, proofs=[0]*4), True)

    def test_missing_or_failed_contract(self):
        for stats in ([0]*6, [5, 0, 5, 0, 0, 0], [5, 5, 0, 0, 0, 0], [5, 5, 5, 1, 0, 0], [5, 5, 5, 0, 1, 0], [5, 5, 5, 0, 0, 118]):
            with self.assertRaises(RuntimeError):
                validate_skinning(dict(enabled=True, stats=stats, proofs=[5, 10, 61440, 16128]), True)

    def test_incomplete_gpu_snapshots(self):
        for proofs in ([0]*4, [4, 10, 61440, 16128], [5, 9, 61440, 16128], [5, 10, 0, 16128], [5, 10, 61440, 0]):
            with self.assertRaises(RuntimeError):
                validate_skinning(dict(enabled=True, stats=[5, 5, 5, 0, 0, 0], proofs=proofs, bindingChecks=[5,5,0,0]), True)
        with self.assertRaises(RuntimeError):
            validate_skinning(dict(enabled=False, stats=[1, 0, 0, 0, 0, 0], proofs=[0]*4), False)

    def test_submission_validation_is_required(self):
        for bindings in ([0,0,0,0], [5,4,0,0], [4,5,0,0], [5,5,1,1], [5,5,0,4]):
            with self.assertRaises(RuntimeError):
                validate_skinning(dict(enabled=True, stats=[5,5,5,0,0,0], proofs=[5,10,61440,16128], bindingChecks=bindings), True)


if __name__ == '__main__':
    unittest.main()
