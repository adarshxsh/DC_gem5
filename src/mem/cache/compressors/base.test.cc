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

#include <cmath>
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

    double
    getDecayedUncompressedBits() const
    {
        return decayedUncompressedBits;
    }
    double
    getDecayedCompressedBits() const
    {
        return decayedCompressedBits;
    }
    uint64_t
    getTotalCompressionRequests() const
    {
        return totalCompressionRequests;
    }
    bool
    getBypassActive() const
    {
        return bypassActive;
    }
};

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

    ZeroCompressorParams
    createParams(bool enableBypass, float threshold, unsigned sampling,
                 unsigned evalInterval = 1000, float decayFactor = 0.8f,
                 float sharpDelta = 0.5f)
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
        p.evaluation_interval = evalInterval;
        p.decay_factor = decayFactor;
        p.sharp_delta_threshold = sharpDelta;
        return p;
    }
};

/**
 * Test exponential decay scaling on sampled updates.
 */
TEST_F(BaseCompressorTest, ExponentialDecayCounters)
{
    // High sharp_delta_threshold (1000.0) so sharp delta reset won't interfere
    auto params = createParams(true, 1.0f, 1, 1000, 0.8f, 1000.0f);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Sample 1: Zero line -> uncompressed=512, compressed=0
    comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_DOUBLE_EQ(comp.getDecayedUncompressedBits(), 512.0);
    EXPECT_DOUBLE_EQ(comp.getDecayedCompressedBits(), 0.0);

    // Sample 2: Random line -> uncompressed=512, compressed=512
    // nextDecayedUncompressed = 512.0 * 0.8 + 512.0 = 921.6
    // nextDecayedCompressed = 0.0 * 0.8 + 512.0 = 512.0
    comp.compress(randomLine, comp_lat, decomp_lat);
    EXPECT_NEAR(comp.getDecayedUncompressedBits(), 921.6, 1e-4);
    EXPECT_NEAR(comp.getDecayedCompressedBits(), 512.0, 1e-4);

    // Sample 3: Random line -> uncompressed=512, compressed=512
    // nextDecayedUncompressed = 921.6 * 0.8 + 512.0 = 1249.28
    // nextDecayedCompressed = 512.0 * 0.8 + 512.0 = 921.6
    comp.compress(randomLine, comp_lat, decomp_lat);
    EXPECT_NEAR(comp.getDecayedUncompressedBits(), 1249.28, 1e-4);
    EXPECT_NEAR(comp.getDecayedCompressedBits(), 921.6, 1e-4);
}

/**
 * Test sharp delta threshold reset.
 */
TEST_F(BaseCompressorTest, SharpDeltaReset)
{
    auto params_sensitive = createParams(true, 1.0f, 1, 1000, 0.8f, 0.1f);
    TestBaseCompressor comp_sens(params_sensitive);
    comp_sens.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    for (int i = 0; i < 5; i++) {
        comp_sens.compress(randomLine, comp_lat, decomp_lat);
    }
    // previousRatio ~ 1.0. Zero block currentRatio = 2.0. Delta = 1.0 > 0.1
    comp_sens.compress(zeroLine, comp_lat, decomp_lat);

    EXPECT_DOUBLE_EQ(comp_sens.getDecayedUncompressedBits(), 512.0);
    EXPECT_DOUBLE_EQ(comp_sens.getDecayedCompressedBits(), 0.0);
}

/**
 * Test evaluation interval bypass state updates.
 */
TEST_F(BaseCompressorTest, EvaluationIntervalBypass)
{
    auto params = createParams(true, 1.35f, 1, 10, 0.8f, 1000.0f);
    TestBaseCompressor comp(params);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Access 1: totalCompressionRequests = 1, (1-1)%10 == 0 -> evaluates bypass
    // Random line: decayedUncompressed = 512, decayedCompressed = 512
    // observedRatio = 1.0 < 1.35 -> bypassActive = true
    comp.compress(randomLine, comp_lat, decomp_lat);
    EXPECT_TRUE(comp.getBypassActive());

    // Compress zero lines to reach highly compressible state
    // Zero line: comp_size = 0. observedRatio = 512/0 -> 2.0 > 1.35 -> bypassActive = false
    for (int i = 2; i <= 10; i++) {
        comp.compress(zeroLine, comp_lat, decomp_lat);
    }

    // Access 11: totalCompressionRequests = 11, (11-1)%10 == 0 -> evaluates bypass
    // observedRatio > 1.35 -> bypassActive = false
    comp.compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_FALSE(comp.getBypassActive());
}
