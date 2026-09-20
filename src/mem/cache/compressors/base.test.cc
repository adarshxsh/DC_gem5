/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

class DummyCompressor : public Base
{
  public:
    std::size_t mockCompressedSizeBits;

    DummyCompressor(const BaseCacheCompressorParams &p)
        : Base(p), mockCompressedSizeBits(512)
    {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk>& chunks,
             Cycles& comp_lat, Cycles& decomp_lat) override
    {
        auto comp_data = std::make_unique<CompressionData>();
        comp_data->setSizeBits(mockCompressedSizeBits);
        comp_lat = Cycles(2);
        decomp_lat = Cycles(2);
        return comp_data;
    }

    void
    decompress(const CompressionData* comp_data, uint64_t* data) override
    {}

    using Base::compress;
};

TEST(BaseCompressorTest, HysteresisWindowingBypassTransitions)
{
    BaseCacheCompressorParams p{};
    p.name = "dummy_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.05;
    p.hysteresis_low_threshold = 1.03;
    p.hysteresis_high_threshold = 1.08;
    p.sampling_interval = 1; // sample every compression
    p.decay_shift = 2; // decay factor 1 - 2^-2 = 0.75

    DummyCompressor compressor(p);
    compressor.regStats();

    uint64_t data[8] = {0};
    Cycles comp_lat(0), decomp_lat(0);

    // 1. Excellent compression (256 bits compressed for 512 bits uncompressed => ratio 2.0 > 1.08)
    compressor.mockCompressedSizeBits = 256;
    for (int i = 0; i < 30; ++i) {
        compressor.compress(data, comp_lat, decomp_lat);
    }
    // Initially not bypassing and ratio is 2.0 > 1.08. Compression active:
    compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(2)); // Active compression

    // 2. Mediocre compression (500 bits compressed => ratio 512/500 = 1.024 < 1.03)
    compressor.mockCompressedSizeBits = 500;
    for (int i = 0; i < 30; ++i) {
        compressor.compress(data, comp_lat, decomp_lat);
    }
    // Cumulative ratio drops below 1.03, entering bypass mode!
    // Non-sampled/bypass compression must return 0 latency
    compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0)); // Bypassed

    // 3. Modest compression improvement (490 bits compressed => ratio 512/490 = 1.045, between 1.03 and 1.08)
    compressor.mockCompressedSizeBits = 490;
    for (int i = 0; i < 30; ++i) {
        compressor.compress(data, comp_lat, decomp_lat);
    }
    // Observed ratio moves to ~1.045 (between 1.03 and 1.08), stays in bypass mode due to hysteresis
    compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0)); // Still bypassed due to hysteresis window!

    // 4. Significant compression improvement (256 bits compressed => ratio 2.0 > 1.08)
    compressor.mockCompressedSizeBits = 256;
    for (int i = 0; i < 30; ++i) {
        compressor.compress(data, comp_lat, decomp_lat);
    }
    // Observed ratio rises above 1.08, exiting bypass mode.
    // Compression is re-enabled!
    compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(2)); // Compression re-enabled
}
