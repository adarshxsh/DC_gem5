# Copyright (c) 2026
# All rights reserved.

import argparse
import sys
import m5
from m5.objects import *

parser = argparse.ArgumentParser(description="QoS Compression and Queue Pressure Test")
parser.add_argument("--compression-aware", action="store_true", help="Enable compression awareness")
parser.add_argument("--pressure-gradient", action="store_true", help="Enable pressure gradient turnaround")
parser.add_argument("--q-policy", type=str, default="fifo", choices=["fifo", "lifo", "lrg", "cp"], help="Queue selection policy")

args = parser.parse_args()

nb_cores = 2
cpus = [
    MemTest(max_loads=1000, progress_interval=200) for i in range(nb_cores)
]

# Set up system
system = System(cpu=cpus, physmem=SimpleMemory(), membus=SystemXBar())
system.voltage_domain = VoltageDomain()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=system.voltage_domain)
system.cpu_clk_domain = SrcClockDomain(clock="2GHz", voltage_domain=system.voltage_domain)

# Configure QoS Policy
policy = QoSFixedPriorityPolicy(
    enable_compression_awareness=args.compression_aware,
    qos_fixed_prio_default_prio=1
)

# Configure Turnaround Policy
turnaround = QoSTurnaroundPolicyIdeal(
    enable_pressure_gradient=args.pressure_gradient,
    hysteresis_threshold=0.1
)

# Configure Memory Controller with QoS
qos_ctrl = QoSMemSinkCtrl(
    qos_priorities=4,
    qos_policy=policy,
    qos_turnaround_policy=turnaround,
    qos_q_policy=args.q_policy,
    qos_pressure_gradient=args.pressure_gradient,
    interface=QoSMemSinkInterface()
)

system.system_port = system.membus.cpu_side_ports
system.physmem.port = system.membus.mem_side_ports

for cpu in cpus:
    cpu.clk_domain = system.cpu_clk_domain
    cpu.port = system.membus.cpu_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"

m5.instantiate()
print(f"Beginning QoS Test (compression_aware={args.compression_aware}, pressure_gradient={args.pressure_gradient}, q_policy={args.q_policy})...")
exit_event = m5.simulate()
print(f"Exiting @ tick {m5.curTick()} because {exit_event.getCause()}.")
