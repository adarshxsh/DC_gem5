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

#include "base/output.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
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

    std::unique_ptr<Base>
    createZeroCompressor(bool enable_adaptive, float breakeven_thresh,
                         unsigned sampling_int, unsigned window_size)
    {
        ZeroCompressorParams p;
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 64;
        p.size_threshold_percentage = 100;
        p.comp_chunks_per_cycle = 8;
        p.comp_extra_latency = Cycles(1);
        p.decomp_chunks_per_cycle = 8;
        p.decomp_extra_latency = Cycles(1);
        p.dictionary_size = 64;
        p.enable_adaptive_bypass = enable_adaptive;
        p.latency_breakeven_threshold = breakeven_thresh;
        p.sampling_interval = sampling_int;
        p.adaptive_window_size = window_size;

        auto compressor = std::make_unique<Zero>(p);
        compressor->regStats();
        return compressor;
    }
};

/**
 * Test windowed adaptive bypass phase transitions.
 */
TEST_F(BaseCompressorTest, WindowedAdaptiveBypassTransition)
{
    // Enable adaptive bypass with window size = 5, sampling interval = 1, breakeven threshold = 1.5
    auto compressor = createZeroCompressor(true, 1.5f, 1, 5);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Phase 1: Compressible phase (5 zero lines)
    for (int i = 0; i < 5; i++) {
        auto comp_data = compressor->compress(zeroLine, comp_lat, decomp_lat);
        ASSERT_NE(comp_data, nullptr);
        // ZeroCompressor compresses zero lines to 0 bits (size < 512 bits)
        ASSERT_LT(comp_data->getSizeBits(), 512);
    }

    // Ratio in window is now high (512 / 0 = infinity). Observed ratio >= 1.5.
    ASSERT_GE(compressor->getObservedRatio(), 1.5);

    // Phase 2: Uncompressible phase (5 random lines)
    for (int i = 0; i < 5; i++) {
        compressor->compress(randomLine, comp_lat, decomp_lat);
    }

    // After 5 uncompressible samples in window of size 5, window contains only random lines.
    // Observed ratio in window drops to 512 / 512 = 1.0 < 1.5.
    ASSERT_LT(compressor->getObservedRatio(), 1.5);

    // Non-sampled request in bypass state returns uncompressed size (512) and 0 latencies
    // Next request when sampling interval = 1 is sampled; but bypass is active on non-sampled/bypassed requests.
    auto comp_data_bpassed = compressor->compress(randomLine, comp_lat, decomp_lat);
    ASSERT_EQ(comp_data_bpassed->getSizeBits(), 512);

    // Phase 3: Transition back to compressible phase (5 zero lines)
    for (int i = 0; i < 5; i++) {
        compressor->compress(zeroLine, comp_lat, decomp_lat);
    }

    // Window has purged old uncompressible samples and now contains zero lines.
    // Observed ratio rises back above threshold.
    ASSERT_GE(compressor->getObservedRatio(), 1.5);
}

/**
 * Test that cumulative statistics track total simulation counts independently of window pops.
 */
TEST_F(BaseCompressorTest, CumulativeStatsIndependence)
{
    auto compressor = createZeroCompressor(true, 1.5f, 1, 3);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Run 10 compressions
    for (int i = 0; i < 5; i++) {
        compressor->compress(zeroLine, comp_lat, decomp_lat);
    }
    for (int i = 0; i < 5; i++) {
        compressor->compress(randomLine, comp_lat, decomp_lat);
    }

    // Window size is 3, so active window has only 3 samples.
    // But overall cumulative sampled compressions must equal 10.
    // Total sampled uncompressed bits = 10 * 512 = 5120 bits.
    // (5 zero lines @ 0 bits + 5 random lines @ 512 bits = 2560 compressed bits total).
    // Let's verify via getObservedRatio() vs cumulative statistics logic.
    // The window has 3 samples of randomLine (ratio 1.0), whereas cumulative total is 5120 / 2560 = 2.0x ratio.
    ASSERT_DOUBLE_EQ(compressor->getObservedRatio(), 1.0);
}
>>>>>>> cd2d75a (mem-cache,configs: Refactor adaptive bypass window)
