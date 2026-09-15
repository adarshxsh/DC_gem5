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

TEST(SuperBlkTest, AccumulativeBitCoAllocation)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    SuperBlk superBlk;
    superBlk.setBlkSize(64); // 64 bytes = 512 bits
    constexpr unsigned numSubBlks = 8;
    CompressionBlk subBlks[numSubBlks];
    superBlk.blks.resize(numSubBlks);
    for (unsigned k = 0; k < numSubBlks; ++k) {
        superBlk.blks[k] = &subBlks[k];
        subBlks[k].setSectorBlock(&superBlk);
        subBlks[k].setSectorOffset(k);
        subBlks[k].registerTagExtractor([](Addr addr) { return addr; });
    }
    superBlk.registerTagExtractor([](Addr addr) { return addr; });

    // Empty superblock can co-allocate any compressed size < 512 bits
    EXPECT_TRUE(superBlk.canCoAllocate(256));
    EXPECT_TRUE(superBlk.canCoAllocate(64));
    EXPECT_FALSE(superBlk.canCoAllocate(512)); // uncompressed size

    // Insert 256-bit sub-block
    subBlks[0].insert({0x1000, false});
    subBlks[0].setSizeBits(256);

    // Accumulated bits = 256.
    // 256 + 256 = 512 bits <= 512 bits -> true
    EXPECT_TRUE(superBlk.canCoAllocate(256));
    // 256 + 257 = 513 bits > 512 bits -> false
    EXPECT_FALSE(superBlk.canCoAllocate(257));

    // Fill remaining 7 sub-block slots with 16-bit sub-blocks
    for (unsigned k = 1; k < numSubBlks; ++k) {
        subBlks[k].insert({0x1000, false});
        subBlks[k].setSizeBits(16);
    }
    // All 8 slots occupied -> getNumValid() == blks.size()
    EXPECT_FALSE(superBlk.canCoAllocate(16));
}
