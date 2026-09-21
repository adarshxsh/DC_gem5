#!/usr/bin/env python3
import sys

files = {
    "Baseline": "experiments/leela_baseline_200/gem5/stats.txt",
    "BDI": "experiments/leela_bdi_200/gem5/stats.txt",
    "CPack": "experiments/leela_cpack_200/gem5/stats.txt",
}


def parse_dump2(filepath):
    metrics = {}
    with open(filepath, encoding="utf-8", errors="ignore") as f:
        content = f.read()
    dumps = content.split("---------- End Simulation Statistics   ----------")
    # Dump 0 = Boot phase, Dump 1 = ROI phase
    target = dumps[1] if len(dumps) > 1 else dumps[0]
    for line in target.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2:
            metrics[parts[0]] = parts[1]
    return metrics


data = {name: parse_dump2(path) for name, path in files.items()}

print("=" * 95)
print(f"{'Metric':<40} | {'Baseline':<15} | {'BDI':<15} | {'CPack':<15}")
print("=" * 95)


def print_row(title, key, fmt=None):
    b_val = data["Baseline"].get(key, "N/A")
    bdi_val = data["BDI"].get(key, "N/A")
    cp_val = data["CPack"].get(key, "N/A")
    if fmt == "float":
        try:
            b_val = f"{float(b_val):.6f}"
        except:
            pass
        try:
            bdi_val = f"{float(bdi_val):.6f}"
        except:
            pass
        try:
            cp_val = f"{float(cp_val):.6f}"
        except:
            pass
    elif fmt == "int":
        try:
            b_val = f"{int(float(b_val)):,}"
        except:
            pass
        try:
            bdi_val = f"{int(float(bdi_val)):,}"
        except:
            pass
        try:
            cp_val = f"{int(float(cp_val)):,}"
        except:
            pass
    print(f"{title:<40} | {b_val:<15} | {bdi_val:<15} | {cp_val:<15}")


print_row(
    "Committed Instructions (ROI)",
    "board.processor.switch.core.commitStats0.numInsts",
    "int",
)
print_row(
    "CPU Simulated Cycles", "board.processor.switch.core.numCycles", "int"
)
print_row(
    "Instructions Per Cycle (IPC)", "board.processor.switch.core.ipc", "float"
)
print_row(
    "Cycles Per Instruction (CPI)", "board.processor.switch.core.cpi", "float"
)
print_row(
    "L2 Accesses",
    "board.cache_hierarchy.l2-cache-0.demandAccesses::total",
    "int",
)
print_row(
    "L2 Misses", "board.cache_hierarchy.l2-cache-0.demandMisses::total", "int"
)
print_row(
    "L2 Miss Rate",
    "board.cache_hierarchy.l2-cache-0.demandMissRate::total",
    "float",
)
print_row(
    "L2 Replacements (Evictions)",
    "board.cache_hierarchy.l2-cache-0.replacements",
    "int",
)
print_row(
    "Zero-Eviction Co-allocations",
    "board.cache_hierarchy.l2-cache-0.tags.evictionsReplacement::0",
    "int",
)
print_row(
    "Compressions Evaluated",
    "board.cache_hierarchy.l2-cache-0.compressor.compressions",
    "int",
)
print_row(
    "Incompressible Blocks (512b)",
    "board.cache_hierarchy.l2-cache-0.compressor.compressionSize::512",
    "int",
)
print_row(
    "2:1 Compressed Blocks (256b)",
    "board.cache_hierarchy.l2-cache-0.compressor.compressionSize::256",
    "int",
)
print_row(
    "16:1 Compressed Blocks (32b)",
    "board.cache_hierarchy.l2-cache-0.compressor.compressionSize::32",
    "int",
)
print_row(
    "Zero Blocks (0b)",
    "board.cache_hierarchy.l2-cache-0.compressor.compressionSize::0",
    "int",
)
print_row(
    "Average Compressed Size (bits)",
    "board.cache_hierarchy.l2-cache-0.compressor.avgCompressionSizeBits",
    "float",
)
print_row(
    "Observed Compression Ratio",
    "board.cache_hierarchy.l2-cache-0.compressor.observedCompressionRatio",
    "float",
)
print("=" * 95)
