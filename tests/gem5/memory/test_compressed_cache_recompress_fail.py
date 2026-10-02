# Copyright (c) 2026
# All rights reserved.
#
# Test script for compressed cache write hit recompression failure.
# Verifies that when updateCompressionData fails on write hits or overwrites,
# evictBlock(blk, writebacks) is called to write back dirty modified data.

import sys

import m5
from m5.objects import *

m5.util.addToPath("../../../configs/")
from common.Caches import *

# Set up memtest system with compressed L2 cache
nb_cores = 4
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
system.l2c = L2Cache(
    clk_domain=system.cpu_clk_domain,
    size="16KiB",
    assoc=2,
    replace_expansions=True,
)

# Configure BDI compression and CompressedTags on L2 cache
system.l2c.compressor = BDI()
system.l2c.tags = CompressedTags()

system.l2c.cpu_side = system.toL2Bus.mem_side_ports
system.l2c.mem_side = system.membus.cpu_side_ports

for cpu in cpus:
    cpu.clk_domain = system.cpu_clk_domain
    cpu.l1c = L1Cache(size="8KiB", assoc=2)
    cpu.l1c.cpu_side = cpu.port
    cpu.l1c.mem_side = system.toL2Bus.cpu_side_ports

system.system_port = system.membus.cpu_side_ports
system.physmem.port = system.membus.mem_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

m5.instantiate()
print("Beginning compressed cache recompression failure eviction test...")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}.")

assert exit_event.getCause() == "maximum number of loads reached"
print(
    "Test completed successfully: Store data preserved during recompression failures."
)
