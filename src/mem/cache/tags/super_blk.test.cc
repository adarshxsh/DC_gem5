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

TEST(SuperBlkTest, GetVictimsOnExpansionTargeted)
{
    Tick mockTick = 1000;
    Gem5Internal::_curTickPtr = &mockTick;

    SuperBlk super_blk;
    super_blk.setBlkSize(64);
    super_blk.registerTagExtractor([](Addr addr) { return addr; });

    constexpr int num_sub_blks = 4;
    CompressionBlk sub_blks[num_sub_blks];
    super_blk.blks.resize(num_sub_blks);

    for (int i = 0; i < num_sub_blks; ++i) {
        super_blk.blks[i] = &sub_blks[i];
        sub_blks[i].setSectorBlock(&super_blk);
        sub_blks[i].setSectorOffset(i);
        sub_blks[i].registerTagExtractor([](Addr addr) { return addr; });
    }

    // Insert 4 valid sub-blocks with different ages
    for (int i = 0; i < num_sub_blks; ++i) {
        sub_blks[i].insert({0x1000, false});
        sub_blks[i].setSizeBits(64); // CF = 8
    }

    // Currently num_valid = 4.
    // Case 1: Expanding sub_blks[3] to target_cf = 2.
    // num_valid (4) > target_cf (2), so num_to_evict = 2.
    std::vector<CacheBlk*> evict_blks;
    super_blk.getVictimsOnExpansion(&sub_blks[3], 2, evict_blks);

    EXPECT_EQ(evict_blks.size(), 2);
    // Ensure expanding block itself is not in evict_blks
    for (auto blk : evict_blks) {
        EXPECT_NE(blk, &sub_blks[3]);
    }
    // Oldest sub-blocks (inserted first: sub_blks[0] and sub_blks[1]) should be victims
    EXPECT_EQ(evict_blks[0], &sub_blks[0]);
    EXPECT_EQ(evict_blks[1], &sub_blks[1]);

    // Case 2: Expanding sub_blks[3] to target_cf = 4.
    // num_valid (4) <= target_cf (4), so 0 evictions.
    evict_blks.clear();
    super_blk.getVictimsOnExpansion(&sub_blks[3], 4, evict_blks);
    EXPECT_EQ(evict_blks.size(), 0);
}
