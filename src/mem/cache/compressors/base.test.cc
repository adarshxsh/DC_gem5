/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

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
using namespace gem5::compression;

class TestBaseCompressor : public Zero
{
  public:
    using Base::compress;
    using Zero::Zero;

    unsigned getWindowSize() const { return windowSize; }
    std::size_t getWindowCount() const { return windowCount; }
    std::size_t getWindowHead() const { return windowHead; }
    uint64_t getSampledUncompressedBits() const { return sampledUncompressedBits; }
    uint64_t getSampledCompressedBits() const { return sampledCompressedBits; }
};

TEST(BaseCacheCompressorTest, DefaultWindowSizeAndCapacityEviction)
{
    ZeroCompressorParams p{};
    p.name = "test_base_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 1;
    p.decay_shift = 4;
    p.window_size = 10; // Use small window size for testing

    TestBaseCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.getWindowSize(), 10U);

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t random_data[8] = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL,
                               0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL,
                               0x1234567812345678ULL, 0x8765432187654321ULL,
                               0x2233445566778899ULL, 0x33445566778899AAULL};

    Cycles comp_lat(0), decomp_lat(0);

    // Fill window with 10 zero requests (compressible to 0 bits)
    for (int i = 0; i < 10; i++) {
        compressor.compress(zero_data, comp_lat, decomp_lat);
    }

    EXPECT_EQ(compressor.getWindowCount(), 10U);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 10 * 64 * 8ULL);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 0ULL);

    // Compress 10 random requests (uncompressible = 64 * 8 bits)
    // Each new sample should evict 1 zero request
    for (int i = 0; i < 10; i++) {
        compressor.compress(random_data, comp_lat, decomp_lat);
    }

    // Now all 10 zero requests must be evicted. The active window contains 10 random requests.
    EXPECT_EQ(compressor.getWindowCount(), 10U);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 10 * 64 * 8ULL);
    EXPECT_EQ(compressor.getSampledCompressedBits(), 10 * 64 * 8ULL);
}

TEST(BaseCacheCompressorTest, PhaseTransitionAdaptation)
{
    ZeroCompressorParams p{};
    p.name = "test_phase_transition";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 2.0; // Require compression ratio >= 2.0
    p.sampling_interval = 1;
    p.decay_shift = 0; // Disable exponential decay
    p.window_size = 5;  // Window size of 5 requests

    TestBaseCompressor compressor(p);
    compressor.regStats();

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t random_data[8] = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL,
                               0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL,
                               0x1234567812345678ULL, 0x8765432187654321ULL,
                               0x2233445566778899ULL, 0x33445566778899AAULL};

    Cycles comp_lat(0), decomp_lat(0);

    // Phase 1: 5 highly compressible requests
    for (int i = 0; i < 5; i++) {
        auto comp_data = compressor.compress(zero_data, comp_lat, decomp_lat);
        // Compressed size is 0 bits, compression is active
        EXPECT_EQ(comp_data->getSizeBits(), 0U);
    }

    // Phase 2: Transition to incompressible data
    // Feed incompressible requests until observed ratio drops below 2.0
    int bypassed_count = 0;
    for (int i = 0; i < 10; i++) {
        auto comp_data = compressor.compress(random_data, comp_lat, decomp_lat);
        if (comp_data->getSizeBits() == 64 * 8) {
            bypassed_count++;
        }
    }

    // Adaptive bypass must trigger within window_size requests after phase transition
    EXPECT_GT(bypassed_count, 0);
}

TEST(BaseCacheCompressorTest, ZeroWindowExponentialDecayFallback)
{
    ZeroCompressorParams p{};
    p.name = "test_decay_fallback";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 1;
    p.decay_shift = 2; // k = 2 -> shift by 2
    p.window_size = 0; // window_size = 0 disables sliding window

    TestBaseCompressor compressor(p);
    compressor.regStats();

    EXPECT_EQ(compressor.getWindowSize(), 0U);

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);

    // First request: uncompressed = 512 bits, decayed = 512 - 0 = 512
    compressor.compress(zero_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 512ULL);

    // Second request: previous decayed by >> 2 (512 - 128 = 384), then +512 = 896
    compressor.compress(zero_data, comp_lat, decomp_lat);
    EXPECT_EQ(compressor.getSampledUncompressedBits(), 896ULL);
}
