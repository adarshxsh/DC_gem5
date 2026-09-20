#!/usr/bin/env python3
"""
Unit test for validating decayShift CLI parameter options in gem5 configuration scripts.
Verifies that --decay-shift and --decayShift are parsed correctly and propagated to
cache compressor SimObject instances in spec2017_compression_kvm.py and spec2017_compression_eval.py.
"""

import os
import sys
import unittest
from unittest.mock import (
    MagicMock,
    patch,
)

sys.path.insert(0, "/app/DC_gem5/src/python")
sys.path.insert(0, "/app/DC_gem5")

# Mock m5 module before importing configuration scripts
mock_m5 = MagicMock()
mock_m5.options.outdir = "/tmp"
sys.modules["m5"] = mock_m5
sys.modules["m5.objects"] = MagicMock()
sys.modules["m5.params"] = MagicMock()
sys.modules["m5.proxy"] = MagicMock()
sys.modules["m5.util"] = MagicMock()

# Mock gem5 modules
mock_gem5 = MagicMock()
sys.modules["gem5"] = mock_gem5
sys.modules["gem5.coherence_protocol"] = MagicMock()
sys.modules["gem5.components"] = MagicMock()
sys.modules["gem5.components.boards"] = MagicMock()
sys.modules["gem5.components.boards.x86_board"] = MagicMock()
sys.modules["gem5.components.cachehierarchies"] = MagicMock()
sys.modules["gem5.components.cachehierarchies.abstract_cache_hierarchy"] = (
    MagicMock()
)
sys.modules[
    "gem5.components.cachehierarchies.abstract_two_level_cache_hierarchy"
] = MagicMock()
sys.modules["gem5.components.cachehierarchies.classic"] = MagicMock()
sys.modules[
    "gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy"
] = MagicMock()
sys.modules["gem5.components.cachehierarchies.classic.caches"] = MagicMock()
sys.modules["gem5.components.cachehierarchies.classic.caches.l1dcache"] = (
    MagicMock()
)
sys.modules["gem5.components.cachehierarchies.classic.caches.l1icache"] = (
    MagicMock()
)
sys.modules["gem5.components.cachehierarchies.classic.caches.l2cache"] = (
    MagicMock()
)
sys.modules["gem5.components.memory"] = MagicMock()
sys.modules["gem5.components.processors"] = MagicMock()
sys.modules["gem5.components.processors.cpu_types"] = MagicMock()
sys.modules["gem5.components.processors.simple_switchable_processor"] = (
    MagicMock()
)
sys.modules["gem5.isas"] = MagicMock()
sys.modules["gem5.resources"] = MagicMock()
sys.modules["gem5.resources.resource"] = MagicMock()
sys.modules["gem5.simulate"] = MagicMock()
sys.modules["gem5.simulate.exit_event"] = MagicMock()
sys.modules["gem5.simulate.simulator"] = MagicMock()
sys.modules["gem5.utils"] = MagicMock()
sys.modules["gem5.utils.override"] = MagicMock()
sys.modules["gem5.utils.requires"] = MagicMock()


# Define dummy base classes for hierarchy inheritance
class AbstractClassicCacheHierarchy:
    def __init__(self):
        pass


class AbstractTwoLevelCacheHierarchy:
    def __init__(self, **kwargs):
        pass


sys.modules[
    "gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy"
].AbstractClassicCacheHierarchy = AbstractClassicCacheHierarchy
sys.modules[
    "gem5.components.cachehierarchies.abstract_two_level_cache_hierarchy"
].AbstractTwoLevelCacheHierarchy = AbstractTwoLevelCacheHierarchy


class TestDecayShiftCLI(unittest.TestCase):

    def test_spec2017_compression_kvm_hierarchy(self):
        """Test PrivateL1PrivateL2WithCompressionHierarchy parameter propagation in spec2017_compression_kvm.py."""
        test_argv = [
            "spec2017_compression_kvm.py",
            "--benchmark",
            "505.mcf_r",
            "--size",
            "test",
            "--enable-adaptive-bypass",
            "--decayShift",
            "6",
        ]
        with patch.object(sys, "argv", test_argv), patch(
            "os.path.exists", return_value=True
        ), patch("os.access", return_value=True):
            from configs.spec2017_compression_kvm import (
                PrivateL1PrivateL2WithCompressionHierarchy,
                args,
            )

            self.assertEqual(args.decay_shift, 6)
            self.assertTrue(args.enable_adaptive_bypass)

            mock_l2 = MagicMock()
            mock_compressor = MagicMock()

            with patch("m5.objects.L2Cache", return_value=mock_l2), patch(
                "m5.objects.BDI", return_value=mock_compressor
            ), patch("m5.objects.CompressedTags", return_value=MagicMock()):

                hierarchy = PrivateL1PrivateL2WithCompressionHierarchy(
                    compressor="bdi",
                    enable_adaptive_bypass=args.enable_adaptive_bypass,
                    latency_breakeven_threshold=args.latency_breakeven_threshold,
                    sampling_interval=args.sampling_interval,
                    decay_shift=args.decay_shift,
                )

                l2_instance = hierarchy._create_l2_cache()

                self.assertTrue(l2_instance.compressor.enable_adaptive_bypass)
                self.assertEqual(l2_instance.compressor.decay_shift, 6)

    def test_spec2017_compression_eval_hierarchy(self):
        """Test PrivateL1PrivateL2WithCompressionHierarchy parameter propagation in spec2017_compression_eval.py."""
        test_argv = [
            "spec2017_compression_eval.py",
            "--benchmark",
            "505.mcf_r",
            "--size",
            "test",
            "--enable-adaptive-bypass",
            "--decay-shift",
            "5",
        ]
        with patch.object(sys, "argv", test_argv), patch(
            "os.path.exists", return_value=True
        ):
            from configs.spec2017_compression_eval import (
                PrivateL1PrivateL2WithCompressionHierarchy,
                args,
            )

            self.assertEqual(args.decay_shift, 5)
            self.assertTrue(args.enable_adaptive_bypass)

            mock_l2 = MagicMock()
            mock_compressor = MagicMock()

            with patch("m5.objects.L2Cache", return_value=mock_l2), patch(
                "m5.objects.BDI", return_value=mock_compressor
            ), patch("m5.objects.CompressedTags", return_value=MagicMock()):

                hierarchy = PrivateL1PrivateL2WithCompressionHierarchy(
                    l1d_size="32KiB",
                    l1i_size="32KiB",
                    l2_size="512KiB",
                    l2_assoc=16,
                    use_compression=True,
                    enable_adaptive_bypass=args.enable_adaptive_bypass,
                    latency_breakeven_threshold=args.latency_breakeven_threshold,
                    sampling_interval=args.sampling_interval,
                    decay_shift=args.decay_shift,
                )

                l2_instance = hierarchy._create_l2_cache()

                self.assertTrue(l2_instance.compressor.enable_adaptive_bypass)
                self.assertEqual(l2_instance.compressor.decay_shift, 5)


if __name__ == "__main__":
    unittest.main()
