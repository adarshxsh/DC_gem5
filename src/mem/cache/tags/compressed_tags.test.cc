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
    subBlks[2].insert({0x1000, false});
    subBlks[2].setSizeBits(64);

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
    subBlksB[1] = std::move(subBlks[1]);

    // Verify subBlksB[1] is valid and retained its 128-bit size
    ASSERT_TRUE(subBlksB[1].isValid());
    ASSERT_EQ(subBlksB[1].getSizeBits(), 128);
    ASSERT_EQ(superBlkB.getNumValid(), 1);
    ASSERT_EQ(superBlkB.getCompressionFactor(), 4);

    // Verify superBlk (A) lost subBlks[1], so its CF recovered to 8
    ASSERT_FALSE(subBlks[1].isValid());
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

        if (!cblks[sb_idx][sub_idx].isValid()) {
            Addr tag = tag_base + (sb_idx * 0x1000);
            if (!sblks[sb_idx].isValid() || sblks[sb_idx].getTag() == tag) {
                cblks[sb_idx][sub_idx].insert({tag, false});
                cblks[sb_idx][sub_idx].setSizeBits(sz);
            }
        } else if (iter % 3 == 0) {
            // Invalidate/evict
            cblks[sb_idx][sub_idx].invalidate();
        } else if (iter % 5 == 0) {
            // Migrate to next superblock if target sub-block is invalid
            // and destination superblock tag matches or is invalid
            int target_sb = (sb_idx + 1) % NumSuperBlks;
            int target_sub = sub_idx;
            if (!cblks[target_sb][target_sub].isValid() &&
                (!sblks[target_sb].isValid() ||
                 sblks[target_sb].getTag() ==
                     cblks[sb_idx][sub_idx].getTag())) {
                cblks[target_sb][target_sub] =
                    std::move(cblks[sb_idx][sub_idx]);
            }
        } else {
            // Update size (expansion / contraction)
            cblks[sb_idx][sub_idx].setSizeBits(sz);
        }

        for (int i = 0; i < NumSuperBlks; ++i) {
            verifyInvariants(sblks[i]);
        }
    }
}
