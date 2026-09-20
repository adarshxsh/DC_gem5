# Module 3: Code Tour & The Crucial Bug Fixes

In this module, we will examine the actual C++ code modifications we made to the gem5 simulator. We will trace the exact microarchitectural panic that crashed stock gem5, dissect the underlying software bug bit-by-bit, and inspect the exact lines of code that resolved it.

---

## 1. The Panic: `panic: Overwriting valid sector!`

When we initially evaluated BDI compression on SPEC2017 `505.mcf_r`, gem5 crashed within seconds of starting the Region of Interest (ROI) with this fatal assertion:

```text
src/mem/cache/tags/sector_blk.cc:89: panic: Overwriting valid sector!
Memory Usage: 4884712 KBytes
Program aborted at tick 1450284729000
```

### What Triggered This Panic?
In a sector cache with compression (`CompressedTags`), each 64-byte physical sector (superblock) has a **sector tag**. All compressed sub-blocks co-allocated within that sector must share the **exact same sector tag** (because they are located at consecutive offsets within the same aligned 64-byte address range).

When a sub-block is modified by a store instruction, its size changes. If it can no longer fit in its current sector, gem5 moves the sub-block to another sector using the C++ move assignment operator:
```cpp
*dest_blk = std::move(*src_blk);
```

Before our fix, this move operation tripped an assertion:
```cpp
panic_if(_sectorBlk && _sectorBlk->isValid() &&
    !_sectorBlk->match(tag), "Overwriting valid sector!");
```
gem5 believed that the incoming sub-block had a completely different tag from the destination sector, even when they had the exact same memory address!

---

## 2. Root Cause: The "Double Tag Extraction" Bug

Let's trace how the tag extraction function works in gem5's memory indexing policy.

### How Address to Tag Mapping Works:
Suppose a memory address is `0x7FFF_8000_1040`.
* In a 64-byte cache line with 16-way associativity, the bottom 6 bits are the block offset, the next 8 bits are the cache set index, and the remaining upper bits are the **Tag**:

```
Address: 0x7FFF_8000_1040
┌──────────────────────────────┬──────────────────┬──────────────┐
│          Tag Bits            │     Set Bits     │ Offset Bits  │
│       [63 : 14]              │    [13 : 6]      │   [5 : 0]    │
└──────────────────────────────┴──────────────────┴──────────────┘
```

When a new block is inserted from memory, gem5 calls `extractTag(address)`:
```cpp
Addr extracted_tag = extractTag(0x7FFF_8000_1040); // e.g., returns 0x1FFF_E000
```
The variable `_tag` inside `TaggedEntry` now holds `0x1FFF_E000`.

### The Bug in Stock gem5's `CacheBlk::operator=`:
In stock gem5, the move assignment operator was written like this:

```cpp
// STOCK GEM5 CODE (BUGGY):
CacheBlk& CacheBlk::operator=(CacheBlk&& other)
{
    // ...
    // other.getTag() ALREADY returns 0x1FFF_E000 (already extracted!)
    insert({other.getTag(), other.isSecure()});
    // ...
}
```

Now trace what happened inside `insert()`:
```cpp
void TaggedEntry::insert(const KeyType &key)
{
    // key.address is 0x1FFF_E000 (the tag, NOT the original raw address!)
    setTag(extractTag(key.address)); 
}
```

Because `insert()` assumed `key.address` was a raw memory address, it called `extractTag()` a **second time** on an address that was already shifted:

$$\text{Tag}_{\text{first}} = \text{Addr} \gg 14$$
$$\text{Tag}_{\text{second}} = \text{Tag}_{\text{first}} \gg 14 = \text{Addr} \gg 28$$

1. The tag was bit-shifted to the right twice!
2. In many cases, this shifted the tag bits down to `0x0000` or random noise.
3. When the destination sector checked `_sectorBlk->match()`, it compared the valid sector tag (`0x1FFF_E000`) against the corrupted, double-shifted tag (`0x0000`).
4. **MISMATCH!** gem5 panicked: `Overwriting valid sector!`.

---

## 3. The 4-Part Surgical C++ Fix

To fix this once and for all without touching performance-critical fast paths, we introduced a clean, direct tag-copying interface across 4 files:

### Part 1: `src/mem/cache/tags/tagged_entry.hh`
We added `copyTagsFrom()`, which transfers the extracted tag directly without running the extractor function again:
```cpp
virtual void
copyTagsFrom(const TaggedEntry &other)
{
    _tag = other.getTag();
    _secure = other.isSecure();
}
```

### Part 2: `src/mem/cache/cache_blk.hh`
In `CacheBlk::operator=`, we replaced `insert()` with `copyTagsFrom()`:
```cpp
virtual CacheBlk&
operator=(CacheBlk&& other)
{
    assert(!isValid());
    assert(other.isValid());

    // Direct transfer - NO double extraction!
    copyTagsFrom(other);
    setValid();

    if (other.wasPrefetched()) {
        setPrefetched();
    }
    setCoherenceBits(other.coherence);
    setTaskId(other.getTaskId());
    setPartitionId(other.getPartitionId());
    setWhenReady(curTick());
    setRefCount(other.getRefCount());
    setSrcRequestorId(other.getSrcRequestorId());
    std::swap(lockList, other.lockList);

    other.invalidate();
    return *this;
}
```

### Part 3: `src/mem/cache/tags/sector_blk.cc`
We implemented `SectorSubBlk::operator=` to maintain sector-level tag consistency:
```cpp
SectorSubBlk&
SectorSubBlk::operator=(SectorSubBlk&& other)
{
    assert(!isValid());
    assert(other.isValid());

    // Verify consistency: destination sector must match source tag
    panic_if(_sectorBlk && _sectorBlk->isValid() &&
        ((_sectorBlk->getTag() != other.getTag()) ||
         (_sectorBlk->isSecure() != other.isSecure())),
        "Overwriting valid sector!");

    // If destination sector is currently empty, initialize its tag
    if (_sectorBlk && !_sectorBlk->isValid()) {
        _sectorBlk->copyTagsFrom(other);
    }

    // Delegate to base CacheBlk
    CacheBlk::operator=(std::move(other));
    return *this;
}
```

### Part 4: `src/mem/cache/tags/super_blk.cc`
We updated `CompressionBlk::operator=` to delegate to `SectorSubBlk::operator=` and trigger real-time capacity updates:
```cpp
CompressionBlk&
CompressionBlk::operator=(CompressionBlk&& other)
{
    _size = other._size;
    setDecompressionLatency(other.getDecompressionLatency());
    if (other.isCompressed()) {
        setCompressed();
    } else {
        setUncompressed();
    }

    SuperBlk *src_super = static_cast<SuperBlk *>(other.getSectorBlock());

    SectorSubBlk::operator=(std::move(other));

    // Dynamic Superblock Compaction (PR #3):
    SuperBlk *dest_super = static_cast<SuperBlk *>(getSectorBlock());
    if (src_super) {
        src_super->updateCompressionFactor();
    }
    if (dest_super && dest_super != src_super) {
        dest_super->updateCompressionFactor();
    }

    return *this;
}
```

### Result:
With this fix in place, gem5 successfully executed **over 111,926 dynamic expansions** without a single panic or memory corruption issue!

---

## 4. The Asymmetric Two's Complement Delta Bound Fix

In commit `0ee4b8e`, we fixed a subtle mathematical bug in `DictionaryCompressor` delta pattern matching.

### The Mathematics:
In digital binary logic, signed integers use **Two's Complement** representation. For an $N$-bit signed integer, the valid numerical range is asymmetric:
$$\text{Range} = \left[ -2^{N-1}, \; +2^{N-1} - 1 \right]$$

For an 8-bit signed delta ($N=8$):
* Minimum negative value: $-2^7 = \mathbf{-128}$
* Maximum positive value: $+2^7 - 1 = \mathbf{+127}$

Notice that there is **one more negative number than positive numbers**!

### The Bug in gem5 (`src/mem/cache/compressors/dictionary_compressor.hh`):
gem5 defined `limit = (1 << (num_bits - 1)) - 1` (which equals `127`).
Then it validated deltas using:
```cpp
// BUGGY CODE:
return (delta >= -limit) && (delta <= limit);
```
* With `limit = 127`, the check evaluated: `delta >= -127 && delta <= 127`.
* If a cache line had an offset of exactly **$-128$**, gem5 declared it **INVALID**!
* Perfectly compressible blocks were rejected and stored uncompressed.

### The Fix:
```cpp
// FIXED CODE:
const typename std::make_signed<T>::type delta = value - base;
return (delta >= -(limit + 1)) && (delta <= limit);
```
Now `-(limit + 1)` correctly evaluates to `-(127 + 1) = -128`. All valid 8-bit negative deltas are accepted!

---

In **Module 4**, we will study how Base-Delta-Immediate (BDI) compression compresses memory blocks using these exact deltas.
