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

TEST(SuperBlkTest, EagerCompactionAndOffsetRemapping)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    constexpr std::size_t BlkSize = 64; // 64 bytes = 512 bits
    constexpr unsigned NumSubBlks = 8;

    SuperBlk superBlk;
    superBlk.setBlkSize(BlkSize);
    std::vector<CompressionBlk> subBlks(NumSubBlks);
    superBlk.blks.resize(NumSubBlks);

    for (unsigned k = 0; k < NumSubBlks; ++k) {
        superBlk.blks[k] = &subBlks[k];
        subBlks[k].setSectorBlock(&superBlk);
        subBlks[k].setSectorOffset(k);
        subBlks[k].registerTagExtractor([](Addr addr) { return addr; });
    }
    superBlk.registerTagExtractor([](Addr addr) { return addr; });

    // Insert sub-blocks at sector offset 5, 2, 7
    subBlks[0].insert({0x1000, false});
    subBlks[0].setSectorOffset(5);
    subBlks[0].setSizeBits(64);

    subBlks[1].insert({0x1000, false});
    subBlks[1].setSectorOffset(2);
    subBlks[1].setSizeBits(64);

    subBlks[2].insert({0x1000, false});
    subBlks[2].setSectorOffset(7);
    subBlks[2].setSizeBits(64);

    EXPECT_EQ(superBlk.getNumValid(), 3);
    EXPECT_EQ(superBlk.getSlot(5), 0);
    EXPECT_EQ(superBlk.getSlot(2), 1);
    EXPECT_EQ(superBlk.getSlot(7), 2);
    EXPECT_EQ(superBlk.getBlkByOffset(5), &subBlks[0]);
    EXPECT_EQ(superBlk.getBlkByOffset(2), &subBlks[1]);
    EXPECT_EQ(superBlk.getBlkByOffset(7), &subBlks[2]);

    // Invalidate sub-block at slot 1 (sector offset 2)
    subBlks[1].invalidate();

    // Verify compaction: valid count is 2
    EXPECT_EQ(superBlk.getNumValid(), 2);
    // Offset 2 is unmapped
    EXPECT_EQ(superBlk.getSlot(2), -1);
    // Offset 5 stays at slot 0
    EXPECT_EQ(superBlk.getSlot(5), 0);
    // Offset 7 is compacted into slot 1
    EXPECT_EQ(superBlk.getSlot(7), 1);

    // Contiguous low-index slots 0 and 1 are valid
    EXPECT_TRUE(superBlk.blks[0]->isValid());
    EXPECT_TRUE(superBlk.blks[1]->isValid());
    EXPECT_FALSE(superBlk.blks[2]->isValid());
}
