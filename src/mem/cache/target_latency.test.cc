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
#include <vector>

#include "base/gtest/cur_tick_fake.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
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
    using Base::toChunks;

    Cycles compLat{4};
    Cycles decompLat{8};

    MockCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<Base::CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        comp_lat = compLat;
        decomp_lat = decompLat;
        auto comp_data = std::make_unique<Base::CompressionData>();
        comp_data->setSizeBits(256);
        return comp_data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *data) override
    {}
};

static BaseCacheCompressorParams
createParams()
{
    BaseCacheCompressorParams p{};
    p.name = "mock_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.comp_chunks_per_cycle = 1;
    p.decomp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 1;
    return p;
}

TEST(TargetLatencyTest, TargetDecompressionLatencyReadHit)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(0);

    MockCompressor compressor(createParams());

    CompressionBlk blk;
    blk.setSizeBits(256);
    blk.setDecompressionLatency(Cycles(8));

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr read_pkt = Packet::createRead(req);

    // Read target on valid compressed block
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(8));

    // Calculate decompression latency for read request
    Cycles lat = compressor.getDecompressionLatency(&blk);
    EXPECT_EQ(lat, Cycles(8));

    delete read_pkt;
}

TEST(TargetLatencyTest, TargetDecompressionLatencyPartialWriteHit)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(0);

    MockCompressor compressor(createParams());

    CompressionBlk blk;
    blk.setSizeBits(256);
    blk.setDecompressionLatency(Cycles(8));

    RequestPtr partial_req = std::make_shared<Request>(0x1000, 8, 0, 0);
    PacketPtr partial_write_pkt = Packet::createWrite(partial_req);

    EXPECT_TRUE(blk.isCompressed());
    EXPECT_FALSE(partial_write_pkt->isWholeLineWrite(64));

    Cycles lat = compressor.getDecompressionLatency(&blk);
    EXPECT_EQ(lat, Cycles(8));

    delete partial_write_pkt;
}

TEST(TargetLatencyTest, TargetDecompressionLatencyFullWriteHit)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(0);

    MockCompressor compressor(createParams());

    CompressionBlk blk;
    blk.setSizeBits(256);
    blk.setDecompressionLatency(Cycles(8));

    RequestPtr full_req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr full_write_pkt = Packet::createWrite(full_req);

    EXPECT_TRUE(blk.isCompressed());
    EXPECT_TRUE(full_write_pkt->isWholeLineWrite(64));

    // Whole line write does not require decompression of old line
    bool needs_decompression =
        full_write_pkt->isRead() || !full_write_pkt->isWholeLineWrite(64);
    EXPECT_FALSE(needs_decompression);

    delete full_write_pkt;
}

TEST(TargetLatencyTest, TargetDecompressionLatencyUncompressed)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(0);

    MockCompressor compressor(createParams());

    CompressionBlk blk;
    blk.setSizeBits(512); // uncompressed
    blk.setDecompressionLatency(Cycles(0));

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr read_pkt = Packet::createRead(req);

    EXPECT_FALSE(blk.isCompressed());

    Cycles lat = compressor.getDecompressionLatency(&blk);
    EXPECT_EQ(lat, Cycles(0));

    delete read_pkt;
}

TEST(TargetLatencyTest, CompressorRecompressionLatency)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(0);

    MockCompressor compressor(createParams());

    Cycles comp_lat{0};
    Cycles decomp_lat{0};
    uint64_t data[8] = {0};

    compressor.compress(compressor.toChunks(data), comp_lat, decomp_lat);

    EXPECT_EQ(comp_lat, Cycles(4));
    EXPECT_EQ(decomp_lat, Cycles(8));
}
