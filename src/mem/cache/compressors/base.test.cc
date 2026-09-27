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

#include <cstring>
#include <memory>
#include <vector>

#include "mem/cache/base.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/compressed_tags.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/cur_tick.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;

namespace compression
{

class DummyCompressor : public Base
{
  public:
    using Base::compress;

    DummyCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        comp_lat = Cycles(8);
        decomp_lat = Cycles(3);
        auto comp_data = std::make_unique<CompressionData>();
        comp_data->setSizeBits(256);
        return comp_data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {
        std::memset(cache_line, 0, 64);
    }
};

} // namespace compression
} // namespace gem5

using namespace gem5;
using namespace gem5::compression;

TEST(BaseCompressorTest, RecompressionAndDecompressionLatency)
{
    BaseCacheCompressorParams params{};
    params.name = "dummy_compressor";
    params.eventq_index = 0;
    params.block_size = 64;
    params.chunk_size_bits = 64;
    params.size_threshold_percentage = 100;
    params.comp_chunks_per_cycle = 8;
    params.comp_extra_latency = Cycles(1);
    params.decomp_chunks_per_cycle = 8;
    params.decomp_extra_latency = Cycles(1);

    DummyCompressor compressor(params);
    compressor.regStats();

    uint64_t data[8] = {0};
    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    auto comp_data = compressor.compress(static_cast<const uint64_t *>(data),
                                         comp_lat, decomp_lat);

    ASSERT_NE(comp_data, nullptr);
    ASSERT_EQ(comp_lat, Cycles(8));
    ASSERT_EQ(decomp_lat, Cycles(3));
}

TEST(BaseCompressorTest, CompressionBlkLatencyAndReadyTimestamp)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(4));
    ASSERT_EQ(blk.getDecompressionLatency(), Cycles(4));

    Tick mock_tick = 1000;
    Gem5Internal::_curTickPtr = &mock_tick;

    blk.setWhenReady(mock_tick + 500);
    ASSERT_EQ(blk.getWhenReady(), 1500);
}
