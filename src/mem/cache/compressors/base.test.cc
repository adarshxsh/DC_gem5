/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/BaseCacheCompressor.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

class TestZeroCompressor : public Zero
{
  public:
    using Base::compress;
    using Base::getDecompressionLatency;
    using Base::getObservedRatio;
    using Base::windowCount;
    using Base::windowHead;
    using Base::windowSize;
    using Base::windowUncompressedBits;
    using Base::windowCompressedBits;
    using Zero::Zero;
};

static ZeroCompressorParams
createParams(unsigned window_size = 32, bool adaptive_bypass = true,
             float breakeven = 1.5f, unsigned sampling_int = 1)
{
    ZeroCompressorParams p{};
    p.name = "zero_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(0);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(0);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = adaptive_bypass;
    p.latency_breakeven_threshold = breakeven;
    p.sampling_interval = sampling_int;
    p.decay_shift = 0; // disable decay for explicit window test
    p.window_size = window_size;
    return p;
}

TEST(BaseCompressorTest, SlidingWindowRingBufferUpdatesAndEvicts)
{
    auto p = createParams(4, false, 1.5f, 1);
    TestZeroCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.windowSize, 4);
    EXPECT_EQ(compressor.windowCount, 0);
    EXPECT_EQ(compressor.windowUncompressedBits, 0);
    EXPECT_EQ(compressor.windowCompressedBits, 0);

    uint64_t zero_block[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t uncomp_block[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    Cycles c_lat(0), d_lat(0);

    // Insert 1 zero block (64 bytes = 512 bits uncompressed, 0 bits compressed for zero)
    compressor.compress(zero_block, c_lat, d_lat);
    EXPECT_EQ(compressor.windowCount, 1);
    EXPECT_EQ(compressor.windowUncompressedBits, 512);
    EXPECT_EQ(compressor.windowCompressedBits, 0);

    // Insert 3 more zero blocks (total 4 elements, buffer full)
    for (int i = 0; i < 3; i++) {
        compressor.compress(zero_block, c_lat, d_lat);
    }
    EXPECT_EQ(compressor.windowCount, 4);
    EXPECT_EQ(compressor.windowUncompressedBits, 2048);
    EXPECT_EQ(compressor.windowCompressedBits, 0);

    // Adding 5th sample (uncompressible block: 512 bits uncompressed, 512 bits compressed)
    // Should evict 1 zero block
    compressor.compress(uncomp_block, c_lat, d_lat);
    EXPECT_EQ(compressor.windowCount, 4);
    EXPECT_EQ(compressor.windowUncompressedBits, 2048); // 3 * 512 + 512
    EXPECT_EQ(compressor.windowCompressedBits, 512);    // 3 * 0 + 512

    // Check windowed ratio: 2048 / 512 = 4.0
    EXPECT_NEAR(compressor.getObservedRatio(), 4.0, 1e-4);
}

TEST(BaseCompressorTest, PhaseTransitionAdaptiveBypass)
{
    // Window size = 5, breakeven threshold = 2.0
    auto p = createParams(5, true, 2.0f, 1);
    TestZeroCompressor compressor(p);
    compressor.regStats();

    uint64_t zero_block[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t uncomp_block[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Cycles c_lat(0), d_lat(0);

    // Phase 1: 100 zero blocks (highly compressible)
    for (int i = 0; i < 100; i++) {
        compressor.compress(zero_block, c_lat, d_lat);
    }
    EXPECT_GT(compressor.getObservedRatio(), 2.0);

    // Phase 2: Transition to uncompressible blocks
    // Stream 5 uncompressible blocks (filling the window of size 5)
    for (int i = 0; i < 5; i++) {
        compressor.compress(uncomp_block, c_lat, d_lat);
    }

    // Now the window of size 5 contains ONLY uncompressible blocks
    // windowUncompressedBits = 5 * 512 = 2560
    // windowCompressedBits = 5 * 512 = 2560
    // Observed ratio = 1.0 < 2.0 (threshold)
    EXPECT_NEAR(compressor.getObservedRatio(), 1.0, 1e-4);

    // Next request should trigger adaptive bypass
    auto comp_data = compressor.compress(uncomp_block, c_lat, d_lat);
    EXPECT_EQ(c_lat, Cycles(0));
    EXPECT_EQ(d_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(), 512);
}

TEST(BaseCompressorTest, FallbackWhenWindowSizeIsZero)
{
    // window_size = 0 disables sliding window ring buffer
    auto p = createParams(0, true, 1.5f, 1);
    TestZeroCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.windowSize, 0);
    EXPECT_EQ(compressor.windowCount, 0);

    uint64_t zero_block[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles c_lat(0), d_lat(0);

    compressor.compress(zero_block, c_lat, d_lat);

    // windowUncompressedBits remains 0
    EXPECT_EQ(compressor.windowUncompressedBits, 0);
    EXPECT_EQ(compressor.windowCompressedBits, 0);

    // Observed ratio uses scalar counters (512 uncompressed / 0 compressed -> ratio defaults when comp bits is 0)
    // When sampledCompressedBits is 0 (since zero block compressed bits = 0), ratio is latencyBreakevenThreshold + 1.0 = 2.5
    EXPECT_NEAR(compressor.getObservedRatio(), 2.5, 1e-4);
}
