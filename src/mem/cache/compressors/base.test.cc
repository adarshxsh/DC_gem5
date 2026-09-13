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

#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/queue.hh"
#include "mem/packet.hh"
#include "params/BaseCacheCompressor.hh"

namespace gem5
{

namespace compression
{

class TestCompressorData : public Base::CompressionData
{
  public:
    TestCompressorData(std::size_t size_bits)
    {
        setSizeBits(size_bits);
    }
};

class DummyCompressor : public Base
{
  public:
    DummyCompressor(const BaseCacheCompressorParams &p)
        : Base(p), forcedCompSizeBits(512)
    {
    }

    std::size_t forcedCompSizeBits;

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk>& chunks, Cycles& comp_lat,
             Cycles& decomp_lat) override
    {
        comp_lat = Cycles(1);
        decomp_lat = Cycles(1);
        return std::make_unique<TestCompressorData>(forcedCompSizeBits);
    }

    void
    decompress(const CompressionData* comp_data, uint64_t* cache_line) override
    {
    }

    void setSampledBits(uint64_t uncomp, uint64_t comp)
    {
        sampledUncompressedBits = uncomp;
        sampledCompressedBits = comp;
    }
};

} // namespace compression
} // namespace gem5

using namespace gem5;
using namespace gem5::compression;

TEST(BaseCompressorTest, HysteresisBypassEvaluation)
{
    BaseCacheCompressorParams params;
    std::memset(&params, 0, sizeof(params));
    params.block_size = 64;
    params.chunk_size_bits = 64;
    params.size_threshold_percentage = 100;
    params.comp_chunks_per_cycle = 1;
    params.decomp_chunks_per_cycle = 1;
    params.enable_adaptive_bypass = true;
    params.latency_breakeven_threshold = 1.0f;
    params.hysteresis_margin = 0.05f;
    params.sampling_interval = 1;

    DummyCompressor comp(params);

    EXPECT_FALSE(comp.isBypassing());

    uint64_t line[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    Cycles c_lat(0), d_lat(0);

    // High compression ratio (64 bytes -> 16 bytes = 4.0x ratio > 1.05)
    comp.forcedCompSizeBits = 128;
    comp.compress(line, c_lat, d_lat);
    EXPECT_FALSE(comp.isBypassing());

    // Ratio drops below low threshold (64 bytes -> 70 bytes = 0.91x < 0.95)
    comp.setSampledBits(512, 560);
    comp.compress(line, c_lat, d_lat);
    EXPECT_TRUE(comp.isBypassing());

    // Ratio recovers slightly to breakeven (1.00x), but within hysteresis range [0.95, 1.05]
    // Must remain in bypassed state!
    comp.setSampledBits(512, 512);
    comp.compress(line, c_lat, d_lat);
    EXPECT_TRUE(comp.isBypassing());

    // Ratio recovers above high threshold (64 bytes -> 32 bytes = 2.0x > 1.05)
    comp.setSampledBits(512, 256);
    comp.compress(line, c_lat, d_lat);
    EXPECT_FALSE(comp.isBypassing());
}

TEST(PacketTest, CompressionBackpressureFlags)
{
    RequestPtr req = std::make_shared<Request>(
        0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressionBackpressure());

    pkt.setCompressionBackpressure();
    EXPECT_TRUE(pkt.isCompressionBackpressure());

    pkt.clearCompressionBackpressure();
    EXPECT_FALSE(pkt.isCompressionBackpressure());
}

class TestEntry : public QueueEntry
{
  public:
    TestEntry() : QueueEntry() {}
    bool matchBlockAddr(Addr b_addr, bool is_sec) const override { return false; }
    bool matchBlockAddr(const PacketPtr p) const override { return false; }
    bool trySatisfyFunctional(PacketPtr p) override { return false; }
    bool sendPacket(BaseCache& c) override { return true; }
};

TEST(QueueTest, OccupancyRatioAndCapacity)
{
    Queue<TestEntry> queue("test_queue", 10, 0, "test");

    EXPECT_EQ(queue.capacity(), 10);
    EXPECT_EQ(queue.numAllocated(), 0);
    EXPECT_DOUBLE_EQ(queue.getOccupancyRatio(), 0.0);
}
