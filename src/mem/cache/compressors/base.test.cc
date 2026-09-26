/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/cpack.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "params/CPack.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

class TestBaseCompressor : public CPack
{
  public:
    using Base::compress;
    using Base::sampledCompressedBits;
    using Base::sampledUncompressedBits;
    using Base::sampleWindowEntries;
    using Base::sampleWindowHead;
    using Base::sampleWindowSize;
    using CPack::CPack;
};

TEST(BaseCacheCompressorTest, SlidingWindowEvictionAndRatio)
{
    CPackParams p{};
    p.name = "cpack_window_test";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 1;
    p.decay_shift = 4;
    p.sample_window_size = 2;

    TestBaseCompressor compressor(p);
    compressor.regStats();

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t uncomp_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                               0x2122232425262728ULL, 0x2930313233343536ULL,
                               0x3738394041424344ULL, 0x4546474849505152ULL,
                               0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);

    // Sample 1: All zeroes block (compresses to 32 bits out of 512)
    compressor.compress(zero_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.sampleWindowEntries, 1);
    EXPECT_EQ(compressor.sampledUncompressedBits, 512);
    EXPECT_EQ(compressor.sampledCompressedBits, 32);

    // Sample 2: Incompressible block (compresses to 512 bits)
    compressor.compress(uncomp_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.sampleWindowEntries, 2);
    EXPECT_EQ(compressor.sampledUncompressedBits, 1024);
    EXPECT_EQ(compressor.sampledCompressedBits, 544);

    // Sample 3: Incompressible block (overwrites Sample 1)
    compressor.compress(uncomp_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.sampleWindowEntries, 2);
    // Sample 1 (512 uncompressed, 32 compressed) evicted!
    // Remaining window: Sample 2 (512, 512) + Sample 3 (512, 512)
    EXPECT_EQ(compressor.sampledUncompressedBits, 1024);
    EXPECT_EQ(compressor.sampledCompressedBits, 1024);
}

TEST(BaseCacheCompressorTest, FallbackWhenWindowSizeZero)
{
    CPackParams p{};
    p.name = "cpack_fallback_test";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 1;
    p.decay_shift = 1;        // 50% decay
    p.sample_window_size = 0; // Disabled windowing

    TestBaseCompressor compressor(p);
    compressor.regStats();

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);

    compressor.compress(zero_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.sampleWindowEntries, 0);
    EXPECT_EQ(compressor.sampledUncompressedBits, 512);
    EXPECT_EQ(compressor.sampledCompressedBits, 32);

    // Second sample applies 50% decay to previous running totals
    compressor.compress(zero_data, comp_lat, decomp_lat);
    // sampledUncompressedBits = (512 - 256) + 512 = 768
    // sampledCompressedBits = (32 - 16) + 32 = 48
    EXPECT_EQ(compressor.sampledUncompressedBits, 768);
    EXPECT_EQ(compressor.sampledCompressedBits, 48);
}

TEST(BaseCacheCompressorTest, PhaseTransitionAndAdaptiveBypass)
{
    CPackParams p{};
    p.name = "cpack_phase_test";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 2.0; // Ratio below 2.0 triggers bypass
    p.sampling_interval = 1;
    p.decay_shift = 0;
    p.sample_window_size = 4; // Window size W = 4

    TestBaseCompressor compressor(p);
    compressor.regStats();

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t uncomp_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                               0x2122232425262728ULL, 0x2930313233343536ULL,
                               0x3738394041424344ULL, 0x4546474849505152ULL,
                               0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);

    // Phase 1: Compressible blocks (ratio = 16.0)
    for (int i = 0; i < 4; i++) {
        compressor.compress(zero_data, comp_lat, decomp_lat);
    }
    // Window ratio = 2048 / 128 = 16.0 > threshold 2.0
    double ratio = (double)compressor.sampledUncompressedBits /
                   compressor.sampledCompressedBits;
    EXPECT_DOUBLE_EQ(ratio, 16.0);

    // Phase 2: Enter uncompressible phase
    for (int i = 0; i < 4; i++) {
        compressor.compress(uncomp_data, comp_lat, decomp_lat);
    }
    // After 4 uncompressible samples (within W = 4 samples),
    // all compressible samples are evicted.
    // Window ratio = 2048 / 2048 = 1.0 < threshold 2.0
    ratio = (double)compressor.sampledUncompressedBits /
            compressor.sampledCompressedBits;
    EXPECT_DOUBLE_EQ(ratio, 1.0);
}
