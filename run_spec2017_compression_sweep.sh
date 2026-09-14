#!/usr/bin/env bash
# ==============================================================================
# SPEC CPU2017 Compression Evaluation Experiment Runner
# Sweeps across:
#   Benchmarks: 541.leela_r, 502.gcc_r, 538.imagick_r, 500.perlbench_r
#   L2 Cache Sizes: 256KiB, 512KiB, 1024KiB (1MiB)
#   Compressors: none (baseline), bdi, cpack, fpc, zero
# Fast-Forward: 10B instructions in KVM
# Warmup: 50M instructions
# ROI: 500M instructions in O3CPU
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GEM5_ROOT="$SCRIPT_DIR"
GEM5_BIN="${GEM5_BIN:-$GEM5_ROOT/build/X86/gem5.opt}"
CONFIG_SCRIPT="$GEM5_ROOT/configs/spec2017_compression_kvm.py"
OUTPUT_BASE="$GEM5_ROOT/m5out_eval_sweep"

mkdir -p "$OUTPUT_BASE"

# Parameters
BENCHMARKS=("541.leela_r" "502.gcc_r" "538.imagick_r" "500.perlbench_r")
CACHE_SIZES=("256KiB" "512KiB" "1024KiB")
COMPRESSORS=("none" "bdi" "cpack" "fpc" "zero")
SIZE="ref"
FAST_FORWARD_INSTS=10000000000  # 10 Billion
WARMUP_INSTS=50000000           # 50 Million
MAX_INSTS=500000000             # 500 Million

echo "================================================================================"
echo "SPEC CPU2017 Cache Compression Evaluation Sweep"
echo "Binary:         $GEM5_BIN"
echo "Config:         $CONFIG_SCRIPT"
echo "Output Root:    $OUTPUT_BASE"
echo "Fast-Forward:   $FAST_FORWARD_INSTS insts (KVM)"
echo "Warmup:         $WARMUP_INSTS insts (O3)"
echo "Measured ROI:   $MAX_INSTS insts (O3)"
echo "================================================================================"

run_simulation() {
    local bench="$1"
    local csize="$2"
    local comp="$3"
    local write_high="${4:-${WRITE_HIGH_THRESH:-85}}"
    local write_low="${5:-${WRITE_LOW_THRESH:-50}}"
    local enable_throttling="${6:-${ENABLE_PRESSURE_THROTTLING:-false}}"

    local outdir="$OUTPUT_BASE/${bench}_${csize}_${comp}_wh${write_high}_wl${write_low}"
    mkdir -p "$outdir"

    echo ""
    echo "--------------------------------------------------------------------------------"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Starting: Benchmark=$bench | L2=$csize | Compressor=$comp | WriteHigh=$write_high% | WriteLow=$write_low% | PressureThrottling=$enable_throttling"
    echo "OutDir: $outdir"
    echo "--------------------------------------------------------------------------------"
    local cmd_args=(
        --benchmark="$bench"
        --size="$SIZE"
        --l2-size="$csize"
        --compressor="$comp"
        --use-kvm
        --fast-forward-insts="$FAST_FORWARD_INSTS"
        --warmup-insts="$WARMUP_INSTS"
        --max-insts="$MAX_INSTS"
        --write-high-thresh="$write_high"
        --write-low-thresh="$write_low"
    )

    if [ "$enable_throttling" = "true" ] || [ "$enable_throttling" = "1" ]; then
        cmd_args+=(--enable-pressure-throttling)
    fi

    "$GEM5_BIN" \
        --outdir="$outdir" \
        "$CONFIG_SCRIPT" \
        "${cmd_args[@]}" \
        2>&1 | tee "$outdir/sim_run.log"
}

# If arguments passed, allow running a single target, e.g.:
# ./run_spec2017_compression_sweep.sh 541.leela_r 256KiB cpack 85 50 true
if [ "$#" -ge 3 ]; then
    run_simulation "$1" "$2" "$3" "$4" "$5" "$6"
    exit 0
fi

echo "Usage:"
echo "  $0 <benchmark> <l2_size> <compressor> [write_high_thresh] [write_low_thresh] [enable_pressure_throttling]"
echo "  Example: $0 541.leela_r 256KiB cpack 85 50 true"
echo ""
echo "Available Benchmarks: ${BENCHMARKS[*]}"
echo "Available Cache Sizes: ${CACHE_SIZES[*]}"
echo "Available Compressors: ${COMPRESSORS[*]}"
