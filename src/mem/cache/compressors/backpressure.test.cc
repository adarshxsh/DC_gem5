/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "base/output.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/compressors/zero.hh"
#include "mem/packet.hh"
#include "params/ZeroCompressor.hh"
#include "sim/cur_tick.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class TestBackpressureCompressor : public Zero
{
  public:
    using Base::compress;
    using Zero::decompress;

    TestBackpressureCompressor(const ZeroCompressorParams &p) : Zero(p) {}
};

class BackpressureCompressorTest : public ::testing::Test
{
  protected:
    uint64_t zeroLine[8];

    inline static Tick myTick = 0;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &myTick;
        std::memset(zeroLine, 0, sizeof(zeroLine));
    }

    ZeroCompressorParams
    createParams()
    {
        ZeroCompressorParams p;
        p.name = "test_backpressure_compressor";
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 64;
        p.size_threshold_percentage = 100;
        p.comp_chunks_per_cycle = 8;
        p.comp_extra_latency = Cycles(1);
        p.decomp_chunks_per_cycle = 8;
        p.decomp_extra_latency = Cycles(1);
        p.dictionary_size = 64;
        p.enable_adaptive_bypass = false;
        p.latency_breakeven_threshold = 1.0;
        p.sampling_interval = 100;
        p.decay_shift = 4;
        return p;
    }
};

/**
 * Test Packet backpressure flag manipulation.
 */
TEST(PacketBackpressureTest, FlagSetAndClear)
{
    static Tick myTick = 0;
    Gem5Internal::_curTickPtr = &myTick;
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isBackpressured());

    pkt.setBackpressure();
    EXPECT_TRUE(pkt.isBackpressured());

    pkt.clearBackpressure();
    EXPECT_FALSE(pkt.isBackpressured());
}

/**
 * Test compressor bypass when memory backpressure is active.
 */
TEST_F(BackpressureCompressorTest, BypassCompressionOnBackpressure)
{
    auto params = createParams();
    TestBackpressureCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Without backpressure, zero line compresses to 0 bits with comp_lat > 0
    auto comp_data_normal = comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data_normal->getSizeBits(), 0ULL);
    EXPECT_GT(comp_lat, Cycles(0));

    // Activate backpressure
    comp.setBackpressure(true);
    EXPECT_TRUE(comp.isBackpressured());

    // Under backpressure, compression must be bypassed
    auto comp_data_bp = comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data_bp->getSizeBits(),
              512ULL); // Full uncompressed block size in bits
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));

    // Deactivate backpressure
    comp.setBackpressure(false);
    EXPECT_FALSE(comp.isBackpressured());

    // Normal compression resumes
    auto comp_data_resumed = comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data_resumed->getSizeBits(), 0ULL);
    EXPECT_GT(comp_lat, Cycles(0));
}
