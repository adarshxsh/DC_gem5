# Module 1: The 4 Fatal Flaws in Stock gem5 Cache Compression

Why did stock gem5 run *slower* with compression enabled than with compression completely disabled?

On paper, hardware cache compression sounds like free capacity. But in computer architecture, every optimization introduces microarchitectural trade-offs. In stock gem5, four fatal design flaws crippled the system on memory-intensive workloads like SPEC CPU2017 `505.mcf_r`.

---

## Flaw A: The "Collateral Eviction" Pathology

### The Mental Model: 4 Roommates in a Shared Apartment
In gem5's `CompressedTags`, physical cache lines are organized into **Superblocks**. Each superblock consists of 64 physical bytes (512 bits) of SRAM data storage, backed by multiple tag entries called **SectorSubBlks** (sub-blocks).

```
                     ┌────────────────────────────────────────────────────────┐
                     │            Physical Superblock (64 Bytes)              │
                     ├──────────────┬──────────────┬───────────┬──────────────┤
                     │  Sub-Block 0 │  Sub-Block 1 │Sub-Block 2│  Sub-Block 3 │
                     │   (16 B)     │    (16 B)    │  (16 B)   │    (16 B)    │
                     └──────────────┴──────────────┴───────────┴──────────────┘
```
Think of the 64-byte physical line as a 4-bedroom apartment shared by 4 roommates.

### The Pathology:
In a CPU, memory is dynamic. When a CPU executes a store instruction (write hit) to a cached block:
1. The line's content changes.
2. The compressor re-compresses the modified block.
3. The new compressed size might grow from 16 bytes to 20 bytes (**Data Expansion**). (relative to the previous compressed size)

Now, the 4 roommates exceed the physical space of the apartment (they need 68 bytes, but only 64 bytes exist).

#### What Stock gem5 Did:
Instead of kicking out just one roommate (the least recently used one) to make 16 bytes of room, stock gem5 had **no partial eviction mechanism**.
* It panicked or **evicted the entire 64-byte superblock**!
* In one fell swoop, it flushed all 4 valid, hot cache lines out to off-chip DRAM.

```
Write hit on Sub-Block 0 (expands from 16B to 20B)
             │
             ▼
Stock gem5: Cannot fit in superblock!
             │
             ▼
FLUSH ALL 4 ROOMMATES (Sub-Block 0, 1, 2, 3) TO DRAM!
             │
             ▼
Result: Massive spike in L2 misses, MSHR pipeline stalls, and memory traffic.
```

In `505.mcf_r`, this dynamic data expansion happened **over 111,000 times** during a single 200M instruction run. The constant thrashing wiped out thousands of hot cache lines, destroying the cache hit rate.

---

## Flaw B: The "Decompression Tax" (Phantom Latency)

When the CPU issues a load request (read demand) that hits in the L2 cache, the data must travel from the L2 data array back through the crossbar into the CPU execution pipeline.

* If the line is **compressed**, hardware logic must run the decompression algorithm (e.g., subtracting deltas from a base value). This takes **1 to 3 clock cycles**.
* But what if the line is **incompressible** (raw 64-byte data)?

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CPU Demand Read on L2 Hit                       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
                     Is the line compressed in SRAM?
                                    │
                  ┌─────────────────┴─────────────────┐
                  ▼                                   ▼
             YES (Compressed)                    NO (Raw Data)
                  │                                   │
                  ▼                                   ▼
         Real Hardware Decomp.               Stock gem5 Behavior:
         Takes 1–3 cycles                    STILL ADDS 1–3 CYCLES!
         (Necessary penalty)                 (Phantom latency tax!)
```

### The Incompressible Workload Disaster:
In `505.mcf_r`, **90.49% of all cache lines cannot be compressed** (they are random 64-bit memory addresses and pointers).
* Stock gem5 stored these lines as 512-bit raw blocks.
* Yet on **every single L2 hit**, stock gem5 unconditionally injected the full decompression latency cycle penalty into the critical read path!
* The CPU pipeline stalled for 1 to 3 cycles waiting for data that was **already uncompressed**.
* Over 200 million instructions, this phantom tax wasted **2,061,129 clock cycles**, causing the observed -0.66% IPC drop.

---

## Flaw C: Rigid Power-of-Two Quantization

Stock gem5 calculated compression efficiency using discrete integer powers of two:
$$\text{Compression Factor} \in \{1\times, 2\times, 4\times, 8\times\}$$

### The Internal Fragmentation Trap:
Consider a cache line containing linked-list pointer structures in `505.mcf_r` (`node_t` structs).
* When compressed with BDI, the 64-byte line shrinks to **36 bytes** (288 bits).
* The true compression ratio is:
  $$\frac{64\text{ bytes}}{36\text{ bytes}} = 1.78\times$$

```
   Raw Block: 64 Bytes (512 bits)
   ┌────────────────────────────────────────────────────────────────┐
   │                                                                │
   └────────────────────────────────────────────────────────────────┘
   Compressed Block: 36 Bytes (288 bits)
   ┌───────────────────────────────────┬────────────────────────────┐
   │         Compressed Data (36 B)    │       Unused (28 B)        │
   └───────────────────────────────────┴────────────────────────────┘
                                       ▲
                         Can we fit another 36B line here?
                         36B + 36B = 72B > 64B (No)
                         Can we fit a 20B line here?
                         36B + 20B = 56B <= 64B (YES!)
```

#### What Stock gem5 Did:
Stock gem5 asked: "Is $1.78\times \ge 2\times$?"
* Answer: **No**.
* Stock gem5 rounded the factor down to **$1\times$** (uncompressed)!
* It marked the entire 64-byte superblock as full, completely refusing to co-allocate any other compressed line alongside it.
* **28 bytes of expensive physical SRAM were left completely empty and wasted.**

This rigid quantization caused severe internal fragmentation, reducing the effective cache capacity of stock gem5 from a theoretical $2.0\times$ down to a meager $\sim 1.09\times - 1.20\times$.

---

## Flaw D: Prefetcher Pollution & Speculative Evictions

Modern high-performance CPUs use hardware prefetchers (like the Stride Prefetcher) to guess which memory addresses the CPU will need next.

* **Demand Accesses**: High-confidence loads from executing CPU instructions.
* **Prefetch Accesses**: Speculative guesses that may or may not ever be used.

### The Tragedy of the Commons in the Cache:
In stock gem5:
1. The cache replacement policy (e.g., LRU) treated speculative prefetch lines with the exact same priority as demand lines.
2. When a prefetch fill arrived at the L2 cache, gem5 would happily pick an existing superblock as a victim.
3. If that victim superblock held **3 or 4 dense, highly-compressed, frequently-used demand sub-blocks**, stock gem5 evicted all of them to make room for a single speculative prefetch line that had never been touched!

```
Speculative Prefetch Line Arrives
             │
             ▼
Stock Replacement Policy: "Find the oldest superblock..."
             │
             ▼
Picks Superblock containing 4 valid demand lines!
             │
             ▼
EVICTS 4 DEMAND LINES TO INSERT 1 SPECULATIVE PREFETCH LINE!
             │
             ▼
Net Result: Cache density plummeted, and the CPU stalled when it needed the evicted lines.
```

---

## Summary Matrix of the 4 Flaws

| Flaw | Root Cause | Impact on SPEC2017 `505.mcf_r` |
| :--- | :--- | :--- |
| **A: Collateral Eviction** | Expanding 1 sub-block evicted the whole 64B superblock | 111,926 thrashing events, massive eviction spikes |
| **B: Decompression Tax** | Unconditional 1–3 cycle latency added even to raw uncompressed data | 2,061,129 wasted cycles, -0.66% IPC degradation |
| **C: Rigid Quantization** | Factors snapped to powers of 2 ($1\times, 2\times, 4\times$), ignoring true byte sums | Severe internal fragmentation; 28B+ wasted per block |
| **D: Prefetch Pollution** | Speculative prefetches evicted dense multi-sub-block superblocks | Destruction of high-density cache lines |

In **Module 2**, we will see the exact architectural solutions we engineered to eliminate every single one of these flaws!
