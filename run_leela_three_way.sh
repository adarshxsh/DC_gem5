#!/usr/bin/env bash
# ==============================================================================
# Three-Way SPEC CPU2017 541.leela_r Evaluation: Baseline vs BDI vs CPack
# Scale: 40M Warmup | 200M ROI Instructions on DerivO3CPU with KVM Fast-Boot
# High-Compressibility Reference Workload (Monte Carlo Tree Search Go Engine)
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ ! -f "build/X86/gem5.opt" ]; then
    echo "ERROR: build/X86/gem5.opt not found!"
    exit 1
fi

echo "===================================================================="
echo "Phase 1: Initializing 3-Way Simulation for 541.leela_r"
echo "Workload:    SPEC CPU2017 541.leela_r (test)"
echo "Warmup:      40,000,000 instructions"
echo "ROI Cap:     200,000,000 instructions"
echo "Engines:     1) Baseline (none) | 2) BDI | 3) CPack"
echo "===================================================================="

mkdir -p experiments/leela_baseline_200/gem5
mkdir -p experiments/leela_bdi_200/gem5
mkdir -p experiments/leela_cpack_200/gem5

# 1. Launch Baseline (Uncompressed)
echo "[1/3] Launching Baseline (Uncompressed) simulation..."
nohup ./build/X86/gem5.opt \
  -d experiments/leela_baseline_200/gem5 \
  configs/spec2017_compression_kvm.py \
  --benchmark 541.leela_r \
  --size test \
  --warmup-insts 40000000 \
  --max-insts 200000000 \
  --use-kvm \
  --compressor none \
  > experiments/leela_baseline_200/run.log 2>&1 &
BASE_PID=$!
echo "  -> Baseline PID: $BASE_PID"
echo "  -> Log: experiments/leela_baseline_200/run.log"

# 2. Launch BDI (Pattern-based Compression)
echo "[2/3] Launching BDI (Base-Delta-Immediate) simulation..."
nohup ./build/X86/gem5.opt \
  -d experiments/leela_bdi_200/gem5 \
  configs/spec2017_compression_kvm.py \
  --benchmark 541.leela_r \
  --size test \
  --warmup-insts 40000000 \
  --max-insts 200000000 \
  --use-kvm \
  --compressor bdi \
  > experiments/leela_bdi_200/run.log 2>&1 &
BDI_PID=$!
echo "  -> BDI PID: $BDI_PID"
echo "  -> Log: experiments/leela_bdi_200/run.log"

# 3. Launch CPack (Dictionary-based Compression)
echo "[3/3] Launching CPack (Dictionary Compressor) simulation..."
nohup ./build/X86/gem5.opt \
  -d experiments/leela_cpack_200/gem5 \
  configs/spec2017_compression_kvm.py \
  --benchmark 541.leela_r \
  --size test \
  --warmup-insts 40000000 \
  --max-insts 200000000 \
  --use-kvm \
  --compressor cpack \
  > experiments/leela_cpack_200/run.log 2>&1 &
CPACK_PID=$!
echo "  -> CPack PID: $CPACK_PID"
echo "  -> Log: experiments/leela_cpack_200/run.log"

echo ""
echo "===================================================================="
echo "All 3 simulations are running in parallel in the background!"
echo "To monitor Baseline progress: tail -f experiments/leela_baseline_200/run.log"
echo "To monitor BDI progress:      tail -f experiments/leela_bdi_200/run.log"
echo "To monitor CPack progress:    tail -f experiments/leela_cpack_200/run.log"
echo "===================================================================="
