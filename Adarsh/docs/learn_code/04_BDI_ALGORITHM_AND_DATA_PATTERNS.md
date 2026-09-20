# Module 4: Base-Delta-Immediate (BDI) Algorithm & Data Patterns

In this module, we will explore the algorithm at the heart of our experiment: **Base-Delta-Immediate (BDI)** cache compression, why it works on structured memory, and why workloads like SPEC CPU2017 `505.mcf_r` and `541.leela_r` behave so differently under hardware compression.

---

## 1. What is Base-Delta-Immediate (BDI) Compression?

Proposed by Gennady Pekhimenko et al. (PACT 2012), **Base-Delta-Immediate (BDI)** is a hardware-friendly, low-latency compression algorithm based on a key architectural observation:

> **The Low Dynamic Range Observation**:
> Most data elements stored within a single 64-byte cache line have small differences (deltas) relative to each other, even if their absolute numerical values are very large.

### Example: Pointers to the Same Memory Page
Consider a cache line holding eight 64-bit pointers allocated on the heap:

```text
Raw 64-bit Pointers (64 Bytes total):
Ptr 0: 0x00007FFF80001000
Ptr 1: 0x00007FFF80001048
Ptr 2: 0x00007FFF80001090
Ptr 3: 0x00007FFF800010D8
Ptr 4: 0x00007FFF80001120
Ptr 5: 0x00007FFF80001168
Ptr 6: 0x00007FFF800011B0
Ptr 7: 0x00007FFF800011F8
```

Notice that all 8 pointers share the exact same top 52 bits (`0x00007FFF80001...`). 
Instead of storing all 64 bytes:
1. **Base Value**: Store `Base 0 = 0x00007FFF80001000` (8 bytes).
2. **Deltas**: Store the difference between each pointer and `Base 0`:
   * $\Delta_0 = 0$
   * $\Delta_1 = +0x48$ (72)
   * $\Delta_2 = +0x90$ (144)
   * $\Delta_3 = +0xD8$ (216)...
3. Each delta fits comfortably inside an **8-bit or 16-bit integer**!

```text
Compressed Representation (Base64Delta8):
┌────────────────────────┬────────────────────────────────────────────┐
│   Base 0 (8 Bytes)     │  8 x 1-Byte Deltas [d0, d1, ..., d7] (8 B) │
└────────────────────────┴────────────────────────────────────────────┘
Total Size: 8 + 8 = 16 Bytes (instead of 64 Bytes!) -> 4:1 Compression Ratio!
```

---

## 2. The 8 Parallel Sub-Compressors in gem5's BDI

Hardware compression must be fast (1–2 cycles). It cannot afford complex iterative loops like gzip or zstd.

In gem5, BDI is implemented as a `MultiCompressor` running **8 parallel hardware encoders** simultaneously:

```
                            ┌───────────────────────────────┐
                            │    Raw 64-Byte Cache Line     │
                            └───────────────┬───────────────┘
                                            │
        ┌───────────────────┬───────────────┼───────────────┬───────────────────┐
        ▼                   ▼               ▼               ▼                   ▼
 ┌─────────────┐     ┌─────────────┐ ┌─────────────┐ ┌─────────────┐     ┌─────────────┐
 │    Zero     │     │  Repeated   │ │Base64Delta8 │ │Base64Delta16│ ... │Base32Delta8 │
 │ Compressor  │     │   Qwords    │ │ (8B Base,   │ │ (8B Base,   │     │ (4B Base,   │
 │   (0 bits)  │     │  (64 bits)  │ │  1B Delta)  │ │  2B Delta)  │     │  1B Delta)  │
 └──────┬──────┘     └──────┬──────┘ └──────┬──────┘ └──────┬──────┘     └──────┬──────┘
        │                   │               │               │                   │
        └───────────────────┴───────────────┼───────────────┴───────────────────┘
                                            ▼
                           Arbitration: Pick Smallest Size!
```

| Engine | Name | Base Size | Delta Size | Typical Target Data |
| :---: | :--- | :---: | :---: | :--- |
| **1** | `ZeroCompressor` | 0 B | 0 B | All-zero pages, uninitialized buffers |
| **2** | `RepeatedQwords` | 8 B | 0 B | Lines filled with repeated constants (e.g. `0xFF...`) |
| **3** | `Base64Delta8` | 8 B | 1 B | 64-bit heap pointers with close proximity |
| **4** | `Base64Delta16`| 8 B | 2 B | 64-bit pointers or large integer arrays |
| **5** | `Base64Delta32`| 8 B | 4 B | Wide-spread 64-bit addresses |
| **6** | `Base32Delta8` | 4 B | 1 B | 32-bit integers, loop indices, coordinates |
| **7** | `Base32Delta16`| 4 B | 2 B | 32-bit integer arrays with moderate dynamic range |
| **8** | `Base16Delta8` | 2 B | 1 B | 16-bit audio/sensor data or short integers |

Whichever engine achieves the smallest valid bit count wins and is stored in the cache line!

---

## 3. Workload Comparison: Why `505.mcf_r` vs `541.leela_r`?

Hardware cache compression does not behave uniformly across applications. Its effectiveness depends entirely on the **data structures and memory layouts** used by the program.

### Workload A: SPEC CPU2017 `505.mcf_r` (Vehicle Scheduling / Network Simplex)
* **What it does**: Solves massive single-depot vehicle scheduling problems using a primal network simplex algorithm.
* **Core Data Structure**: Large directed graphs with millions of `node_t` and `arc_t` structures connected via pointers.

```c
typedef struct node {
    cost_t          potential;
    int             orientation;
    struct node     *child;
    struct node     *sibling;
    struct node     *parent;
    struct arc      *pred;
    // ...
} node_t;
```

#### Why it is 90.5% Incompressible:
1. **Disparate Pointer Targets**: Pointers in `node_t` point to nodes allocated at completely different times and in completely different regions of memory. The delta between `child` and `parent` is often tens of gigabytes apart.
2. **Pseudo-Random Traversal**: The network simplex basis exchange frequently hops across unrelated graph nodes, creating high spatial entropy.
3. **Outcome**:
   * **90.49% of cache lines fail compression**.
   * Only 5.36% are zeros, and 4.15% compress at 2:1.
   * Average compression ratio: **1.09x**.

---

### Workload B: SPEC CPU2017 `541.leela_r` (Monte Carlo Tree Search / Deep Go Engine)
* **What it does**: Plays the game of Go using Monte Carlo Tree Search (MCTS) combined with deep convolutional neural network evaluations.
* **Core Data Structure**: High-density board representations, visited node visit counters, prior probabilities, and win-rate statistics.

```c
struct NodeData {
    float policy_prob;
    uint32_t visit_count;
    uint32_t black_wins;
    uint32_t white_wins;
};
```

#### Why it is Highly Compressible (38.5% Compressible):
1. **Repetitive Low-Order Integers**: Game tree search involves millions of small counters (0, 1, 2, 5 visits) and sparse game board states (empty grid intersections).
2. **Zero-Filled Buffers**: Neural network activation tensors often contain long runs of zeros due to ReLU activation functions.
3. **Outcome**:
   * **20.1% of lines compress down to 32 bits (16:1 ratio!)**.
   * **14.8% of lines compress at 2:1**.
   * Over **38.5% of all cache lines** expand effective cache capacity significantly!

---

## 4. The Core Architectural Takeaway

This contrast reveals the foundational law of cache compression:

$$\text{Net Speedup} = (\Delta \text{DRAM Misses Avoided} \times \text{DRAM Latency}) - (\text{L2 Hits} \times \text{Decompression Latency})$$

1. On **`541.leela_r`**, compressibility is high, avoiding millions of slow off-chip DRAM fetches (200 cycles each). The 1-cycle decompression delay is trivial compared to the massive miss reduction.
2. On **`505.mcf_r`**, compressibility is low (1.09x). If uncompressed lines are penalized with decompression latency, the small reduction in misses cannot compensate, and performance regresses.
3. **Our 0-Cycle Latency Fast-Path and EWMA Adaptive Bypass completely eliminate this dilemma**: they allow `505.mcf_r` to run at full speed without penalty, while still capturing capacity gains on compressible phases!

---

In **Module 5**, we will examine the full simulation pipeline, KVM fast boot, and how to read the resulting gem5 statistics.
