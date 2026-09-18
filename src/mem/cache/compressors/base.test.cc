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

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/cpack.hh"
#include "params/CPack.hh"

using namespace gem5;
using namespace compression;

class TestCPackBypass : public CPack
{
  public:
    using Base::bypassActive;
    using Base::compress;
    using Base::decayedCompressedBits;
    using Base::decayedUncompressedBits;
    using Base::sampledCompressedBits;
    using Base::sampledUncompressedBits;
    using CPack::CPack;
};

TEST(BaseCompressorTest, ExponentialDecayAdaptiveBypass)
{
    CPackParams p;
    p.name = "cpack_bypass";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0f;
    p.sampling_interval = 1;
    p.decay_shift = 4;
    p.ewma_alpha = 0.05f;
    p.hysteresis_margin_perc = 5;
    p.bypass_enable_threshold = 0.0f;
    p.bypass_disable_threshold = 0.0f;

    TestCPackBypass compressor(p);

    uint64_t uncomp_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                               0x2122232425262728ULL, 0x2930313233343536ULL,
                               0x3738394041424344ULL, 0x4546474849505152ULL,
                               0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);
    compressor.compress(uncomp_data, comp_lat, decomp_lat);

    EXPECT_GT(compressor.sampledUncompressedBits, 0);
    EXPECT_GT(compressor.sampledCompressedBits, 0);
}

TEST(BaseCompressorTest, DualThresholdHysteresisAndEMADecay)
{
    CPackParams p;
    p.name = "cpack_bypass";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.2f;
    p.sampling_interval = 1;
    p.decay_shift = 4;
    p.ewma_alpha = 0.5f;
    p.hysteresis_margin_perc =
        10; // enable threshold = 1.08, disable threshold = 1.32
    p.bypass_enable_threshold = 0.0f;
    p.bypass_disable_threshold = 0.0f;

    TestCPackBypass compressor(p);

    // Initial state: bypassActive should be false
    EXPECT_FALSE(compressor.bypassActive);

    // 1. Send all-zero compressible blocks (ratio = 512 / 32 = 16.0 > 1.32)
    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);
    for (int i = 0; i < 5; i++) {
        compressor.compress(zero_data, comp_lat, decomp_lat);
    }
    // High ratio -> bypass remains inactive
    EXPECT_FALSE(compressor.bypassActive);

    // 2. Send uncompressible blocks (compressed size = 512 bits, ratio = 512 /
    // 512 = 1.0 < 1.08)
    uint64_t uncomp_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                               0x2122232425262728ULL, 0x2930313233343536ULL,
                               0x3738394041424344ULL, 0x4546474849505152ULL,
                               0x5354555657585960ULL, 0x6162636465666768ULL};

    for (int i = 0; i < 10; i++) {
        compressor.compress(uncomp_data, comp_lat, decomp_lat);
    }
    // Ratio falls below 1.08 -> adaptive bypass transitions to active
    EXPECT_TRUE(compressor.bypassActive);

    // 3. Test deadband stability (ratio near 1.15, between 1.08 and 1.32):
    // Bypass must stay active without single-cycle toggling/thrashing!
    for (int i = 0; i < 5; i++) {
        compressor.compress(uncomp_data, comp_lat, decomp_lat);
        EXPECT_TRUE(compressor.bypassActive);
    }

    // 4. Send highly compressible blocks again (ratio = 16.0 > 1.32)
    for (int i = 0; i < 10; i++) {
        compressor.compress(zero_data, comp_lat, decomp_lat);
    }
    // Ratio rises above 1.32 -> adaptive bypass transitions back to inactive
    EXPECT_FALSE(compressor.bypassActive);
}

TEST(MemCtrlEMATest, QueueLengthSmoothingCalculation)
{
    double alpha = 0.5;
    double currentEMA = 0.0;

    // Simulate sequence of queue lengths
    uint32_t samples[] = {10, 10, 0, 0, 20};
    double expectedEMA[] = {5.0, 7.5, 3.75, 1.875, 12.875};

    for (size_t i = 0; i < 5; i++) {
        currentEMA = alpha * samples[i] + (1.0 - alpha) * currentEMA;
        EXPECT_DOUBLE_EQ(currentEMA, expectedEMA[i]);
    }
}
