# Copyright (c) 2026
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
Regression test script for L2 to L1 cross-level compression backpressure signaling
and L1 writeback rate limiting.
Verifies cross-level backpressure communication and L1 dirty writeback pacing
under L2 adaptive compression bypass and write queue pressure.
"""

import sys

import m5
from m5.objects import *

m5.util.addToPath("../../../configs/")
from common.Caches import *

nb_cores = 2
cpus = [
    MemTest(max_loads=10000, progress_interval=2000) for i in range(nb_cores)
]

system = System(cpu=cpus, physmem=SimpleMemory(), membus=SystemXBar())
system.voltage_domain = VoltageDomain()
system.clk_domain = SrcClockDomain(
    clock="1GHz", voltage_domain=system.voltage_domain
)

system.cpu_clk_domain = SrcClockDomain(
    clock="2GHz", voltage_domain=system.voltage_domain
)

system.toL2Bus = L2XBar(clk_domain=system.cpu_clk_domain)
system.l2c = L2Cache(clk_domain=system.cpu_clk_domain, size="64KiB", assoc=8)

# Configure BDI compression with adaptive bypass and hysteresis on L2
system.l2c.compressor = BDI(
    enable_adaptive_bypass=True,
    latency_breakeven_threshold=1.0,
    hysteresis_margin=0.05,
    sampling_interval=10,
)
system.l2c.tags = CompressedTags(sector_eviction_threshold=5)

system.l2c.cpu_side = system.toL2Bus.mem_side_ports
system.l2c.mem_side = system.membus.cpu_side_ports

for cpu in cpus:
    cpu.clk_domain = system.cpu_clk_domain
    cpu.l1c = L1Cache(
        size="16KiB",
        assoc=4,
        writeback_throttle_interval=10,
        max_queue_residence_time=100,
    )
    cpu.l1c.cpu_side = cpu.port
    cpu.l1c.mem_side = system.toL2Bus.cpu_side_ports

system.system_port = system.membus.cpu_side_ports
system.physmem.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

m5.instantiate()
print("Beginning L2 cross-level compression backpressure test...")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}.")

compressions = system.l2c.compressor.compressions.value
bypassed = system.l2c.compressor.bypassedCompressions.value

print(f"L2 Compressor Backpressure Stats:")
print(f"  Compressions: {compressions}")
print(f"  Bypassed Compressions: {bypassed}")
