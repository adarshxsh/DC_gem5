# Copyright (c) 2025
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
SPEC CPU2017 Cache Compression Evaluation Script
=================================================

Evaluates the performance impact of BDI (Base-Delta-Immediate) cache
compression in gem5 x86 full-system mode.

Uses a Classic cache hierarchy (not Ruby) so that gem5's built-in
cache compressor infrastructure can be used. Atomic CPU boot
with switch to O3 CPU for the measured region of interest (ROI).

Note: Original script used KVM boot which requires Linux x86 host.
This version uses AtomicSimpleCPU for boot (slower but portable).

The SPEC2017 disk image's runscript.sh places ``m5 exit`` calls
before and after the benchmark ROI. This script handles those exits
to switch CPUs and collect stats only for the ROI.

Usage
-----
Baseline (no compression):
    ./build/ALL/gem5.opt configs/spec2017_compression_eval.py \\
        --benchmark 505.mcf_r --size test

BDI compression enabled:
    ./build/ALL/gem5.opt -d m5out_bdi configs/spec2017_compression_eval.py \\
        --benchmark 505.mcf_r --size test --compression
"""

import argparse
import os
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
from m5.util import fatal, warn

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

    Extends the stock PrivateL1PrivateL2CacheHierarchy with an optional
    ``compressor`` on the L2 cache. When a compressor is provided, the
    L2 tags are switched to ``CompressedTags`` (superblock layout) so
    compressed lines can be co-allocated, effectively increasing L2
    capacity for compressible data.
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
        l1d_size: str,
        l1i_size: str,
        l2_size: str,
        l2_assoc: int = 16,
        use_compression: bool = False,
        membus: Optional[BaseXBar] = None,
    ) -> None:
        """
        :param l1d_size: L1 Data Cache size (e.g. "32KiB").
        :param l1i_size: L1 Instruction Cache size (e.g. "32KiB").
        :param l2_size: L2 Cache size (e.g. "256KiB").
        :param l2_assoc: L2 Cache associativity.
        :param use_compression: If True, attach BDI compressor and
            CompressedTags to the L2 cache.
        :param membus: Optional memory bus override.
        """
        AbstractClassicCacheHierarchy.__init__(self=self)
        AbstractTwoLevelCacheHierarchy.__init__(
            self,
            l1i_size=l1i_size,
            l1i_assoc=8,
            l1d_size=l1d_size,
            l1d_assoc=8,
            l2_size=l2_size,
            l2_assoc=l2_assoc,
        )

        self._use_compression = use_compression
        self.membus = membus if membus else self._get_default_membus()

    @overrides(AbstractClassicCacheHierarchy)
    def get_mem_side_port(self) -> Port:
        return self.membus.mem_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def get_cpu_side_port(self) -> Port:
        return self.membus.cpu_side_ports

    def _create_l2_cache(self) -> L2Cache:
        """Create an L2 cache, optionally with BDI compression."""
        l2 = L2Cache(size=self._l2_size, assoc=self._l2_assoc)

        if self._use_compression:
            # Import compression-related SimObjects
            from m5.objects import BDI, CompressedTags

            l2.compressor = BDI()
            l2.tags = CompressedTags()
            print(
                "[CompressionEval] L2 cache configured with BDI compressor "
                "and CompressedTags (max_compression_ratio=2)"
            )
        else:
            print("[CompressionEval] L2 cache configured without compression (baseline)")

        return l2

    @overrides(AbstractCacheHierarchy)
    def incorporate_cache(self, board) -> None:
        # Set up the system port for functional access from the simulator.
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
        """Create a cache for coherent I/O connections."""
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
    description="SPEC CPU2017 cache compression evaluation with gem5 "
    "(Classic cache hierarchy, KVM boot, BDI compression toggle)."
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
    help="Enable BDI compression on the L2 cache. "
    "Without this flag, runs in baseline (uncompressed) mode.",
)

parser.add_argument(
    "--partition",
    type=str,
    required=False,
    default=None,
    help="Root partition of the SPEC disk image. "
    'Pass "" for un-partitioned images.',
)

parser.add_argument(
    "--num-cores",
    type=int,
    required=False,
    default=1,
    help="Number of CPU cores (default: 1).",
)

parser.add_argument(
    "--warmup-insts",
    type=int,
    required=False,
    default=10_000_000,
    help="Number of instructions to run after CPU switch before resetting "
    "stats (warm-up phase, default: 10M). Fills caches and branch "
    "predictor so the measured phase reflects steady-state behaviour.",
)

parser.add_argument(
    "--max-insts",
    type=int,
    required=False,
    default=50_000_000,
    help="Number of instructions to measure after the warm-up phase "
    "(default: 50M). Stats are dumped after this many instructions "
    "or when the benchmark finishes, whichever comes first.",
)

args = parser.parse_args()


# ---------------------------------------------------------------------------
# Locate local resources (kernel & disk image)
# ---------------------------------------------------------------------------

# Resolve paths relative to this script's location
# configs/ is inside gem5/ which is inside dcProject/
_script_dir = os.path.dirname(os.path.abspath(__file__))  # .../gem5/configs
_gem5_root = os.path.dirname(_script_dir)                 # .../gem5
_project_root = os.path.dirname(_gem5_root)               # .../dcProject

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

print(f"[CompressionEval] Kernel:     {_kernel_path}")
print(f"[CompressionEval] Disk image: {_disk_image_path}")
print(f"[CompressionEval] Benchmark:  {args.benchmark} ({args.size})")
print(f"[CompressionEval] Compression: {'BDI' if args.compression else 'OFF (baseline)'}")
print(f"[CompressionEval] Cores:      {args.num_cores}")
print(f"[CompressionEval] Warmup:     {args.warmup_insts:,} instructions")
print(f"[CompressionEval] Max insts:  {args.max_insts:,} (measured ROI)")


# ---------------------------------------------------------------------------
# Verify gem5 build requirements
# ---------------------------------------------------------------------------

# Note: KVM boot removed for Apple Silicon compatibility.
# Using AtomicSimpleCPU for boot instead (slower but portable).


# ---------------------------------------------------------------------------
# Build the system
# ---------------------------------------------------------------------------

# Cache hierarchy: Classic L1+L2, with optional BDI on L2
cache_hierarchy = PrivateL1PrivateL2WithCompressionHierarchy(
    l1d_size="32KiB",
    l1i_size="32KiB",
    l2_size="512KiB",
    l2_assoc=16,
    use_compression=args.compression,
)

# Memory: Dual Channel DDR4 2400, 3 GiB (X86Board hard limit)
memory = DualChannelDDR4_2400(size="3GiB")

# Processor: Atomic for boot → O3CPU for ROI measurement
# (Original used KVM boot; Atomic is slower but works on Apple Silicon)
processor = SimpleSwitchableProcessor(
    starting_core_type=CPUTypes.ATOMIC,
    switch_core_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=args.num_cores,
)

# Board: X86 full-system
board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)


# ---------------------------------------------------------------------------
# Set up the workload
# ---------------------------------------------------------------------------

# Output directory for SPEC results copied off the disk image
output_dir = "speclogs_" + "".join(x.strip() for x in time.asctime().split())
output_dir = output_dir.replace(":", "")

try:
    os.makedirs(os.path.join(m5.options.outdir, output_dir))
except FileExistsError:
    warn("Output directory already exists!")

# The readfile_contents is passed to the disk image's runscript.sh,
# which calls the benchmark with m5 exit markers around the ROI.
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
# The SPEC2017 disk image's runscript.sh uses generic `m5 exit` calls
# (not m5 workbegin / m5 workend). These map to ExitEvent.EXIT events.
#
# Simulation phases and stat dumps:
#
#   Phase 0 - KVM boot:
#       Stats collected from simulation start → 1st m5 exit.
#       Dump #1 written at the 1st EXIT (boot stats).
#
#   Phase 1 - O3CPU warm-up (--warmup-insts, default 10M):
#       CPU switches to O3, stats reset. Caches and branch predictor
#       heat up so the measured phase reflects steady-state behaviour.
#
#   Phase 2 - O3CPU measurement (--max-insts, default 50M):
#       Stats reset after warmup. Simulation runs until --max-insts
#       committed instructions OR the benchmark's 2nd m5 exit.
#       Dump #2 written at that point (benchmark ROI stats).

from gem5.simulate.exit_event import ExitEvent


def spec_exit_event_handler():
    """Generator handling the two m5 exit events from the SPEC runscript."""
    # ------------------------------------------------------------------ #
    # 1st m5 exit: Linux boot complete.                                   #
    # Dump boot-phase stats, then switch to O3 and start warm-up.        #
    # ------------------------------------------------------------------ #
    print("[CompressionEval] === BOOT COMPLETE ===")
    print("[CompressionEval] Dump #1: boot-phase stats")
    m5.stats.dump()   # <-- Dump #1: boot stats
    m5.stats.reset()

    print("[CompressionEval] Switching from KVM -> O3CPU")
    processor.switch()

    print(f"[CompressionEval] Starting warm-up phase ({args.warmup_insts:,} insts)")
    simulator.schedule_max_insts(args.warmup_insts)
    yield False  # Continue simulation (warm-up running)

    # ------------------------------------------------------------------ #
    # 2nd m5 exit: benchmark finished before the instruction cap.         #
    # ------------------------------------------------------------------ #
    print("[CompressionEval] === ROI END (benchmark finished before cap) ===")
    print("[CompressionEval] Dump #2: benchmark ROI stats")
    m5.stats.dump()   # <-- Dump #2: benchmark ROI stats
    yield True  # Exit simulation


def max_insts_exit_handler():
    """Two-phase handler: first fires end-of-warmup, second fires end-of-ROI."""
    # ------------------------------------------------------------------ #
    # First MAX_INSTS event: warm-up complete.                           #
    # Reset stats and schedule the measurement cap.                       #
    # ------------------------------------------------------------------ #
    print("[CompressionEval] === WARM-UP COMPLETE ===")
    print("[CompressionEval] Resetting stats - beginning measured ROI")
    m5.stats.reset()
    print(f"[CompressionEval] Measurement cap: {args.max_insts:,} instructions")
    simulator.schedule_max_insts(args.max_insts)
    yield False  # Continue simulation (measurement running)

    # ------------------------------------------------------------------ #
    # Second MAX_INSTS event: measurement cap reached.                   #
    # ------------------------------------------------------------------ #
    print(
        f"[CompressionEval] === ROI END "
        f"(measurement cap {args.max_insts:,} insts reached) ==="
    )
    print("[CompressionEval] Dump #2: benchmark ROI stats")
    m5.stats.dump()   # <-- Dump #2: benchmark ROI stats
    yield True  # Stop simulation


# ---------------------------------------------------------------------------
# Run the simulation
# ---------------------------------------------------------------------------

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.EXIT: spec_exit_event_handler(),
        ExitEvent.MAX_INSTS: max_insts_exit_handler(),
    },
)

global_start = time.time()

print("[CompressionEval] Starting simulation (KVM boot)...")
m5.stats.reset()

simulator.run()

global_end = time.time()
print(
    f"[CompressionEval] Simulation complete. "
    f"Wall-clock time: {global_end - global_start:.1f}s"
)
print(f"[CompressionEval] Stats written to: {m5.options.outdir}/stats.txt")
print(
    f"[CompressionEval] Terminal log: {m5.options.outdir}/board.pc.com_1.device"
)
