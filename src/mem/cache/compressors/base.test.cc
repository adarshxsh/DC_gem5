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

#include <climits>
#include <cstring>
#include <memory>
#include <vector>

#include "base/output.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "mem/cache/queue.hh"
#include "mem/cache/tags/super_blk.hh"
#include "mem/packet.hh"
#include "params/BaseCacheCompressor.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

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
    using Base::compress;

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
        decomp_lat = Cycles(2);
        auto data = std::make_unique<TestCompressorData>(forcedCompSizeBits);
        return data;
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

class TestBaseCompressor : public Zero
{
  public:
    using Base::compress;
    using Zero::decompress;

    TestBaseCompressor(const ZeroCompressorParams &p) : Zero(p) {}

    uint64_t getSampledUncompressedBits() const { return sampledUncompressedBits; }
    uint64_t getSampledCompressedBits() const { return sampledCompressedBits; }
    uint64_t getTotalCompressionRequests() const { return totalCompressionRequests; }
};

class BaseCompressorTest : public ::testing::Test
{
  protected:
    uint64_t zeroLine[8];
    uint64_t randomLine[8];

    void SetUp() override
    {
        std::memset(zeroLine, 0, sizeof(zeroLine));

        randomLine[0] = 0x1122334455667788ULL;
        randomLine[1] = 0x99AABBCCDDEEFF00ULL;
        randomLine[2] = 0x0123456789ABCDEFULL;
        randomLine[3] = 0xFEDCBA9876543210ULL;
        randomLine[4] = 0x1234567812345678ULL;
        randomLine[5] = 0x8765432187654321ULL;
        randomLine[6] = 0xA1B2C3D4E5F60718ULL;
        randomLine[7] = 0x9F8E7D6C5B4A3928ULL;
    }

    ZeroCompressorParams createParams(bool enableBypass, float threshold,
                                     unsigned sampling, unsigned decayShift)
    {
        ZeroCompressorParams p;
        p.name = "test_base_compressor";
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 64;
        p.size_threshold_percentage = 100;
        p.comp_chunks_per_cycle = 8;
        p.comp_extra_latency = Cycles(1);
        p.decomp_chunks_per_cycle = 8;
        p.decomp_extra_latency = Cycles(1);
        p.dictionary_size = 64;
        p.enable_adaptive_bypass = enableBypass;
        p.latency_breakeven_threshold = threshold;
        p.sampling_interval = sampling;
        p.decay_shift = decayShift;
        return p;
    }
};

/**
 * Test exponential decay scaling on sampled updates.
 */
TEST_F(BaseCompressorTest, ExponentialDecayCounters)
{
    auto params = createParams(true, 1.0f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Sample 1: Zero line -> uncompressed=512, compressed=0
    comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp.getSampledUncompressedBits(), 512ULL);
    EXPECT_EQ(comp.getSampledCompressedBits(), 0ULL);

    // Sample 2: Random line -> uncompressed=512, compressed=512
    comp.compress(randomLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp.getSampledUncompressedBits(), 992ULL);
    EXPECT_EQ(comp.getSampledCompressedBits(), 512ULL);
}

/**
 * Test rapid response to phase transition from compressible to uncompressible.
 */
TEST_F(BaseCompressorTest, AdaptToUncompressiblePhaseWithin20Samples)
{
    auto params = createParams(true, 1.35f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    for (int i = 0; i < 20; i++) {
        comp.compress(zeroLine, comp_lat, decomp_lat);
    }

    int samples_to_bypass = 0;
    bool bypassed = false;

    for (int i = 1; i <= 50; i++) {
        auto comp_data = comp.compress(randomLine, comp_lat, decomp_lat);
        if (comp_data->getSizeBits() == 512 && comp_lat == Cycles(0)) {
            bypassed = true;
            samples_to_bypass = i;
            break;
        }
    }

    EXPECT_TRUE(bypassed);
    EXPECT_LE(samples_to_bypass, 20);
}

/**
 * Test rapid response to phase transition from uncompressible to compressible.
 */
TEST_F(BaseCompressorTest, AdaptToCompressiblePhaseWithin20Samples)
{
    auto params = createParams(true, 1.35f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    for (int i = 0; i < 50; i++) {
        comp.compress(randomLine, comp_lat, decomp_lat);
    }

    int samples_to_reenable = 0;
    bool re_enabled = false;

    for (int i = 1; i <= 50; i++) {
        auto comp_data = comp.compress(zeroLine, comp_lat, decomp_lat);
        if (comp_data->getSizeBits() == 0 && comp_lat > Cycles(0)) {
            re_enabled = true;
            samples_to_reenable = i;
            break;
        }
    }

    EXPECT_TRUE(re_enabled);
    EXPECT_LE(samples_to_reenable, 20);
}

/**
 * Test that counter decay does NOT alter behavior when enableAdaptiveBypass is false.
 */
TEST_F(BaseCompressorTest, DisabledBypassNoDecay)
{
    auto params = createParams(false, 1.0f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    for (int i = 0; i < 5; i++) {
        comp.compress(randomLine, comp_lat, decomp_lat);
    }

    EXPECT_EQ(comp.getSampledUncompressedBits(), 2560ULL);
    EXPECT_EQ(comp.getSampledCompressedBits(), 2560ULL);
}

/**
 * Test numerical stability when counters decay near zero.
 */
TEST_F(BaseCompressorTest, NumericalStabilityNearZero)
{
    auto params = createParams(true, 1.0f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    for (int i = 0; i < 100; i++) {
        comp.compress(zeroLine, comp_lat, decomp_lat);
        EXPECT_GE(comp.getSampledUncompressedBits(), 512ULL);
    }

    for (int i = 0; i < 100; i++) {
        comp.compress(randomLine, comp_lat, decomp_lat);
        EXPECT_GE(comp.getSampledUncompressedBits(), 512ULL);
        EXPECT_GE(comp.getSampledCompressedBits(), 512ULL);
    }
}

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
