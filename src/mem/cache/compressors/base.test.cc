/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

namespace gem5
{
namespace compression
{

TEST(BaseCompressorTest, DecompressionLatencyQuery)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(4));

    // Uncompressed block (64 bytes = 512 bits)
    blk.setSizeBits(512);
    EXPECT_FALSE(blk.isCompressed());

    // Compressed block (256 bits)
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(4));
}

TEST(BaseCompressorTest, TargetCompressionLatencyRead)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256); // Compressed block

    // Read target on compressed block should return decompression latency
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}

TEST(BaseCompressorTest, TargetCompressionLatencyWholeLineWrite)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256);

    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}

TEST(BaseCompressorTest, TargetCompressionLatencyPartialWrite)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256);

    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}

} // namespace compression
} // namespace gem5
