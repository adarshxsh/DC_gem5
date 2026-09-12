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

#include <cstddef>
#include <cstdint>
#include <memory>
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
        for (const auto &blk : sb.blks) {
            if (blk->isValid()) {
                count_valid++;
                const CompressionBlk *cblk =
                    static_cast<const CompressionBlk *>(blk);
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
            ASSERT_LE(count_valid, sb.getCompressionFactor());
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
