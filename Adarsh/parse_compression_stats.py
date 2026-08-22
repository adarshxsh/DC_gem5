#!/usr/bin/env python3
"""
gem5 Statistics Parser for Cache Compression Evaluation
Compares baseline vs compressed runs and calculates key metrics:
- IPC / CPI
- Cache Miss Rates (L1I, L1D, L2)
- Compression Ratios and Chunk Allocations
- Memory Traffic (Bytes / Packets)
"""

import sys
import os
import re
from typing import Dict, Any, Optional

def parse_stats_file(filepath: str) -> Dict[str, float]:
    """Parse gem5 stats.txt file and extract numerical metrics."""
    stats = {}
    if not os.path.exists(filepath):
        print(f"Warning: {filepath} does not exist.")
        return stats

    with open(filepath, 'r') as f:
        # Note: in multi-phase runs with m5.stats.dump(), we want the final ROI dump
        # We read all dumps and retain the latest values for the ROI
        current_dump = {}
        for line in f:
            line = line.strip()
            if not line or line.startswith("---"):
                if "End Simulation Statistics" in line and current_dump:
                    stats = current_dump.copy()
                continue
            
            parts = line.split()
            if len(parts) >= 2:
                stat_name = parts[0]
                stat_val_str = parts[1]
                try:
                    # Handle nan / inf / numbers
                    if stat_val_str == "nan":
                        stat_val = float('nan')
                    elif stat_val_str == "inf":
                        stat_val = float('inf')
                    else:
                        stat_val = float(stat_val_str)
                    current_dump[stat_name] = stat_val
                except ValueError:
                    pass
        if current_dump and not stats:
            stats = current_dump
    return stats

def get_metric(stats: Dict[str, float], keys: list, default: float = 0.0) -> float:
    for k in keys:
        if k in stats:
            return stats[k]
    # Check regex match if exact key not found
    for pattern in keys:
        for k, v in stats.items():
            if re.search(pattern, k):
                return v
    return default

def print_comparison(baseline_stats: Dict[str, float], bdi_stats: Dict[str, float]):
    print("=" * 80)
    print(f"{'Metric':<45} | {'Baseline':<14} | {'BDI Compressed':<14} | {'Delta (%)':<10}")
    print("=" * 80)

    # Core Performance
    base_ipc = get_metric(baseline_stats, ['board.processor.cores.core.ipc', 'sim_insts / sim_ticks', 'ipc'])
    bdi_ipc = get_metric(bdi_stats, ['board.processor.cores.core.ipc', 'sim_insts / sim_ticks', 'ipc'])
    base_cpi = get_metric(baseline_stats, ['board.processor.cores.core.cpi', 'cpi'])
    bdi_cpi = get_metric(bdi_stats, ['board.processor.cores.core.cpi', 'cpi'])
    if base_cpi == 0.0 and base_ipc > 0:
        base_cpi = 1.0 / base_ipc
    if bdi_cpi == 0.0 and bdi_ipc > 0:
        bdi_cpi = 1.0 / bdi_ipc

    sim_insts_base = get_metric(baseline_stats, ['simInsts', 'sim_insts'])
    sim_insts_bdi = get_metric(bdi_stats, ['simInsts', 'sim_insts'])
    
    sim_seconds_base = get_metric(baseline_stats, ['simSeconds', 'sim_seconds'])
    sim_seconds_bdi = get_metric(bdi_stats, ['simSeconds', 'sim_seconds'])

    def format_row(name: str, base_val: float, bdi_val: float, is_ratio: bool = False, higher_is_better: bool = True):
        delta_str = "N/A"
        if base_val > 0:
            delta = ((bdi_val - base_val) / base_val) * 100.0
            sign = "+" if delta > 0 else ""
            delta_str = f"{sign}{delta:.2f}%"
        
        if is_ratio:
            print(f"{name:<45} | {base_val:<14.4f} | {bdi_val:<14.4f} | {delta_str:<10}")
        else:
            print(f"{name:<45} | {base_val:<14.0f} | {bdi_val:<14.0f} | {delta_str:<10}")

    print("--- Core Execution ---")
    format_row("Simulated Instructions (ROI)", sim_insts_base, sim_insts_bdi, False)
    format_row("IPC (Instructions Per Cycle)", base_ipc, bdi_ipc, True, True)
    format_row("CPI (Cycles Per Instruction)", base_cpi, bdi_cpi, True, False)
    format_row("Simulated Time (seconds)", sim_seconds_base, sim_seconds_bdi, True, False)

    print("\n--- L1 Caches ---")
    base_l1d_misses = get_metric(baseline_stats, ['.*l1d.*overallMisses::total', '.*l1d-cache.*demandMisses::total'])
    bdi_l1d_misses = get_metric(bdi_stats, ['.*l1d.*overallMisses::total', '.*l1d-cache.*demandMisses::total'])
    base_l1d_mshr = get_metric(baseline_stats, ['.*l1d.*overallMshrMisses::total'])
    bdi_l1d_mshr = get_metric(bdi_stats, ['.*l1d.*overallMshrMisses::total'])
    format_row("L1D Overall Misses", base_l1d_misses, bdi_l1d_misses, False, False)
    format_row("L1D MSHR Misses", base_l1d_mshr, bdi_l1d_mshr, False, False)

    print("\n--- L2 Cache ---")
    base_l2_accesses = get_metric(baseline_stats, ['.*l2.*overallAccesses::total', '.*l2-cache.*demandAccesses::total'])
    bdi_l2_accesses = get_metric(bdi_stats, ['.*l2.*overallAccesses::total', '.*l2-cache.*demandAccesses::total'])
    base_l2_misses = get_metric(baseline_stats, ['.*l2.*overallMisses::total', '.*l2-cache.*demandMisses::total'])
    bdi_l2_misses = get_metric(bdi_stats, ['.*l2.*overallMisses::total', '.*l2-cache.*demandMisses::total'])
    base_l2_miss_rate = get_metric(baseline_stats, ['.*l2.*overallMissRate::total', '.*l2-cache.*demandMissRate::total'])
    bdi_l2_miss_rate = get_metric(bdi_stats, ['.*l2.*overallMissRate::total', '.*l2-cache.*demandMissRate::total'])
    if base_l2_miss_rate == 0.0 and base_l2_accesses > 0:
        base_l2_miss_rate = base_l2_misses / base_l2_accesses
    if bdi_l2_miss_rate == 0.0 and bdi_l2_accesses > 0:
        bdi_l2_miss_rate = bdi_l2_misses / bdi_l2_accesses

    format_row("L2 Accesses", base_l2_accesses, bdi_l2_accesses, False)
    format_row("L2 Misses", base_l2_misses, bdi_l2_misses, False, False)
    format_row("L2 Miss Rate", base_l2_miss_rate, bdi_l2_miss_rate, True, False)

    print("\n--- Compression Metrics (BDI) ---")
    bdi_comp_ratio = get_metric(bdi_stats, ['.*compressor.*compression_ratio', '.*compressor.*compressionRatio', '.*compression_ratio'])
    bdi_comp_blks = get_metric(bdi_stats, ['.*compressor.*compressed_blocks', '.*compressor.*total_compressed_blocks'])
    bdi_uncomp_blks = get_metric(bdi_stats, ['.*compressor.*uncompressed_blocks'])
    print(f"{'Compression Ratio':<45} | {'1.0000 (N/A)':<14} | {bdi_comp_ratio:<14.4f} | {'N/A':<10}")
    print(f"{'Compressed Cache Blocks':<45} | {'0':<14} | {bdi_comp_blks:<14.0f} | {'N/A':<10}")
    print(f"{'Uncompressed Blocks':<45} | {'N/A':<14} | {bdi_uncomp_blks:<14.0f} | {'N/A':<10}")
    print("=" * 80)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 parse_compression_stats.py <m5out_baseline/stats.txt> <m5out_bdi/stats.txt>")
        sys.exit(1)
    
    base_file = sys.argv[1]
    bdi_file = sys.argv[2]
    
    base_stats = parse_stats_file(base_file)
    bdi_stats = parse_stats_file(bdi_file)
    
    print_comparison(base_stats, bdi_stats)
