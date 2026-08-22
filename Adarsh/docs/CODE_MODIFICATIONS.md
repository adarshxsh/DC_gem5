# Summary of Code Modifications

This document records all modifications made to the gem5 codebase to support KVM nested virtualization fast-boot, O3CPU simulation switching, and fix the `CompressedTags`/`SectorSubBlk` co-allocation bug.

---

## 1. Simulation Configuration
### File: `configs/spec2017_compression_kvm.py`
* **KVM Nested Virtualization Compatibility**:
  * Added `proc.core.usePerf = False` to each core in `processor.start` when using `CPUTypes.KVM`. In nested GCP virtual machines, hardware performance counter attachment (`PerfKvmCounter::attach`) fails; disabling gem5's optional perf counters allows KVM fast boot without host modification.
* **KVM Permission Checking**:
  * Updated permission check to `os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK)` for robust multi-user/group access verification.

---

## 2. Tag Store & Compression Fix
### Problem Statement
When BDI compression relocated blocks during data expansions/contractions into co-allocated superblocks, gem5 panicked with:
```text
src/mem/cache/tags/sector_blk.cc:89: panic: Overwriting valid sector!
```
* **Root Cause**: `CacheBlk::operator=` previously called `insert({other.getTag(), other.isSecure()})`. Because `other.getTag()` was already the extracted tag, passing it to `insert()` caused `_sectorBlk->match()` to invoke `extractTag()` a second time, shifting the tag to 0 and failing the sector match comparison against the valid superblock tag.

### Modified Files:

#### A. `src/mem/cache/tags/tagged_entry.hh`
* Added public method `copyTagsFrom(const TaggedEntry &other)`:
  ```cpp
  virtual void
  copyTagsFrom(const TaggedEntry &other)
  {
      _tag = other.getTag();
      _secure = other.isSecure();
  }
  ```
  Transfers extracted tag and security state directly without re-applying the tag extractor function.

#### B. `src/mem/cache/cache_blk.hh`
* Updated `CacheBlk::operator=(CacheBlk&& other)`:
  Replaced erroneous `insert({other.getTag(), other.isSecure()})` call with:
  ```cpp
  copyTagsFrom(other);
  setValid();
  ```

#### C. `src/mem/cache/tags/sector_blk.hh` & `src/mem/cache/tags/sector_blk.cc`
* Implemented `SectorSubBlk::operator=(SectorSubBlk&& other)`:
  * Verifies sector consistency on co-allocation:
    ```cpp
    panic_if(_sectorBlk && _sectorBlk->isValid() &&
        ((_sectorBlk->getTag() != other.getTag()) ||
         (_sectorBlk->isSecure() != other.isSecure())),
        "Overwriting valid sector!");
    ```
  * Copies sector tag if destination sector was previously invalid (`_sectorBlk->copyTagsFrom(other)`).
  * Delegates to `CacheBlk::operator=(std::move(other))` to transfer block-level metadata and validate sub-block in destination sector.

#### D. `src/mem/cache/tags/super_blk.cc`
* Updated `CompressionBlk::operator=(CompressionBlk&& other)`:
  Delegates directly to `SectorSubBlk::operator=(std::move(other))` to ensure sector-level tags and sub-block counts are properly tracked during compressed block moves.

---

## 3. Repository Configuration
### File: `.gitignore`
* Added ignore patterns for heavy simulation artifacts, log files, and output directories:
  ```gitignore
  # Simulation results, experiment outputs and temporary logs
  experiments/
  m5out*/
  *.log
  *.pid.txt
  gem5_*_pid.txt
  help_out.txt
  ```
