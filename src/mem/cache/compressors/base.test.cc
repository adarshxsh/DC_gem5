/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{

Root *Root::_root = nullptr;

namespace compression
{

class TestZeroCompressor : public Zero
{
  public:
    using Base::compress;
    using Zero::Zero;

    using Base::getObservedRatio;
    using Base::getSampledCompressedBits;
    using Base::getSampledUncompressedBits;
    using Base::getWindowSize;
};

TEST(BaseCompressorTest, SlidingWindowRingBufferSampling)
{
    ZeroCompressorParams p{};
    p.name = "test_zero";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.5;
    p.sampling_interval = 1;
    p.decay_shift = 4;
    p.window_size = 4;

    TestZeroCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.getWindowSize(), 4);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 0);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 0);

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t uncompressible_data[8] = {
        0x1234567891011121ULL, 0x1314151617181920ULL, 0x2122232425262728ULL,
        0x2930313233343536ULL, 0x3738394041424344ULL, 0x4546474849505152ULL,
        0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);

    // 1. Fill window with 4 compressible (zero) blocks. ZeroCompressor
    // compresses zero block to 0 bits.
    for (int i = 0; i < 4; i++) {
        compressor.compress(zero_data, comp_lat, decomp_lat);
    }

    EXPECT_EQ(compressor.getSampledUncompressedBits(), 4 * 512);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 0);
    EXPECT_GT(compressor.getObservedRatio(), 1.5);

    // 2. Push 2 uncompressible blocks (512 bits compressed each).
    // Window now has 2 zero blocks (0 bits) and 2 uncompressible blocks (512
    // bits each).
    compressor.compress(uncompressible_data, comp_lat, decomp_lat);
    compressor.compress(uncompressible_data, comp_lat, decomp_lat);

    EXPECT_EQ(compressor.getSampledUncompressedBits(), 4 * 512); // 2048
    EXPECT_EQ(compressor.getSampledCompressedBits(), 2 * 512);   // 1024
    EXPECT_DOUBLE_EQ(compressor.getObservedRatio(), 2.0); // 2048 / 1024 = 2.0

    // 3. Push 3rd uncompressible block.
    // Window now has 1 zero block (0 bits) and 3 uncompressible blocks (512
    // bits each).
    compressor.compress(uncompressible_data, comp_lat, decomp_lat);

    EXPECT_EQ(compressor.getSampledUncompressedBits(), 4 * 512); // 2048
    EXPECT_EQ(compressor.getSampledCompressedBits(), 3 * 512);   // 1536
    EXPECT_DOUBLE_EQ(compressor.getObservedRatio(),
                     2048.0 / 1536.0); // 1.333...

    // Ratio is 1.333... which is below latency_breakeven_threshold (1.5)!
    // 4. Push 4th uncompressible block. Adaptive bypass should bypass
    // compression.
    auto comp_data =
        compressor.compress(uncompressible_data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data->getSizeBits(),
              512); // uncompressed size due to bypass
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));

    EXPECT_EQ(compressor.getSampledUncompressedBits(), 4 * 512); // 2048
    EXPECT_EQ(compressor.getSampledCompressedBits(), 4 * 512);   // 2048
    EXPECT_DOUBLE_EQ(compressor.getObservedRatio(), 1.0);
}

TEST(BaseCompressorTest, FallbackToExponentialDecayWhenWindowSizeZero)
{
    ZeroCompressorParams p{};
    p.name = "test_zero_decay";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.5;
    p.sampling_interval = 1;
    p.decay_shift = 1; // 50% decay per sample
    p.window_size = 0; // Explicitly 0 -> use exponential decay

    TestZeroCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.getWindowSize(), 0);

    uint64_t uncompressible_data[8] = {
        0x1234567891011121ULL, 0x1314151617181920ULL, 0x2122232425262728ULL,
        0x2930313233343536ULL, 0x3738394041424344ULL, 0x4546474849505152ULL,
        0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);

    // Sample 1: 512 bits uncompressed, 512 bits compressed.
    compressor.compress(uncompressible_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 512);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 512);
    EXPECT_DOUBLE_EQ(compressor.getObservedRatio(), 1.0);

    // Sample 2: decay reduces previous (512, 512) to (256, 256), then adds
    // (512, 512) -> (768, 768).
    compressor.compress(uncompressible_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 768);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 768);
    EXPECT_DOUBLE_EQ(compressor.getObservedRatio(), 1.0);
}

} // namespace compression
} // namespace gem5
