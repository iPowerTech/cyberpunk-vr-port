import unittest
from run_launch_tests import insert_retained_bank_check, require_source_banks


class SourceBankLaunchPlanTests(unittest.TestCase):
    def test_check_follows_primary_and_optional_repeat_before_retirement(self):
        for repeat in (None, 'repeat_owned_reference', 'repeat_owned_graph_sort'):
            with self.subTest(repeat=repeat):
                prefix = ['retained_reference'] + ([repeat] if repeat else [])
                plan = prefix + ['native_retirement', 'post_pair_hud', 'controls_restored']
                insert_retained_bank_check(plan)
                self.assertEqual(plan, prefix + ['retained_source_bank', 'native_retirement', 'post_pair_hud', 'controls_restored'])

    def test_unattached_bank_check_is_rejected_without_mutation(self):
        plan = ['stable_hud', 'controls_restored']
        with self.assertRaises(ValueError):
            insert_retained_bank_check(plan)
        self.assertEqual(plan, ['stable_hud', 'controls_restored'])

    def test_both_source_banks_remain_required(self):
        good=dict(verified=True,bankCreated=True,bankStats=[1,1,1,32,0,0],commands=[[0]],
                  ready=1,cpuAssembled=1,privateTablesConsumed=False,nativeExecutionPermitted=False)
        require_source_banks(good)
        require_source_banks(good,dict(good))
        bad=dict(good,ready=0)
        with self.assertRaises(ValueError):
            require_source_banks(bad,good)
        with self.assertRaises(ValueError):
            require_source_banks(good,bad)


if __name__ == '__main__':
    unittest.main()
