/*
 * Copyright (c) 2026
 * All rights reserved
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

#include "mem/cache/tags/super_blk.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

/**
 * Test that CompressionBlk correctly sets compression status for uncompressed
 * vs compressed sizes.
 */
TEST(SuperBlkTest, UncompressedSubBlockDetection)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(2));

    // Uncompressed line (512 bits for a 64-byte line)
    blk.setSizeBits(512);
    EXPECT_FALSE(blk.isCompressed());
    EXPECT_EQ(blk.getSizeBits(), 512);

    // Compressed line (256 bits)
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getSizeBits(), 256);
}

TEST(SuperBlkTest, SetUncompressedClearsCompressed)
{
    CompressionBlk blk;
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());

    blk.setUncompressed();
    EXPECT_FALSE(blk.isCompressed());
}

class CanCoAllocateTest : public ::testing::Test
{
  protected:
    static constexpr std::size_t BlkSize = 64;
    static constexpr unsigned NumSubBlks = 4;

    SuperBlk superBlk;
    std::unique_ptr<CompressionBlk[]> subBlks;

    void
    SetUp() override
    {
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
};

TEST_F(CanCoAllocateTest, CumulativeBitSumCheck)
{
    // Initially empty superblock
    EXPECT_TRUE(superBlk.canCoAllocate(128));
    EXPECT_FALSE(superBlk.canCoAllocate(512)); // uncompressed rejected

    // Add 1st sub-block (128 bits) -> sum = 128
    subBlks[0].insert({0x1000, false});
    subBlks[0].setSizeBits(128);

    // Add 2nd sub-block (128 bits) -> sum = 256
    subBlks[1].insert({0x1000, false});
    subBlks[1].setSizeBits(128);

    // 128 + 128 + 128 = 384 <= 512 bits -> should allow co-allocation
    EXPECT_TRUE(superBlk.canCoAllocate(128));

    // Add 3rd sub-block (128 bits) -> sum = 384
    subBlks[2].insert({0x1000, false});
    subBlks[2].setSizeBits(128);

    // 384 + 128 = 512 <= 512 bits -> should allow 4th 128-bit sub-block
    EXPECT_TRUE(superBlk.canCoAllocate(128));

    // 384 + 256 = 640 > 512 bits -> should reject 256-bit sub-block
    EXPECT_FALSE(superBlk.canCoAllocate(256));
}

TEST_F(CanCoAllocateTest, HeterogeneousSubBlockCoAllocation)
{
    // Add 1st sub-block (256 bits, CF=2)
    subBlks[0].insert({0x2000, false});
    subBlks[0].setSizeBits(256);

    // Add 2nd sub-block (128 bits, CF=4) -> sum = 384 bits, min_cf = 2
    subBlks[1].insert({0x2000, false});
    subBlks[1].setSizeBits(128);

    // Under discrete count cap (numValid < target_cf), numValid = 2 < 2 would
    // be false. Under cumulative bit sum check (384 + 128 = 512 <= 512), this
    // should succeed!
    EXPECT_TRUE(superBlk.canCoAllocate(128));

    // Exceeding 512 bits (384 + 256 = 640 > 512) must be rejected
    EXPECT_FALSE(superBlk.canCoAllocate(256));
}
TEST(SuperBlkTest, InPlaceCompactionOnInvalidation)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    SuperBlk sb;
    sb.setBlkSize(64);
    std::vector<CompressionBlk> sub_blks(4);
    for (int k = 0; k < 4; ++k) {
        sub_blks[k].setSectorBlock(&sb);
        sub_blks[k].setSectorOffset(k);
        sub_blks[k].registerTagExtractor(
            [](Addr addr) { return addr & ~0xFF; });
        sb.blks.push_back(&sub_blks[k]);
    }
    sb.registerTagExtractor([](Addr addr) { return addr & ~0xFF; });

    // Insert 4 sub-blocks of size 128 bits (CF=4)
    Addr base_addr = 0x1000;
    for (int k = 0; k < 4; ++k) {
        sub_blks[k].insert({base_addr + k * 64, false});
        sub_blks[k].setSizeBits(128);
    }

    EXPECT_EQ(sb.getNumValid(), 4);
    EXPECT_EQ(sb.getCompressionFactor(), 4);
    EXPECT_EQ(sb.blks[0]->getSectorOffset(), 0);
    EXPECT_EQ(sb.blks[1]->getSectorOffset(), 1);
    EXPECT_EQ(sb.blks[2]->getSectorOffset(), 2);
    EXPECT_EQ(sb.blks[3]->getSectorOffset(), 3);

    // Invalidate sub-block at physical slot 1 (sector offset 1)
    sb.blks[1]->invalidate();

    // Compaction should shift remaining valid blocks down into contiguous
    // slots [0..2]
    EXPECT_EQ(sb.getNumValid(), 3);
    EXPECT_TRUE(sb.blks[0]->isValid());
    EXPECT_TRUE(sb.blks[1]->isValid());
    EXPECT_TRUE(sb.blks[2]->isValid());
    EXPECT_FALSE(sb.blks[3]->isValid());

    // Check that sector offsets moved properly
    EXPECT_EQ(sb.blks[0]->getSectorOffset(), 0);
    EXPECT_EQ(sb.blks[1]->getSectorOffset(), 2);
    EXPECT_EQ(sb.blks[2]->getSectorOffset(), 3);
}

TEST(SuperBlkTest, InPlaceCompactionOnContractionAndCoAllocation)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    SuperBlk sb;
    sb.setBlkSize(64);
    std::vector<CompressionBlk> sub_blks(4);
    for (int k = 0; k < 4; ++k) {
        sub_blks[k].setSectorBlock(&sb);
        sub_blks[k].setSectorOffset(k);
        sub_blks[k].registerTagExtractor(
            [](Addr addr) { return addr & ~0xFF; });
        sb.blks.push_back(&sub_blks[k]);
    }
    sb.registerTagExtractor([](Addr addr) { return addr & ~0xFF; });

    // Insert 2 sub-blocks of size 256 bits (CF=2)
    Addr base_addr = 0x2000;
    sub_blks[0].insert({base_addr, false});
    sub_blks[0].setSizeBits(256);
    sub_blks[1].insert({base_addr + 64, false});
    sub_blks[1].setSizeBits(256);

    EXPECT_EQ(sb.getNumValid(), 2);
    EXPECT_EQ(sb.getCompressionFactor(), 2);
    EXPECT_FALSE(sb.canCoAllocate(128)); // Full at CF=2

    // Contract both blocks to 128 bits (CF=4)
    sub_blks[0].setSizeBits(128);
    sub_blks[1].setSizeBits(128);

    EXPECT_EQ(sb.getCompressionFactor(), 4);
    EXPECT_TRUE(sb.canCoAllocate(128)); // Now has room for 2 more blocks!

    // Co-allocate 3rd sub-block at first available slot (blks[2])
    unsigned free_slot = sb.getNumValid();
    EXPECT_EQ(free_slot, 2);
    sub_blks[free_slot].insert({base_addr + 128, false});
    sub_blks[free_slot].setSizeBits(128);

    EXPECT_EQ(sb.getNumValid(), 3);
    EXPECT_TRUE(sb.blks[0]->isValid());
    EXPECT_TRUE(sb.blks[1]->isValid());
    EXPECT_TRUE(sb.blks[2]->isValid());
    EXPECT_FALSE(sb.blks[3]->isValid());
}
