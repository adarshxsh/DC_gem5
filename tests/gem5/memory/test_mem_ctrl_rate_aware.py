# Copyright (c) 2026
# All rights reserved.

import unittest
import m5
from m5.objects import MemCtrl, DDR3_1600_8x8

class MemCtrlRateAwareTestCase(unittest.TestCase):
    def test_parameters_exist(self):
        ctrl = MemCtrl(dram=DDR3_1600_8x8())
        self.assertFalse(ctrl.enable_rate_aware_threshold)
        self.assertEqual(ctrl.write_rate_alpha, 0.25)
        self.assertEqual(ctrl.write_rate_threshold, 0.0)
        self.assertEqual(ctrl.rate_sensitivity, 1.0)

    def test_enable_rate_aware_threshold(self):
        ctrl = MemCtrl(
            dram=DDR3_1600_8x8(),
            enable_rate_aware_threshold=True,
            write_rate_alpha=0.2,
            write_rate_threshold=0.001,
            rate_sensitivity=1.5
        )
        self.assertTrue(ctrl.enable_rate_aware_threshold)
        self.assertEqual(ctrl.write_rate_alpha, 0.2)
        self.assertEqual(ctrl.write_rate_threshold, 0.001)
        self.assertEqual(ctrl.rate_sensitivity, 1.5)

if __name__ == "__main__":
    unittest.main()
