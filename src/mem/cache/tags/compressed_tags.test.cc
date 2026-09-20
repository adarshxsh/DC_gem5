/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "mem/cache/tags/super_blk.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

class SuperBlkTestFixture : public ::testing::Test
{
  protected:
    static constexpr std::size_t BlkSize = 64; // 64 bytes = 512 bits
    static constexpr unsigned NumSubBlks = 8;

    Tick mockTick = 0;
    SuperBlk superBlk;
    std::unique_ptr<CompressionBlk[]> subBlks;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
        superBlk.setBlkSize(BlkSize);
        subBlks.reset(new CompressionBlk[NumSubBlks]);
        superBlk.blks.resize(NumSubBlks);

        for (unsigned k = 0; k < NumSubBlks; ++k) {
            superBlk.blks[k] = &subBlks[k];
            subBlks[k].setSectorBlock(&superBlk);
            subBlks[k].setSectorOffset(k);
            subBlks[k].registerTagExtractor([](Addr addr) { return addr; });
        }
        superBlk.registerTagExtractor([](Addr addr) { return addr; });
    }

    void
    verifyInvariants(const SuperBlk &sb)
    {
        uint8_t count_valid = 0;
        uint8_t min_cf = sb.blks.size();
        std::size_t total_bits = 0;
        for (const auto &blk : sb.blks) {
            if (blk->isValid()) {
                count_valid++;
                const CompressionBlk *cblk =
                    static_cast<const CompressionBlk *>(blk);
                total_bits += cblk->getSizeBits();
                uint8_t cf =
                    sb.calculateCompressionFactor(cblk->getSizeBits());
                if (cf < min_cf) {
                    min_cf = cf;
                }
                ASSERT_EQ(blk->getTag(), sb.getTag());
                ASSERT_EQ(blk->isSecure(), sb.isSecure());
            }
        }
        ASSERT_EQ(sb.getNumValid(), count_valid);
        ASSERT_EQ(sb.isValid(), (count_valid > 0));
        if (count_valid > 0) {
            ASSERT_EQ(sb.getCompressionFactor(), min_cf);
            ASSERT_LE(total_bits, BlkSize * CHAR_BIT);
        } else {
            ASSERT_EQ(sb.getCompressionFactor(), 1);
        }

        // Verify slot compaction invariant: active sub-blocks are contiguous
        // in slots 0..count_valid-1
        for (uint8_t i = 0; i < count_valid; ++i) {
            ASSERT_TRUE(sb.blks[i]->isValid());
        }
        for (uint8_t i = count_valid; i < sb.blks.size(); ++i) {
            ASSERT_FALSE(sb.blks[i]->isValid());
        }
        for (std::size_t o = 0; o < sb.blks.size(); ++o) {
            int slot = sb.getSlotForOffset(o);
            ASSERT_GE(slot, 0);
            ASSERT_LT(slot, static_cast<int>(sb.blks.size()));
            ASSERT_EQ(sb.blks[slot]->getSectorOffset(), o);
        }
    }
};

TEST_F(SuperBlkTestFixture, InitialState)
{
    ASSERT_FALSE(superBlk.isValid());
    ASSERT_EQ(superBlk.getNumValid(), 0);
    ASSERT_EQ(superBlk.getCompressionFactor(), 1);
    ASSERT_TRUE(superBlk.isCompressed());
    verifyInvariants(superBlk);
}

TEST_F(SuperBlkTestFixture, CalculateCompressionFactor)
{
    // 64 bytes = 512 bits
    ASSERT_EQ(superBlk.calculateCompressionFactor(0), 8);
    ASSERT_EQ(superBlk.calculateCompressionFactor(64), 8);
    ASSERT_EQ(superBlk.calculateCompressionFactor(128), 4);
    ASSERT_EQ(superBlk.calculateCompressionFactor(256), 2);
    ASSERT_EQ(superBlk.calculateCompressionFactor(512), 1);
    ASSERT_EQ(superBlk.calculateCompressionFactor(1024), 1);
}

TEST_F(SuperBlkTestFixture, CoAllocationAndCapacityReuse)
{
    // Insert block 0 at offset 0 (size 64 bits -> CF=8)
    subBlks[0].insert({0x1000, false});
    subBlks[0].setSizeBits(64);

    ASSERT_TRUE(superBlk.isValid());
    ASSERT_EQ(superBlk.getNumValid(), 1);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);
    verifyInvariants(superBlk);

    // Check co-allocation possibilities
    ASSERT_TRUE(superBlk.canCoAllocate(64));
    ASSERT_TRUE(superBlk.canCoAllocate(
        128)); // target_cf = min(8, 4) = 4, 1 < 4, 128 <= 128
    ASSERT_FALSE(superBlk.canCoAllocate(512)); // target_cf = 1 -> uncompressed

    // Co-allocate block 1 at offset 1 (size 128 bits -> CF=4)
    subBlks[1].insert({0x1000, false});
    subBlks[1].setSizeBits(128);

    ASSERT_EQ(superBlk.getNumValid(), 2);
    ASSERT_EQ(superBlk.getCompressionFactor(), 4);
    verifyInvariants(superBlk);

    // Invalidate block 1 (free sub-block capacity)
    subBlks[1].invalidate();

    ASSERT_EQ(superBlk.getNumValid(), 1);
    // Capacity freed: compression factor should recover to 8!
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);
    verifyInvariants(superBlk);

    // Freed capacity can now co-allocate another 64-bit block
    ASSERT_TRUE(superBlk.canCoAllocate(64));
    int slot2 = superBlk.getSlotForOffset(2);
    auto cblk2 = static_cast<CompressionBlk *>(superBlk.blks[slot2]);
    cblk2->insert({0x1000, false});
    cblk2->setSizeBits(64);

    ASSERT_EQ(superBlk.getNumValid(), 2);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);
    verifyInvariants(superBlk);
}

TEST_F(SuperBlkTestFixture, SubBlockMigration)
{
    // Setup second superblock
    SuperBlk superBlkB;
    superBlkB.setBlkSize(BlkSize);
    std::unique_ptr<CompressionBlk[]> subBlksB(new CompressionBlk[NumSubBlks]);
    superBlkB.blks.resize(NumSubBlks);
    for (unsigned k = 0; k < NumSubBlks; ++k) {
        superBlkB.blks[k] = &subBlksB[k];
        subBlksB[k].setSectorBlock(&superBlkB);
        subBlksB[k].setSectorOffset(k);
        subBlksB[k].registerTagExtractor([](Addr addr) { return addr; });
    }
    superBlkB.registerTagExtractor([](Addr addr) { return addr; });

    // Populate superBlk (A) with 2 blocks
    subBlks[0].insert({0x2000, false});
    subBlks[0].setSizeBits(64); // CF=8
    subBlks[1].insert({0x2000, false});
    subBlks[1].setSizeBits(128); // CF=4

    ASSERT_EQ(superBlk.getCompressionFactor(), 4);
    verifyInvariants(superBlk);

    // Move subBlks[1] (128 bits) to subBlksB[1] in superBlkB
    int slotA1 = superBlk.getSlotForOffset(1);
    int slotB1 = superBlkB.getSlotForOffset(1);
    auto cblkA1 = static_cast<CompressionBlk *>(superBlk.blks[slotA1]);
    auto cblkB1 = static_cast<CompressionBlk *>(superBlkB.blks[slotB1]);
    *cblkB1 = std::move(*cblkA1);

    // Verify subBlksB offset 1 is valid and retained its 128-bit size
    int newSlotB1 = superBlkB.getSlotForOffset(1);
    auto newBlkB1 = static_cast<CompressionBlk *>(superBlkB.blks[newSlotB1]);
    ASSERT_TRUE(newBlkB1->isValid());
    ASSERT_EQ(newBlkB1->getSizeBits(), 128);
    ASSERT_EQ(superBlkB.getNumValid(), 1);
    ASSERT_EQ(superBlkB.getCompressionFactor(), 4);

    // Verify superBlk (A) lost offset 1, so its CF recovered to 8
    int newSlotA1 = superBlk.getSlotForOffset(1);
    auto newBlkA1 = static_cast<CompressionBlk *>(superBlk.blks[newSlotA1]);
    ASSERT_FALSE(newBlkA1->isValid());
    ASSERT_EQ(superBlk.getNumValid(), 1);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);

    verifyInvariants(superBlk);
    verifyInvariants(superBlkB);
}

TEST_F(SuperBlkTestFixture, ExpansionContractionCheck)
{
    subBlks[0].insert({0x3000, false});
    subBlks[0].setSizeBits(64); // CF=8

    // Expansion when new size has worse CF (128 bits -> CF=4 < 8)
    ASSERT_EQ(subBlks[0].checkExpansionContraction(128),
              CompressionBlk::DATA_EXPANSION);

    // Unchanged when new size has same CF
    ASSERT_EQ(subBlks[0].checkExpansionContraction(32),
              CompressionBlk::UNCHANGED);

    // Modify size to 256 bits (CF=2)
    subBlks[0].setSizeBits(256);
    ASSERT_EQ(superBlk.getCompressionFactor(), 2);

    // Contraction when new size has better CF (64 bits -> CF=8 > 2)
    ASSERT_EQ(subBlks[0].checkExpansionContraction(64),
              CompressionBlk::DATA_CONTRACTION);
}

TEST_F(SuperBlkTestFixture, StressCoAllocationMigrationEviction)
{
    // Stress test: 500 iterations of random allocation, co-allocation,
    // size changes, migration, and eviction
    constexpr int NumSuperBlks = 4;
    SuperBlk sblks[NumSuperBlks];
    std::unique_ptr<CompressionBlk[]> cblks[NumSuperBlks];

    for (int i = 0; i < NumSuperBlks; ++i) {
        sblks[i].setBlkSize(BlkSize);
        cblks[i].reset(new CompressionBlk[NumSubBlks]);
        sblks[i].blks.resize(NumSubBlks);
        for (unsigned k = 0; k < NumSubBlks; ++k) {
            sblks[i].blks[k] = &cblks[i][k];
            cblks[i][k].setSectorBlock(&sblks[i]);
            cblks[i][k].setSectorOffset(k);
            cblks[i][k].registerTagExtractor([](Addr addr) { return addr; });
        }
        sblks[i].registerTagExtractor([](Addr addr) { return addr; });
    }

    const std::size_t sizes[] = {32, 64, 128, 256};
    uint64_t tag_base = 0x10000;

    for (int iter = 0; iter < 500; ++iter) {
        int sb_idx = iter % NumSuperBlks;
        int sub_idx = (iter * 3) % NumSubBlks;
        std::size_t sz = sizes[(iter * 7) % 4];

        int slot = sblks[sb_idx].getSlotForOffset(sub_idx);
        auto cblk = static_cast<CompressionBlk *>(sblks[sb_idx].blks[slot]);

        if (!cblk->isValid()) {
            Addr tag = tag_base + (sb_idx * 0x1000);
            if (!sblks[sb_idx].isValid() || sblks[sb_idx].getTag() == tag) {
                cblk->insert({tag, false});
                cblk->setSizeBits(sz);
            }
        } else if (iter % 3 == 0) {
            // Invalidate/evict
            cblk->invalidate();
        } else if (iter % 5 == 0) {
            // Migrate to next superblock if target sub-block is invalid
            // and destination superblock tag matches or is invalid
            int target_sb = (sb_idx + 1) % NumSuperBlks;
            int target_slot = sblks[target_sb].getSlotForOffset(sub_idx);
            auto target_cblk = static_cast<CompressionBlk *>(
                sblks[target_sb].blks[target_slot]);
            if (!target_cblk->isValid() &&
                (!sblks[target_sb].isValid() ||
                 sblks[target_sb].getTag() == cblk->getTag())) {
                *target_cblk = std::move(*cblk);
            }
        } else {
            // Update size (expansion / contraction)
            cblk->setSizeBits(sz);
        }

        for (int i = 0; i < NumSuperBlks; ++i) {
            verifyInvariants(sblks[i]);
        }
    }
}

TEST_F(SuperBlkTestFixture, SelectiveEvictionSufficientCapacity)
{
    // Co-allocate two 64-bit sub-blocks (CF=8)
    subBlks[0].insert({0x4000, false});
    subBlks[0].setSizeBits(64);
    subBlks[1].insert({0x4000, false});
    subBlks[1].setSizeBits(64);

    ASSERT_EQ(superBlk.getNumValid(), 2);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);

    // subBlks[0] expands to 128 bits (CF=4)
    std::size_t new_size = 128;
    uint8_t new_cf = superBlk.calculateCompressionFactor(new_size);
    ASSERT_EQ(new_cf, 4);

    // Evaluate post-expansion capacity with existing valid sub-block
    // subBlks[1]
    uint8_t target_cf = std::min(
        new_cf, superBlk.calculateCompressionFactor(subBlks[1].getSizeBits()));
    std::size_t total_bits = new_size + subBlks[1].getSizeBits();

    // Capacity check: 2 sub-blocks <= target_cf (4), total_bits 192 <= 512
    ASSERT_LE(2, target_cf);
    ASSERT_LE(total_bits, BlkSize * CHAR_BIT);

    // Update size without evicting subBlks[1]
    subBlks[0].setSizeBits(new_size);

    ASSERT_TRUE(subBlks[0].isValid());
    ASSERT_TRUE(subBlks[1].isValid());
    ASSERT_EQ(superBlk.getNumValid(), 2);
    ASSERT_EQ(superBlk.getCompressionFactor(), 4);
    verifyInvariants(superBlk);
}

TEST_F(SuperBlkTestFixture, SelectiveEvictionExceededCapacity)
{
    // Insert 4 sub-blocks with distinct insertion ticks
    mockTick = 10;
    subBlks[0].insert({0x5000, false});
    subBlks[0].setSizeBits(64);

    mockTick = 20;
    subBlks[1].insert({0x5000, false});
    subBlks[1].setSizeBits(64);

    mockTick = 30;
    subBlks[2].insert({0x5000, false});
    subBlks[2].setSizeBits(64);

    mockTick = 40;
    subBlks[3].insert({0x5000, false});
    subBlks[3].setSizeBits(64);

    ASSERT_EQ(superBlk.getNumValid(), 4);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);

    // subBlks[3] expands to 256 bits (CF=2)
    std::size_t expansion_size = 256;
    uint8_t expansion_cf = superBlk.calculateCompressionFactor(expansion_size);
    ASSERT_EQ(expansion_cf, 2);

    // Collect valid co-allocated sub-blocks (excluding subBlks[3])
    std::vector<CompressionBlk *> co_blks;
    for (auto &blk : superBlk.blks) {
        if (blk->isValid() && (blk != &subBlks[3])) {
            co_blks.push_back(static_cast<CompressionBlk *>(blk));
        }
    }
    ASSERT_EQ(co_blks.size(), 3);

    // Sort by age (oldest/LRU first)
    std::sort(co_blks.begin(), co_blks.end(),
              [](const CompressionBlk *a, const CompressionBlk *b) {
                  return a->getAge() > b->getAge();
              });

    // Oldest should be subBlks[0] (t=10), then subBlks[1] (t=20), then
    // subBlks[2] (t=30)
    ASSERT_EQ(co_blks[0], &subBlks[0]);
    ASSERT_EQ(co_blks[1], &subBlks[1]);
    ASSERT_EQ(co_blks[2], &subBlks[2]);

    auto fits_capacity = [&](const std::vector<CompressionBlk *> &sub_list) {
        uint8_t target_cf = expansion_cf;
        std::size_t total_bits = expansion_size;
        for (const auto *sblk : sub_list) {
            uint8_t scf =
                superBlk.calculateCompressionFactor(sblk->getSizeBits());
            target_cf = std::min(target_cf, scf);
            total_bits += sblk->getSizeBits();
        }
        std::size_t total_count = 1 + sub_list.size();
        return (target_cf > 1) && (total_count <= target_cf) &&
               (total_bits <= BlkSize * CHAR_BIT);
    };

    std::vector<CacheBlk *> evict_blks;
    while (!co_blks.empty() && !fits_capacity(co_blks)) {
        evict_blks.push_back(co_blks.front());
        co_blks.erase(co_blks.begin());
    }

    // Only the 2 oldest sub-blocks (subBlks[0] and subBlks[1]) should be
    // selected for eviction
    ASSERT_EQ(evict_blks.size(), 2);
    ASSERT_EQ(evict_blks[0], &subBlks[0]);
    ASSERT_EQ(evict_blks[1], &subBlks[1]);

    // Perform selective eviction by sector offset
    std::vector<int> evict_offsets;
    for (auto *evict_blk : evict_blks) {
        evict_offsets.push_back(
            static_cast<SectorSubBlk *>(evict_blk)->getSectorOffset());
    }
    for (int offset : evict_offsets) {
        int slot = superBlk.getSlotForOffset(offset);
        superBlk.blks[slot]->invalidate();
    }

    // Update expansion sub-block size for offset 3
    int slot3 = superBlk.getSlotForOffset(3);
    static_cast<CompressionBlk *>(superBlk.blks[slot3])
        ->setSizeBits(expansion_size);

    // Verify subBlks at offsets 2 and 3 are preserved, offsets 0 and 1 evicted
    int slot0 = superBlk.getSlotForOffset(0);
    int slot1 = superBlk.getSlotForOffset(1);
    int slot2 = superBlk.getSlotForOffset(2);

    ASSERT_FALSE(superBlk.blks[slot0]->isValid());
    ASSERT_FALSE(superBlk.blks[slot1]->isValid());
    ASSERT_TRUE(superBlk.blks[slot2]->isValid());
    ASSERT_TRUE(superBlk.blks[slot3]->isValid());
    ASSERT_EQ(superBlk.getNumValid(), 2);
    ASSERT_EQ(superBlk.getCompressionFactor(), 2);
    verifyInvariants(superBlk);
}

TEST_F(SuperBlkTestFixture, HasValidDemand)
{
    // Initial state: empty superblock has no valid demand
    ASSERT_FALSE(superBlk.hasValidDemand());

    // Insert a prefetched block
    subBlks[0].insert({0x6000, false});
    subBlks[0].setPrefetched();
    ASSERT_TRUE(subBlks[0].isValid());
    ASSERT_TRUE(subBlks[0].wasPrefetched());
    ASSERT_FALSE(superBlk.hasValidDemand());

    // Insert a demand block
    subBlks[1].insert({0x6000, false});
    ASSERT_TRUE(subBlks[1].isValid());
    ASSERT_FALSE(subBlks[1].wasPrefetched());
    ASSERT_TRUE(superBlk.hasValidDemand());

    // Invalidate demand block
    subBlks[1].invalidate();
    ASSERT_FALSE(superBlk.hasValidDemand());

    // Access prefetched block (clears prefetched status)
    subBlks[0].clearPrefetched();
    ASSERT_FALSE(subBlks[0].wasPrefetched());
    ASSERT_TRUE(superBlk.hasValidDemand());
}

TEST_F(SuperBlkTestFixture, PrefetchCoAllocationFactorGuard)
{
    // Insert a demand sub-block into superBlk with size 64 bits (high CF = 8)
    subBlks[0].insert({0x6000, false});
    subBlks[0].setSizeBits(64);
    ASSERT_TRUE(superBlk.hasValidDemand());
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);

    // Evaluate co-allocation of a block of size 256 bits (CF = 2)
    const std::size_t new_size = 256;
    ASSERT_TRUE(superBlk.canCoAllocate(new_size));

    const uint8_t new_blk_cf = superBlk.calculateCompressionFactor(new_size);
    const uint8_t current_cf = superBlk.getCompressionFactor();
    const uint8_t new_cf = (superBlk.getNumValid() == 0)
                               ? new_blk_cf
                               : std::min(current_cf, new_blk_cf);

    ASSERT_EQ(new_blk_cf, 2);
    ASSERT_EQ(new_cf, 2);
    ASSERT_LT(new_cf, current_cf);

    // Prefetch demand-protection guard logic verification:
    // If request is prefetch AND superblock has valid demand AND new_cf <
    // current_cf, co-allocation is disallowed.
    bool is_prefetch = true;
    bool co_alloc_allowed_for_prefetch =
        superBlk.canCoAllocate(new_size) &&
        !(is_prefetch && superBlk.hasValidDemand() && (new_cf < current_cf));

    ASSERT_FALSE(co_alloc_allowed_for_prefetch);

    // For demand requests (is_prefetch = false), co-allocation remains
    // allowed.
    is_prefetch = false;
    bool co_alloc_allowed_for_demand =
        superBlk.canCoAllocate(new_size) &&
        !(is_prefetch && superBlk.hasValidDemand() && (new_cf < current_cf));

    ASSERT_TRUE(co_alloc_allowed_for_demand);
}

TEST_F(SuperBlkTestFixture, PrefetchVictimCandidateFilter)
{
    // Create candidate superblocks and sub-blocks
    SuperBlk sb_demand0;
    SuperBlk sb_demand1;
    SuperBlk sb_prefetch;

    CompressionBlk blk_demand0;
    CompressionBlk blk_demand1;
    CompressionBlk blk_prefetch;

    auto dummyTagExtractor = [](Addr addr) { return addr; };
    blk_demand0.registerTagExtractor(dummyTagExtractor);
    blk_demand1.registerTagExtractor(dummyTagExtractor);
    blk_prefetch.registerTagExtractor(dummyTagExtractor);
    sb_demand0.registerTagExtractor(dummyTagExtractor);
    sb_demand1.registerTagExtractor(dummyTagExtractor);
    sb_prefetch.registerTagExtractor(dummyTagExtractor);

    blk_demand0.setSectorBlock(&sb_demand0);
    blk_demand1.setSectorBlock(&sb_demand1);
    blk_prefetch.setSectorBlock(&sb_prefetch);

    sb_demand0.blks = {&blk_demand0};
    sb_demand1.blks = {&blk_demand1};
    sb_prefetch.blks = {&blk_prefetch};

    blk_demand0.insert({0x1000, false}); // demand block
    blk_demand1.insert({0x2000, false}); // demand block
    blk_prefetch.insert({0x3000, false});
    blk_prefetch.setPrefetched(); // prefetched block

    ASSERT_TRUE(sb_demand0.hasValidDemand());
    ASSERT_TRUE(sb_demand1.hasValidDemand());
    ASSERT_FALSE(sb_prefetch.hasValidDemand());

    std::vector<SuperBlk *> superblock_entries = {&sb_demand0, &sb_demand1,
                                                  &sb_prefetch};

    // Filter candidate victim superblocks for a prefetch request
    std::vector<SuperBlk *> replacement_candidates;
    bool is_prefetch = true;

    if (is_prefetch) {
        for (auto *sb : superblock_entries) {
            if (!sb->hasValidDemand()) {
                replacement_candidates.push_back(sb);
            }
        }
    } else {
        replacement_candidates = superblock_entries;
    }

    // Verify only sb_prefetch is eligible for eviction during a prefetch
    // request
    ASSERT_EQ(replacement_candidates.size(), 1);
    ASSERT_EQ(replacement_candidates[0], &sb_prefetch);

    // If all candidates hold warm demand data:
    blk_prefetch.clearPrefetched(); // Now sb_prefetch holds demand data too
    ASSERT_TRUE(sb_prefetch.hasValidDemand());

    replacement_candidates.clear();
    if (is_prefetch) {
        for (auto *sb : superblock_entries) {
            if (!sb->hasValidDemand()) {
                replacement_candidates.push_back(sb);
            }
        }
    }

    // Verify no candidates remain, which causes findVictim to return nullptr
    // and drop prefetch
    ASSERT_TRUE(replacement_candidates.empty());
}

TEST_F(SuperBlkTestFixture, DynamicSlotCompactionAndOffsetRemapping)
{
    // Populate sub-blocks at sector offsets 0, 1, 2, 3 with CF=8 (64 bits
    // each)
    for (unsigned k = 0; k < 4; ++k) {
        subBlks[k].insert({0x5000, false});
        subBlks[k].setSizeBits(64);
    }

    ASSERT_EQ(superBlk.getNumValid(), 4);
    ASSERT_EQ(superBlk.getCompressionFactor(), 8);
    verifyInvariants(superBlk);

    // Invalidate sub-block at sector offset 1 (middle sub-block)
    subBlks[1].invalidate();

    // Verify compaction occurred: valid count is 3
    ASSERT_EQ(superBlk.getNumValid(), 3);
    // Active sub-blocks are packed contiguously into physical slots 0, 1, 2
    ASSERT_TRUE(superBlk.blks[0]->isValid());
    ASSERT_TRUE(superBlk.blks[1]->isValid());
    ASSERT_TRUE(superBlk.blks[2]->isValid());
    ASSERT_FALSE(superBlk.blks[3]->isValid());

    // Verify indirect offset mapping:
    // sector offset 0 -> slot 0
    // sector offset 2 -> slot 1
    // sector offset 3 -> slot 2
    // sector offset 1 -> slot 3 (invalid slot)
    ASSERT_EQ(superBlk.getSlotForOffset(0), 0);
    ASSERT_EQ(superBlk.getSlotForOffset(2), 1);
    ASSERT_EQ(superBlk.getSlotForOffset(3), 2);
    ASSERT_EQ(superBlk.getSlotForOffset(1), 3);

    ASSERT_EQ(superBlk.blks[0]->getSectorOffset(), 0);
    ASSERT_EQ(superBlk.blks[1]->getSectorOffset(), 2);
    ASSERT_EQ(superBlk.blks[2]->getSectorOffset(), 3);
    ASSERT_EQ(superBlk.blks[3]->getSectorOffset(), 1);

    verifyInvariants(superBlk);

    // Co-allocate a new block for sector offset 1 into available free slot
    // (slot 3)
    int slot1 = superBlk.getSlotForOffset(1);
    ASSERT_FALSE(superBlk.blks[slot1]->isValid());
    superBlk.blks[slot1]->insert({0x5000, false});
    static_cast<CompressionBlk *>(superBlk.blks[slot1])->setSizeBits(64);

    ASSERT_EQ(superBlk.getNumValid(), 4);
    verifyInvariants(superBlk);
}
