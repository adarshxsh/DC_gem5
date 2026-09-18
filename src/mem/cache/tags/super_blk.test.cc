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

TEST(SuperBlkTest, VariableSizedCoAllocation)
{
    SuperBlk super_blk;
    constexpr std::size_t blk_size = 64; // 64 bytes = 512 bits
    super_blk.setBlkSize(blk_size);

    CompressionBlk sub_blks[4];
    super_blk.blks.resize(4);
    for (int i = 0; i < 4; ++i) {
        super_blk.blks[i] = &sub_blks[i];
        sub_blks[i].setSectorBlock(&super_blk);
        sub_blks[i].setSectorOffset(i);
        sub_blks[i].registerTagExtractor([](Addr addr) { return addr; });
    }
    super_blk.registerTagExtractor([](Addr addr) { return addr; });

    // Initially 0 valid sub-blocks. Candidate of 256 bits should be accepted.
    EXPECT_TRUE(super_blk.canCoAllocate(256));
    // Candidate >= line size (512 bits) should be rejected as uncompressed.
    EXPECT_FALSE(super_blk.canCoAllocate(512));

    // Insert first block (256 bits)
    sub_blks[0].insert({0x1000, false});
    sub_blks[0].setSizeBits(256);

    // Remaining capacity: 256 bits. Candidate of 128 bits fits (256 + 128 <=
    // 512).
    EXPECT_TRUE(super_blk.canCoAllocate(128));
    // Candidate of 384 bits exceeds capacity (256 + 384 = 640 > 512).
    EXPECT_FALSE(super_blk.canCoAllocate(384));

    // Insert second block (128 bits)
    sub_blks[1].insert({0x1000, false});
    sub_blks[1].setSizeBits(128);

    // Remaining capacity: 128 bits. Candidate of 128 bits fits (384 + 128 <=
    // 512).
    EXPECT_TRUE(super_blk.canCoAllocate(128));

    // Insert third block (128 bits)
    sub_blks[2].insert({0x1000, false});
    sub_blks[2].setSizeBits(128);

    // Capacity is now 512/512 bits. Candidate of 64 bits exceeds capacity.
    EXPECT_FALSE(super_blk.canCoAllocate(64));
}
