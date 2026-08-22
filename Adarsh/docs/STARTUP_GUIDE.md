# SPEC2017 L2 Cache Compression (BDI) — Startup & Run Guide

## 1. Quick Start Commands

### Build gem5 (X86)
```bash
scons build/X86/gem5.opt -j$(nproc)
```

### Baseline Experiment (No Compression)
```bash
mkdir -p experiments/baseline_10m/gem5
./build/X86/gem5.opt \
  -d experiments/baseline_10m/gem5 \
  configs/spec2017_compression_kvm.py \
  --benchmark 505.mcf_r \
  --size test \
  --warmup-insts 1000000 \
  --max-insts 10000000 \
  --use-kvm \
  > experiments/baseline_10m/run.log 2>&1 &
```

### BDI Compression Experiment (BDI + CompressedTags)
```bash
mkdir -p experiments/bdi_10m/gem5
./build/X86/gem5.opt \
  -d experiments/bdi_10m/gem5 \
  configs/spec2017_compression_kvm.py \
  --benchmark 505.mcf_r \
  --size test \
  --warmup-insts 1000000 \
  --max-insts 10000000 \
  --use-kvm \
  --compression \
  > experiments/bdi_10m/run.log 2>&1 &
```

---

## 2. Execution Flow & Architecture

```text
Host (GCP VM with KVM nested virtualization)
 │
 ▼
Phase 1: KVM Fast Boot (X86KvmCPU)
 ├── Boots guest Ubuntu OS & mounts SPEC2017 disk image (/dev/hda1)
 ├── Launches benchmark workload (505.mcf_r)
 └── Reaches ROI start -> triggers m5 exit
 │
 ▼
Phase 2: CPU Switch (KVM -> O3CPU)
 ├── Dumps boot-phase stats (Dump #1)
 └── Replaces KVM core with detailed DerivO3CPU
 │
 ▼
Phase 3: Warm-up Phase
 ├── Simulates 1,000,000 instructions under O3CPU
 ├── Warms up branch predictors, pipeline, L1I, L1D, and L2 cache
 └── Resets simulation statistics
 │
 ▼
Phase 4: Measured ROI Execution
 ├── Simulates exactly 10,000,000 instructions
 ├── L2 BDI compressor dynamically performs co-allocation, expansions, and contractions
 ├── Cap reached (ExitEvent.MAX_INSTS)
 └── Dumps final benchmark ROI statistics (Dump #2) into stats.txt
```

---

## 3. Directory Layout

```text
experiments/
├── baseline_validation/      # 100k warmup + 1M ROI (No compression)
│   ├── gem5/stats.txt
│   └── run.log
├── baseline_10m/             # 1M warmup + 10M ROI (No compression)
│   ├── gem5/stats.txt
│   └── run.log
├── bdi_validation/           # 10k warmup + 10k ROI (BDI smoke test)
│   ├── gem5/stats.txt
│   └── run.log
├── bdi_1m/                   # 100k warmup + 1M ROI (BDI stress test)
│   ├── gem5/stats.txt
│   └── run.log
└── bdi_10m/                  # 1M warmup + 10M ROI (BDI full experiment)
    ├── gem5/stats.txt
    └── run.log
```

---

## 4. Key Metrics Extraction

To inspect measured ROI statistics (always read **Dump #2**, after the second `---------- Begin Simulation Statistics ----------` line):

* **CPI & IPC**: `board.processor.switch.core.cpi` / `board.processor.switch.core.ipc`
* **L1D Miss Rate**: `board.cache_hierarchy.l1d-cache-0.demandMissRate::total`
* **L1I Miss Rate**: `board.cache_hierarchy.l1i-cache-0.ReadReq.missRate::total`
* **L2 Miss Rate**: `board.cache_hierarchy.l2-cache-0.overallMissRate::total`
* **L2 Replacements**: `board.cache_hierarchy.l2-cache-0.replacements`
* **BDI Expansions / Contractions**: `board.cache_hierarchy.l2-cache-0.dataExpansions` / `board.cache_hierarchy.l2-cache-0.dataContractions`
* **DRAM Traffic**: `board.memory.mem_ctrl0.dram.dramBytesRead` / `dramBytesWritten`
