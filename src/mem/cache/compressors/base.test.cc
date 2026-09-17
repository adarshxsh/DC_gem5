/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "base/output.hh"
#include "mem/cache/compressors/cpack.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "params/CPack.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class BaseCompressorTest : public ::testing::Test
{
  protected:
    uint64_t zeroLine[8];
    uint64_t randomLine[8];

    void
    SetUp() override
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

    CPackParams
    createCPackParams(bool adaptive, float decay, float threshold,
                      unsigned interval)
    {
        CPackParams p;
        p.name = "cpack_test";
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 32;
        p.size_threshold_percentage = 50;
        p.comp_chunks_per_cycle = 2;
        p.comp_extra_latency = Cycles(5);
        p.decomp_chunks_per_cycle = 2;
        p.decomp_extra_latency = Cycles(1);
        p.dictionary_size = 16;
        p.enable_adaptive_bypass = adaptive;
        p.decay_factor = decay;
        p.latency_breakeven_threshold = threshold;
        p.sampling_interval = interval;
        return p;
    }
};

class TestCPack : public CPack
{
  public:
    using Base::compress;
    using CPack::CPack;
};

/**
 * Test that EWMA decay allows rapid adaptation during phase transitions.
 */
TEST_F(BaseCompressorTest, EWMADecayPhaseTransition)
{
    auto p = createCPackParams(true, 0.90f, 1.5f, 10);
    TestCPack compressor(p);
    compressor.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Phase 1: Compress 100 compressible zero lines (10 sampling intervals)
    for (int i = 0; i < 100; i++) {
        compressor.compress(zeroLine, comp_lat, decomp_lat);
    }

    // Phase 2: Switch to uncompressible random lines.
    // With 0.90 decay factor and sampling_interval = 10, adaptive bypass
    // should activate within 30 sampling intervals (300 requests), rapidly
    // adapting to the uncompressible phase transition.
    int requests_until_bypass = 0;
    bool bypassed = false;

    for (int i = 0; i < 500; i++) {
        comp_lat = Cycles(0);
        decomp_lat = Cycles(0);
        auto data = compressor.compress(randomLine, comp_lat, decomp_lat);

        // When bypass is active for non-sampled requests, comp_lat and
        // decomp_lat are 0
        if (comp_lat == Cycles(0) && data->getSizeBits() == 512) {
            bypassed = true;
            requests_until_bypass = i + 1;
            break;
        }
    }

    // Verify adaptive bypass triggered rapidly (within < 100 sampling
    // intervals, here < 300 requests)
    EXPECT_TRUE(bypassed);
    EXPECT_LT(requests_until_bypass, 300);
}

/**
 * Test floor limit logic on decayed accumulators.
 */
TEST_F(BaseCompressorTest, AccumulatorFloorLimits)
{
    // High decay factor (e.g. 0.01) to force rapid decay to small floating
    // point numbers
    auto p = createCPackParams(true, 0.01f, 1.5f, 1);
    TestCPack compressor(p);
    compressor.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Compress a block to initialize accumulators
    compressor.compress(zeroLine, comp_lat, decomp_lat);

    // Repeated compressions with 0.01 decay should safely reach 0.0 without
    // subnormal values or crashes
    for (int i = 0; i < 20; i++) {
        compressor.compress(zeroLine, comp_lat, decomp_lat);
    }
}
