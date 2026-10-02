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
Regression test script for cache recompression failure eviction with writebacks.
Verifies that dirty blocks that fail recompression in compressed caches
are evicted via evictBlock (producing dirty writebacks) rather than silently invalidated.
"""

import m5
from m5.objects import *

m5.util.addToPath("../../../configs/")
from common.Caches import *

# Set up simple memtest system with compressed cache
nb_cores = 1
cpus = [
    MemTest(max_loads=1000, progress_interval=200) for i in range(nb_cores)
]

system = System(cpu=cpus, physmem=SimpleMemory(), membus=SystemXBar())
system.voltage_domain = VoltageDomain()
system.clk_domain = SrcClockDomain(
    clock="1GHz", voltage_domain=system.voltage_domain
)

system.cpu_clk_domain = SrcClockDomain(
    clock="2GHz", voltage_domain=system.voltage_domain
)

system.l1c = L1Cache(size="8KiB", assoc=2)
system.l1c.compressor = BDI()
system.l1c.tags = CompressedTags()

system.l1c.cpu_side = cpus[0].port
system.l1c.mem_side = system.membus.cpu_side_ports

for cpu in cpus:
    cpu.clk_domain = system.cpu_clk_domain

system.system_port = system.membus.cpu_side_ports
system.physmem.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

m5.instantiate()
print("Beginning compressed cache recompression failure eviction test...")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}.")

# Print cache statistics
compressions = system.l1c.compressor.compressions.value
failed_compressions = system.l1c.compressor.failedCompressions.value

print(f"L1 Compressor Stats:")
print(f"  Compressions: {compressions}")
print(f"  Failed Compressions: {failed_compressions}")
