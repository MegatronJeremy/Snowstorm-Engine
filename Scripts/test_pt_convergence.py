"""GPU-free tests for pt-convergence.py's pure parts. Run: py -m unittest Scripts/test_pt_convergence.py"""
import importlib.util
import math
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("pt_convergence", Path(__file__).with_name("pt-convergence.py"))
pc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pc)


class SlopeTests(unittest.TestCase):
    def test_white_noise_mse_is_minus_one(self):
        ns = [1, 4, 16, 64]
        self.assertAlmostEqual(pc.loglog_slope(ns, [0.5 / n for n in ns]), -1.0, places=9)

    def test_steeper_for_qmc_like_decay(self):
        ns = [1, 4, 16, 64]
        self.assertAlmostEqual(pc.loglog_slope(ns, [n ** -1.5 for n in ns]), -1.5, places=9)

    def test_too_few_points(self):
        self.assertIsNone(pc.loglog_slope([4], [0.1]))
        self.assertIsNone(pc.loglog_slope([1, 4], [0.1, 0.0]))


class RatioAndStopRuleTests(unittest.TestCase):
    def test_ratio(self):
        self.assertAlmostEqual(pc.ratio(0.3, 0.2), 1.5)
        self.assertIsNone(pc.ratio(None, 0.2))
        self.assertIsNone(pc.ratio(0.3, 0.0))

    def test_stop_rule_met_at_threshold(self):
        ok, why = pc.stop_rule({4: 1.5, 16: 2.0})
        self.assertTrue(ok)
        self.assertEqual(why, [])

    def test_stop_rule_names_failures(self):
        ok, why = pc.stop_rule({4: 1.2})
        self.assertFalse(ok)
        self.assertEqual(len(why), 2)
        self.assertIn("4 spp", why[0])
        self.assertIn("16 spp not measured", why[1])

    def test_default_spp_cover_stop_rule(self):
        self.assertTrue(set(pc.STOP_RULE_SPP) <= set(pc.DEFAULT_SPP))


if __name__ == "__main__":
    unittest.main()
