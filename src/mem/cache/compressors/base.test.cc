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
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

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
    // Before addition:
    // sampledUncompressedBits decays by 512 >> 4 = 32 => 480
    // + 512 = 992
    // sampledCompressedBits decays 0 >> 4 = 0 => 0
    // + 512 = 512
    comp.compress(randomLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp.getSampledUncompressedBits(), 992ULL);
    EXPECT_EQ(comp.getSampledCompressedBits(), 512ULL);
}

/**
 * Test rapid response to phase transition from compressible to uncompressible.
 */
TEST_F(BaseCompressorTest, AdaptToUncompressiblePhaseWithin20Samples)
{
    // Threshold 1.35x (breakeven), decay_shift = 4 (half-life ~16 samples)
    auto params = createParams(true, 1.35f, 1, 4);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Phase 1: Compress 20 compressible zero blocks
    for (int i = 0; i < 20; i++) {
        comp.compress(zeroLine, comp_lat, decomp_lat);
    }

    // Phase 2: Enter uncompressible phase (random blocks)
    int samples_to_bypass = 0;
    bool bypassed = false;

    for (int i = 1; i <= 50; i++) {
        auto comp_data = comp.compress(randomLine, comp_lat, decomp_lat);
        // Bypassed compressions have size 512 bits and 0 compression latency
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

    // Phase 1: Establish uncompressible phase (50 random blocks)
    for (int i = 0; i < 50; i++) {
        comp.compress(randomLine, comp_lat, decomp_lat);
    }

    // Phase 2: Enter highly compressible phase (zero blocks)
    int samples_to_reenable = 0;
    bool re_enabled = false;

    for (int i = 1; i <= 50; i++) {
        auto comp_data = comp.compress(zeroLine, comp_lat, decomp_lat);
        // When compression is re-enabled, compressed size for zero block is 0 bits
        // and comp_lat > 0
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

    // Compress 5 random lines
    for (int i = 0; i < 5; i++) {
        comp.compress(randomLine, comp_lat, decomp_lat);
    }

    // Without decay, counters simply accumulate: 5 * 512 = 2560
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

    // 100 zero block samples followed by 100 random block samples
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
