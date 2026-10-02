/*
 * Copyright (c) 2026 gem5
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

#include <cstdint>
#include <memory>

#include "mem/cache/base.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/compressed_tags.hh"
#include "params/BaseCache.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;

class MockCompressor : public compression::Base
{
  public:
    Cycles compLat;
    Cycles decompLat;

    MockCompressor(const BaseCacheCompressorParams &p, Cycles comp_lat,
                   Cycles decomp_lat)
        : compression::Base(p), compLat(comp_lat), decompLat(decomp_lat)
    {}

    std::unique_ptr<compression::Base::CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        comp_lat = compLat;
        decomp_lat = decompLat;
        auto comp_data =
            std::make_unique<compression::Base::CompressionData>();
        comp_data->setSizeBits(256);
        return comp_data;
    }

    void
    decompress(const compression::Base::CompressionData *comp_data,
               uint64_t *data) override
    {}
};

TEST(BaseCacheRecompressionTest, UpdateCompressionDataPropagatesLatency)
{
    BaseCacheCompressorParams comp_p{};
    comp_p.name = "mock_compressor";
    comp_p.block_size = 64;
    comp_p.chunk_size_bits = 32;
    comp_p.comp_chunks_per_cycle = 2;
    comp_p.decomp_chunks_per_cycle = 2;
    comp_p.size_threshold_percentage = 100;
    MockCompressor compressor(comp_p, Cycles(3), Cycles(2));

    CompressionBlk blk;
    blk.setSizeBits(512);

    std::vector<uint64_t> chunks(8, 0);
    PacketList writebacks;

    Cycles recomp_lat(0);
    // Directly test compressor re-compression latency calculation
    Cycles comp_lat(0), decomp_lat(0);
    compressor.compress(chunks, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(3));
    EXPECT_EQ(decomp_lat, Cycles(2));

    recomp_lat += comp_lat;
    EXPECT_EQ(recomp_lat, Cycles(3));
}

TEST(BaseCacheRecompressionTest, RecompressionLatencyAccumulation)
{
    Cycles recomp_lat(0);
    Cycles comp_lat(5);

    // Initial recompression latency is 0
    EXPECT_EQ(recomp_lat, Cycles(0));

    // After updating compression data, recompression latency is accumulated
    recomp_lat += comp_lat;
    EXPECT_EQ(recomp_lat, Cycles(5));

    // Access latency calculation for compressed write hits:
    // Partial write hit = AccessLatency + DecompressionLatency +
    // RecompressionLatency
    Cycles access_lat(1);
    Cycles decomp_lat(2);
    Cycles total_partial_write_lat = access_lat + decomp_lat + recomp_lat;
    EXPECT_EQ(total_partial_write_lat, Cycles(8));

    // Whole-line write hit = TagOnlyLatency + RecompressionLatency
    Cycles tag_lat(1);
    Cycles total_whole_line_lat = tag_lat + recomp_lat;
    EXPECT_EQ(total_whole_line_lat, Cycles(6));
}
