# Copyright (c) 2025-2026
# SPEC CPU2017 Cache Compression Evaluation with KVM Acceleration Support
#
# Evaluates BDI cache compression in gem5 x86 full-system mode.
# Supports KVM fast-boot (when /dev/kvm is available) switching to O3CPU for ROI.
# Falls back cleanly to AtomicSimpleCPU if KVM is not available.

import argparse
import os
import sys
import time
from typing import Optional

import m5
from m5.objects import (
    BadAddr,
    BaseCPU,
    BaseXBar,
    Cache,
    L2XBar,
    SystemXBar,
)
from m5.params import Port
from m5.util import (
    fatal,
    warn,
)

from gem5.coherence_protocol import CoherenceProtocol
from gem5.components.boards.x86_board import X86Board
from gem5.components.cachehierarchies.abstract_cache_hierarchy import (
    AbstractCacheHierarchy,
)
from gem5.components.cachehierarchies.abstract_two_level_cache_hierarchy import (
    AbstractTwoLevelCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.caches.l1dcache import L1DCache
from gem5.components.cachehierarchies.classic.caches.l1icache import L1ICache
from gem5.components.cachehierarchies.classic.caches.l2cache import L2Cache
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_switchable_processor import (
    SimpleSwitchableProcessor,
)
from gem5.isas import ISA
from gem5.resources.resource import (
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.simulator import Simulator
from gem5.utils.override import overrides
from gem5.utils.requires import requires

# ---------------------------------------------------------------------------
# Custom Classic Cache Hierarchy with optional BDI compression on L2
# ---------------------------------------------------------------------------


class PrivateL1PrivateL2WithCompressionHierarchy(
    AbstractClassicCacheHierarchy, AbstractTwoLevelCacheHierarchy
):
    """
    A classic cache hierarchy with private L1I, L1D, and L2 caches.
    Extends stock hierarchy with optional BDI compressor and CompressedTags on L2.
    """

    def _get_default_membus(self) -> SystemXBar:
        from m5.objects import NULL

        membus = SystemXBar(width=64)
        membus.snoop_filter = NULL
        membus.badaddr_responder = BadAddr()
        membus.default = membus.badaddr_responder.pio
        return membus

    def __init__(
        self,
        l1i_size: str = "32KiB",
        l1d_size: str = "32KiB",
        l2_size: str = "512KiB",
        l2_assoc: int = 16,
        compressor: str = "none",
        membus: Optional[SystemXBar] = None,
    ) -> None:
        AbstractClassicCacheHierarchy.__init__(self)
        AbstractTwoLevelCacheHierarchy.__init__(
            self,
            l1i_size=l1i_size,
            l1i_assoc=8,
            l1d_size=l1d_size,
            l1d_assoc=8,
            l2_size=l2_size,
            l2_assoc=l2_assoc,
        )

        self._l1i_size = l1i_size
        self._l1d_size = l1d_size
        self._l2_size = l2_size
        self._l2_assoc = l2_assoc
        self._compressor_choice = compressor.lower()
        self.membus = membus if membus else self._get_default_membus()

    @overrides(AbstractClassicCacheHierarchy)
    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def _create_l2_cache(self) -> L2Cache:
        """Create an L2 cache, optionally with selected compressor."""
        l2 = L2Cache(size=self._l2_size, assoc=self._l2_assoc)

        if self._compressor_choice and self._compressor_choice != "none":
            from m5.objects import (
                BDI,
                CPack,
                FPC,
                ZeroCompressor,
                CompressedTags,
            )

            if self._compressor_choice == "bdi":
                l2.compressor = BDI()
            elif self._compressor_choice in ("cpack", "cpac"):
                l2.compressor = CPack()
            elif self._compressor_choice == "fpc":
                l2.compressor = FPC()
            elif self._compressor_choice in ("zero", "zerocompressor"):
                l2.compressor = ZeroCompressor()
            else:
                l2.compressor = BDI()

            l2.tags = CompressedTags()
            print(
                f"[CompressionEval] L2 cache configured with {l2.compressor.type} compressor "
                "and CompressedTags"
            )
        else:
            print(
                "[CompressionEval] L2 cache configured without compression (baseline)"
            )

        return l2

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board) -> None:
        board.connect_system_port(self.membus.cpu_side_ports)

        for _, port in board.get_mem_ports():
            self.membus.mem_side_ports = port

        from m5.objects import NULL

        l2buses = []
        for i in range(board.get_processor().get_num_cores()):
            l2_bus = L2XBar()
            l2_bus.snoop_filter = NULL
            l2buses.append(l2_bus)
        self.l2buses = l2buses

        for i, cpu in enumerate(board.get_processor().get_cores()):
            l2_cache = self._create_l2_cache()
            l2_node = self.add_root_child(f"l2-cache-{i}", l2_cache)

            l1i_node = l2_node.add_child(
                f"l1i-cache-{i}", L1ICache(size=self._l1i_size)
            )
            l1d_node = l2_node.add_child(
                f"l1d-cache-{i}", L1DCache(size=self._l1d_size)
            )

            self.l2buses[i].mem_side_ports = l2_node.cache.cpu_side
            self.membus.cpu_side_ports = l2_node.cache.mem_side

            l1i_node.cache.mem_side = self.l2buses[i].cpu_side_ports
            l1d_node.cache.mem_side = self.l2buses[i].cpu_side_ports

            cpu.connect_icache(l1i_node.cache.cpu_side)
            cpu.connect_dcache(l1d_node.cache.cpu_side)

            cpu.connect_walker_ports(
                self.l2buses[i].cpu_side_ports,
                self.l2buses[i].cpu_side_ports,
            )

            if board.get_processor().get_isa() == ISA.X86:
                int_req_port = self.membus.mem_side_ports
                int_resp_port = self.membus.cpu_side_ports
                cpu.connect_interrupt(int_req_port, int_resp_port)
            else:
                cpu.connect_interrupt()

        if board.has_coherent_io():
            self._setup_io_cache(board)

    def _setup_io_cache(self, board) -> None:
        self.iocache = Cache(
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            size="1KiB",
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges,
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

benchmark_choices = [
    "500.perlbench_r",
    "502.gcc_r",
    "503.bwaves_r",
    "505.mcf_r",
    "507.cactusBSSN_r",
    "508.namd_r",
    "510.parest_r",
    "511.povray_r",
    "519.lbm_r",
    "520.omnetpp_r",
    "521.wrf_r",
    "523.xalancbmk_r",
    "525.x264_r",
    "527.cam4_r",
    "531.deepsjeng_r",
    "538.imagick_r",
    "541.leela_r",
    "544.nab_r",
    "548.exchange2_r",
    "549.fotonik3d_r",
    "554.roms_r",
    "557.xz_r",
    "600.perlbench_s",
    "602.gcc_s",
    "603.bwaves_s",
    "605.mcf_s",
    "607.cactusBSSN_s",
    "608.namd_s",
    "610.parest_s",
    "611.povray_s",
    "619.lbm_s",
    "620.omnetpp_s",
    "621.wrf_s",
    "623.xalancbmk_s",
    "625.x264_s",
    "627.cam4_s",
    "631.deepsjeng_s",
    "638.imagick_s",
    "641.leela_s",
    "644.nab_s",
    "648.exchange2_s",
    "649.fotonik3d_s",
    "654.roms_s",
    "996.specrand_fs",
    "997.specrand_fr",
    "998.specrand_is",
    "999.specrand_ir",
]

size_choices = ["test", "train", "ref"]

parser = argparse.ArgumentParser(
    description="SPEC CPU2017 cache compression evaluation with KVM boot option."
)

parser.add_argument(
    "--benchmark",
    type=str,
    required=True,
    choices=benchmark_choices,
    help="SPEC CPU2017 benchmark to run.",
)

parser.add_argument(
    "--size",
    type=str,
    required=True,
    choices=size_choices,
    help="Input size for the benchmark (test/train/ref).",
)

parser.add_argument(
    "--compression",
    action="store_true",
    default=False,
    help="Enable compression on L2 cache (alias for --compressor bdi).",
)

parser.add_argument(
    "--compressor",
    type=str,
    default="none",
    choices=["none", "bdi", "cpack", "cpac", "fpc", "zero", "zerocompressor"],
    help="Compressor engine to use on L2 cache (none, bdi, cpack, fpc, zero).",
)

parser.add_argument(
    "--use-kvm",
    action="store_true",
    default=True,
    help="Use KVM for fast boot-up (requires /dev/kvm; fails if unavailable).",
)

parser.add_argument(
    "--l2-size",
    type=str,
    default="512KiB",
    help="L2 cache size (default: 512KiB).",
)

parser.add_argument(
    "--partition",
    type=str,
    required=False,
    default="1",
    help="Root partition of the SPEC disk image (default: 1).",
)

parser.add_argument(
    "--num-cores",
    type=int,
    required=False,
    default=1,
    help="Number of CPU cores (default: 1).",
)

parser.add_argument(
    "--fast-forward-insts",
    type=int,
    required=False,
    default=0,
    help="Number of instructions to fast-forward under KVM before warm-up (default: 0).",
)

parser.add_argument(
    "--warmup-insts",
    type=int,
    required=False,
    default=1_000_000,
    help="Number of instructions for warm-up phase (default: 1M).",
)

parser.add_argument(
    "--max-insts",
    type=int,
    required=False,
    default=10_000_000,
    help="Number of instructions for measured ROI (default: 10M).",
)

args = parser.parse_args()

# Normalize compressor choice
chosen_compressor = args.compressor.lower()
if chosen_compressor == "none" and args.compression:
    chosen_compressor = "bdi"


# ---------------------------------------------------------------------------
# Locate local resources (kernel & disk image)
# ---------------------------------------------------------------------------

_script_dir = os.path.dirname(os.path.abspath(__file__))
_gem5_root = os.path.dirname(_script_dir)
_project_root = os.path.dirname(_gem5_root)

_kernel_path = os.path.join(
    _project_root, "resource", "kernel", "vmlinux-4.19.83"
)
_disk_image_path = os.path.join(
    _project_root, "resource", "disk_image", "spec2017.img"
)

if not os.path.exists(_kernel_path):
    fatal(f"Kernel not found: {_kernel_path}")
if not os.path.exists(_disk_image_path):
    fatal(f"Disk image not found: {_disk_image_path}")

# Check KVM availability
if args.use_kvm:
    if not os.path.exists("/dev/kvm"):
        fatal(
            "[CompressionEval] /dev/kvm does not exist! Aborting rather than falling back to Atomic."
        )
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        fatal(
            "[CompressionEval] /dev/kvm exists but is not readable/writable! Check permissions. Aborting."
        )
    starting_cpu = CPUTypes.KVM
    print("[CompressionEval] KVM is verified and enabled for fast boot-up.")
else:
    starting_cpu = CPUTypes.ATOMIC
    print(
        "[CompressionEval] Using ATOMIC CPU for boot (KVM explicitly disabled)."
    )

print(f"[CompressionEval] Kernel:       {_kernel_path}")
print(f"[CompressionEval] Disk image:   {_disk_image_path}")
print(f"[CompressionEval] Benchmark:    {args.benchmark} ({args.size})")
print(f"[CompressionEval] L2 Cache:     {args.l2_size}")
print(f"[CompressionEval] Compressor:   {chosen_compressor.upper()}")
print(f"[CompressionEval] Boot CPU:     {starting_cpu.value}")
print(f"[CompressionEval] ROI CPU:      O3")
print(f"[CompressionEval] Fast-Forward: {args.fast_forward_insts:,} instructions")
print(f"[CompressionEval] Warmup:       {args.warmup_insts:,} instructions")
print(f"[CompressionEval] ROI Cap:      {args.max_insts:,} instructions")


# ---------------------------------------------------------------------------
# Build the system
# ---------------------------------------------------------------------------

cache_hierarchy = PrivateL1PrivateL2WithCompressionHierarchy(
    l1d_size="32KiB",
    l1i_size="32KiB",
    l2_size=args.l2_size,
    l2_assoc=16,
    compressor=chosen_compressor,
)

memory = DualChannelDDR4_2400(size="3GiB")

processor = SimpleSwitchableProcessor(
    starting_core_type=starting_cpu,
    switch_core_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=args.num_cores,
)

if starting_cpu == CPUTypes.KVM:
    for proc in processor.start:
        proc.core.usePerf = False

board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)


# ---------------------------------------------------------------------------
# Set up workload
# ---------------------------------------------------------------------------

output_dir = "speclogs_" + "".join(
    x.strip() for x in time.asctime().split()
).replace(":", "")
try:
    os.makedirs(os.path.join(m5.options.outdir, output_dir))
except FileExistsError:
    pass

command = f"{args.benchmark} {args.size} {output_dir}"

board.set_kernel_disk_workload(
    kernel=KernelResource(local_path=_kernel_path),
    disk_image=DiskImageResource(
        _disk_image_path, root_partition=args.partition
    ),
    readfile_contents=command,
)


# ---------------------------------------------------------------------------
# Exit event handling
# ---------------------------------------------------------------------------

from gem5.simulate.exit_event import ExitEvent


def spec_exit_event_handler():
    """Handles m5 exit events from SPEC runscript."""
    print("[CompressionEval] === BOOT COMPLETE ===")
    print("[CompressionEval] Dump #1: boot-phase stats")
    m5.stats.dump()
    m5.stats.reset()

    if args.fast_forward_insts > 0:
        print(
            f"[CompressionEval] Fast-forwarding {args.fast_forward_insts:,} insts under {starting_cpu.value}"
        )
        simulator.schedule_max_insts(args.fast_forward_insts)
        yield False

    print(f"[CompressionEval] Switching from {starting_cpu.value} -> O3CPU")
    processor.switch()

    print(
        f"[CompressionEval] Starting warm-up phase ({args.warmup_insts:,} insts)"
    )
    simulator.schedule_max_insts(args.warmup_insts)
    yield False

    print("[CompressionEval] === ROI END (benchmark finished before cap) ===")
    print("[CompressionEval] Dump #2: benchmark ROI stats")
    m5.stats.dump()
    yield True


def max_insts_exit_handler():
    """Multi-phase handler: end-of-fast-forward -> end-of-warmup -> end-of-ROI."""
    if args.fast_forward_insts > 0:
        print("[CompressionEval] === FAST-FORWARD COMPLETE ===")
        print(f"[CompressionEval] Switching from {starting_cpu.value} -> O3CPU")
        processor.switch()
        print(
            f"[CompressionEval] Starting warm-up phase ({args.warmup_insts:,} insts)"
        )
        simulator.schedule_max_insts(args.warmup_insts)
        yield False

    print("[CompressionEval] === WARM-UP COMPLETE ===")
    print("[CompressionEval] Resetting stats - beginning measured ROI")
    m5.stats.reset()
    print(
        f"[CompressionEval] Measurement cap: {args.max_insts:,} instructions"
    )
    simulator.schedule_max_insts(args.max_insts)
    yield False

    print(
        f"[CompressionEval] === ROI END (cap {args.max_insts:,} insts reached) ==="
    )
    print("[CompressionEval] Dump #2: benchmark ROI stats")
    m5.stats.dump()
    yield True


# ---------------------------------------------------------------------------
# Run simulation
# ---------------------------------------------------------------------------

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.EXIT: spec_exit_event_handler(),
        ExitEvent.MAX_INSTS: max_insts_exit_handler(),
    },
)

global_start = time.time()
print(f"[CompressionEval] Starting simulation ({starting_cpu.value} boot)...")
m5.stats.reset()

simulator.run()

global_end = time.time()
print(
    f"[CompressionEval] Simulation complete. "
    f"Wall-clock time: {global_end - global_start:.1f}s"
)
print(f"[CompressionEval] Stats written to: {m5.options.outdir}/stats.txt")
