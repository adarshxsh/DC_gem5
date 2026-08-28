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

#include <climits>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "params/BaseCacheCompressor.hh"

namespace gem5
{

namespace compression
{

class DummyCompressor : public Base
{
  public:
    using Base::compress;

    DummyCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk>& chunks, Cycles& comp_lat,
             Cycles& decomp_lat) override
    {
        comp_lat = Cycles(1);
        decomp_lat = Cycles(2);
        auto data = std::make_unique<CompressionData>();
        data->setSizeBits(blkSize * CHAR_BIT);
        return data;
    }

    void
    decompress(const CompressionData* comp_data, uint64_t* data) override
    {
    }
};

class CompressorLatencyTest : public testing::Test
{
  protected:
    BaseCacheCompressorParams params;
    std::unique_ptr<DummyCompressor> compressor;

    CompressorLatencyTest()
    {
        params.block_size = 64;
        params.chunk_size_bits = 32;
        params.size_threshold_percentage = 100;
        params.comp_chunks_per_cycle = 1;
        params.comp_extra_latency = Cycles(0);
        params.decomp_chunks_per_cycle = 1;
        params.decomp_extra_latency = Cycles(0);
        params.name = "dummy_compressor";

        compressor = std::make_unique<DummyCompressor>(params);
    }
};

TEST_F(CompressorLatencyTest, NullBlockReturnsZeroLatency)
{
    EXPECT_EQ(compressor->getDecompressionLatency(nullptr), Cycles(0));
}

TEST_F(CompressorLatencyTest, UncompressedBlockReturnsZeroLatency)
{
    CompressionBlk blk;
    blk.setSizeBits(64 * CHAR_BIT);
    blk.setUncompressed();
    blk.setDecompressionLatency(Cycles(4));

    EXPECT_EQ(compressor->getDecompressionLatency(&blk), Cycles(0));
}

TEST_F(CompressorLatencyTest, BlockWithUncompressedSizeReturnsZeroLatency)
{
    CompressionBlk blk;
    blk.setSizeBits(64 * CHAR_BIT);
    blk.setCompressed();
    blk.setDecompressionLatency(Cycles(4));

    // Even if marked compressed, size equal to 64 bytes (512 bits) means uncompressed payload.
    EXPECT_EQ(compressor->getDecompressionLatency(&blk), Cycles(0));
}

TEST_F(CompressorLatencyTest, GenuinelyCompressedBlockReturnsConfiguredLatency)
{
    CompressionBlk blk;
    blk.setSizeBits(32 * CHAR_BIT);
    blk.setCompressed();
    blk.setDecompressionLatency(Cycles(4));

    EXPECT_EQ(compressor->getDecompressionLatency(&blk), Cycles(4));
}

TEST_F(CompressorLatencyTest, CompressUncompressedDataResetsDecompLatToZero)
{
    uint64_t data[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    Cycles comp_lat(0);
    Cycles decomp_lat(10);

    compressor->compress(data, comp_lat, decomp_lat);

    EXPECT_EQ(decomp_lat, Cycles(0));
}

} // namespace compression
} // namespace gem5
