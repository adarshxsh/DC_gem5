# Cache Compressor / gem5 Research Project Contract

## 1. Project Overview

This project investigates cache-line compression in gem5, with the primary goal of evaluating the effect of cache compression on a simulated x86 system.

The current work focuses on integrating and evaluating a compression technique (BDI / Base-Delta-Immediate related compression) in gem5's cache hierarchy and measuring its impact using a SPEC2017 workload.

The primary benchmark currently being used is:

    505.mcf_r

The main experiment compares:

    Baseline: compression disabled
    Experiment: compression enabled

The current target is to run approximately:

    10,000,000 instructions

for the ROI/benchmark execution.

---

# 2. Research Objective

The objective is NOT simply to make gem5 compile or make a compressed cache run.

The objective is to obtain a reproducible and technically meaningful comparison between:

    Uncompressed cache
              vs
    Compressed cache

while keeping all other experimental variables as constant as possible.

The results should allow us to reason about:

- cache capacity effects
- compression ratio
- cache behavior
- cache misses
- memory traffic
- performance
- storage efficiency
- any timing/latency implications introduced by compression

The experiment must therefore be reproducible.

---

# 3. Current Hardware Environment

The experiment is being moved from an Apple Silicon Mac to a Google Cloud x86 VM.

Current GCP project:

    cache-compressor

Current VM:

    gem5-research-node

Zone:

    asia-south1-a

Machine type:

    n2-standard-8

Resources:

    8 vCPU
    32 GB RAM

Architecture:

    x86_64

CPU:

    Intel Xeon CPU @ 2.80 GHz

OS:

    Ubuntu 24.04 LTS

The VM currently does NOT expose:

    /dev/kvm

Therefore KVM must NOT be assumed to be available.

Do not blindly change gem5 from AtomicSimpleCPU to KVM.

---

# 4. Important CPU / Simulation Background

The previous experiment used a boot CPU configuration based on:

    AtomicSimpleCPU

and then switched to:

    O3CPU

during the relevant simulation phase.

The original motivation for moving to GCP is to obtain a faster x86 environment than the Apple Silicon Mac and make repeated gem5 experiments easier.

However:

    More host CPU cores != native execution of gem5's simulated CPU.

gem5 is still a simulator.

KVM is only an option if the host exposes KVM and the gem5 configuration is explicitly designed to use it.

Current assumption:

    /dev/kvm does not exist
    therefore KVM is NOT currently available.

Do not change the CPU model solely to improve speed without first validating the consequences.

---

# 5. Main gem5 Configuration

The important custom configuration file is:

    configs/spec2017_compression_eval.py

This file is part of the user's experimental setup.

It must be preserved.

Do not replace it with an upstream gem5 configuration.

The primary benchmark is:

    505.mcf_r

The experiment previously used commands conceptually equivalent to:

    ./build/X86/gem5.opt \
        -d m5out_baseline_test \
        configs/spec2017_compression_eval.py \
        --benchmark 505.mcf_r \
        --size test \
        --max-insts 1000

and:

    ./build/X86/gem5.opt \
        -d m5out_bdi \
        configs/spec2017_compression_eval.py \
        --benchmark 505.mcf_r \
        --size test \
        --max-insts 10000000 \
        --compression

These exact commands/configurations should be treated as historical reference points.

Do not modify them without understanding why.

---

# 6. Cache Configuration

The project uses a compressed cache configuration.

The relevant cache hierarchy includes:

    L2 cache

The current L2 target is:

    256 KB

The project investigates cache-line compression.

Terminology:

    Block
        The uncompressed cache line.

    Chunk
        gem5's compressor-level unit used inside a cache block.

In the previous work, the typical cache block size was:

    64 bytes

The exact configured value must always be verified from the active gem5 configuration rather than assumed.

---

# 7. Compression Work

The compression implementation is related to:

    BDI
    Base-Delta-Immediate

Relevant gem5 areas include:

    src/mem/cache/compressors/

and:

    src/mem/cache/tags/

Important files previously inspected include:

    src/mem/cache/compressors/base_delta.hh
    src/mem/cache/compressors/base_delta_impl.hh
    src/mem/cache/compressors/dictionary_compressor_impl.hh
    src/mem/cache/tags/compressed_tags.cc
    src/mem/cache/tags/tagged_entry.hh
    src/mem/cache/tags/sector_tags.cc

Do not assume these files are identical to upstream gem5.

The local project contains modifications.

---

# 8. Important Previous Bug / Discovery

A previous BDI-enabled experiment crashed at simulation tick 0.

The failure was approximately:

    TaggedEntry::match()

with an assertion related to:

    extractTag

inside:

    tagged_entry.hh

The crash happened during:

    KernelWorkload::initState()

The crash was traced to the use of:

    CompressedTags

for the L2 cache.

The important discovery was that:

    CompressedTags::tagsInit()

did not perform the required tag extractor registration.

The corresponding tag initialization logic in:

    SectorTags::tagsInit()

was used as an important reference.

The suspected missing functionality involved:

    registerTagExtractor(...)

for the relevant block/superblock tag extraction.

This issue must NOT be silently removed or worked around.

If the current source still contains this modification, inspect it carefully before changing it.

---

# 9. Current Local Changes

The working project previously showed:

    modified:
        src/mem/cache/tags/compressed_tags.cc

and:

    untracked:
        configs/spec2017_compression_eval.py

There were also generated experiment directories:

    m5out_baseline/
    m5out_baseline_test/
    m5out_bdi/

These m5out directories are generated results and should not be treated as source code.

They can be regenerated.

Do not use old m5out data as if it were a new experimental result.

---

# 10. Resource Files

The full-system experiment requires external resource files.

The project has:

    resource/

with:

    resource/disk_image/spec2017.img
    resource/kernel/vmlinux-4.19.83

Current known sizes are approximately:

    spec2017.img      12 GB
    vmlinux-4.19.83  23 MB

The disk image contains the SPEC2017 guest environment.

These resources are required by the full-system simulation.

Do not delete, recreate, shrink, or modify the disk image unless explicitly instructed.

---

# 11. Current Directory Structure

The intended project structure is:

    ~/DC/

        gem5/
            src/
            configs/
            build/
            system/
            util/
            ...

        resource/
            disk_image/
                spec2017.img

            kernel/
                vmlinux-4.19.83

The gem5 source directory is:

    ~/DC/gem5

The resource directory is:

    ~/DC/resource

The configuration should reference the correct resource paths.

Always verify paths rather than assuming the current working directory.

---

# 12. Build Environment

The GCP VM currently has approximately:

    g++ 13.3.0
    Python 3.12.3
    SCons 4.5.2
    Git 2.43.0

Build target:

    build/X86/gem5.opt

The VM has 8 vCPUs.

When compiling, using:

    scons build/X86/gem5.opt -j8

is reasonable.

Do not use an existing binary copied from another machine as the authoritative executable.

Build gem5 locally on the GCP x86 VM.

---

# 13. Reproducibility Rules

The following variables should remain constant when comparing baseline and compression:

    benchmark
    input size
    instruction count
    CPU model
    cache hierarchy
    cache sizes
    memory configuration
    kernel
    disk image
    simulation configuration
    gem5 source
    compiler/build configuration

The primary variable should be:

    compression enabled/disabled

Do not change multiple major variables simultaneously.

For example, do NOT:

    change CPU model
    + change cache size
    + change gem5 source
    + change benchmark
    + enable compression

and then attribute the resulting difference to compression.

---

# 14. Baseline First

Before testing compression:

    build gem5
    run a small smoke test
    verify simulation completes
    run baseline
    verify statistics are produced

Only after baseline succeeds should compression be enabled.

The baseline is the control experiment.

If baseline fails, do not proceed to BDI.

---

# 15. Compression Experiment

After baseline is verified:

    enable compression
    run the same workload
    use the same instruction count
    collect statistics

Compare:

    baseline
    vs
    compression

Do not overwrite previous output directories.

Use separate output directories such as:

    m5out_baseline/
    m5out_bdi/

or better, uniquely named experiment directories.

Never mix statistics from different runs.

---

# 16. Checkpoint Strategy

Full-system Linux boot in gem5 is expensive.

Previous experience showed that the Linux boot can take substantially longer than the actual 10M-instruction benchmark execution.

Therefore checkpoints are strongly preferred for repeated experiments.

Conceptually:

    Boot Linux
        ↓
    Configure benchmark
        ↓
    Reach ROI
        ↓
    Save checkpoint
        ↓
    Restore checkpoint
        ↓
    Baseline
        ↓
    Restore checkpoint
        ↓
    BDI
        ↓
    Restore checkpoint
        ↓
    Other compression configuration

The goal is to pay the expensive boot/setup cost once.

Do not create checkpoints until the basic configuration is known to work.

---

# 17. KVM Policy

KVM is NOT currently available.

Known observation:

    /dev/kvm
    does not exist

Do not switch:

    AtomicSimpleCPU → KVM

without first verifying:

    /dev/kvm exists

and:

    gem5 KVM configuration is valid for this full-system setup.

Do not assume:

    "Virtualization type: full"

means:

    "KVM is available inside the VM."

If nested virtualization is investigated later, treat it as a separate infrastructure experiment.

---

# 18. Performance Philosophy

The objective is not:

    "make one run finish as fast as possible."

The objective is:

    "obtain trustworthy experimental results efficiently."

Therefore:

    correctness > speed

and:

    reproducibility > convenience

A fast but inconsistent experiment is not useful for research.

---

# 19. Antigravity Operating Rules

Before modifying anything:

    inspect first
    explain what you found
    identify the affected files
    explain why a modification is necessary

Do not automatically:

    rewrite gem5 code
    upgrade gem5
    change CPU models
    change cache sizes
    change benchmark
    change kernel
    replace the disk image
    delete experiment outputs
    install random dependencies
    switch to KVM
    refactor unrelated code

Do not make broad changes merely to make a build pass.

If a build or simulation fails:

    diagnose the root cause first.

Do not hide errors by:

    disabling assertions
    commenting out checks
    changing semantics
    suppressing warnings/errors

---

# 20. Antigravity Must Preserve Research State

Before making significant modifications, record:

    current git status
    relevant file contents/diffs
    current gem5 version
    build command
    simulation command
    configuration values

Generated outputs should be kept separate from source modifications.

Never overwrite the baseline result with a compression result.

---

# 21. First Task on This VM

Before running any benchmark, perform the following inspection:

1. Confirm:

       ~/DC/gem5

2. Confirm:

       ~/DC/resource/disk_image/spec2017.img

3. Confirm:

       ~/DC/resource/kernel/vmlinux-4.19.83

4. Inspect:

       configs/spec2017_compression_eval.py

5. Inspect:

       src/mem/cache/tags/compressed_tags.cc

6. Determine the exact CPU configuration.

7. Determine the exact L2 configuration.

8. Determine how the disk image and kernel paths are constructed.

9. Confirm the compression flag.

10. Confirm whether the current source compiles.

Do NOT run the 10M-instruction experiment yet.

Do NOT modify source code during this inspection.

Report the findings first.

---

# 22. Expected Experimental Workflow

The intended workflow is:

    Phase 1
    =======
    Verify environment

        ↓

    Phase 2
    =======
    Build gem5

        ↓

    Phase 3
    =======
    Small smoke test

        ↓

    Phase 4
    =======
    Baseline 505.mcf_r

        ↓

    Phase 5
    =======
    Verify baseline statistics

        ↓

    Phase 6
    =======
    Enable BDI/compression

        ↓

    Phase 7
    =======
    Run same 505.mcf_r workload

        ↓

    Phase 8
    =======
    Compare statistics

        ↓

    Phase 9
    =======
    Create checkpoint for repeated experiments 

        ↓

    Phase 10
    ========
    Run additional compression configurations

---

# 23. Definition of Success

The project is successful when:

1. The exact modified gem5 source builds on x86.
2. The SPEC2017 full-system environment boots.
3. 505.mcf_r executes successfully.
4. Baseline statistics are generated.
5. Compression-enabled execution succeeds.
6. Compression statistics are generated.
7. Baseline and compressed results can be compared.
8. The experiment can be reproduced from the documented source/configuration.
9. Repeated experiments can use checkpoints instead of repeatedly booting Linux.

A successful compilation alone is NOT considered project success.

---

# 24. Important Principle

Never optimize before establishing correctness.

The correct order is:

    Reproduce
        ↓
    Validate
        ↓
    Measure
        ↓
    Optimize
        ↓
    Automate

Not:

    Change everything
        ↓
    Run
        ↓
    Hope the results are meaningful

---

# 25. Current Research Question

The central research question is:

    How does cache-line compression affect the behavior and
    effective capacity/performance of the cache hierarchy when
    executing a SPEC2017 workload?

The immediate experiment is:

    505.mcf_r
    10M instructions
    256 KB L2
    compression OFF
    vs
    compression ON

All other relevant parameters should remain controlled.
