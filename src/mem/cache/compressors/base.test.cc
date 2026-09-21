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

using namespace gem5;
using namespace compression;

namespace gem5
{
Root *Root::_root = nullptr;
}

class TestCompressor : public Base
{
  public:
    std::size_t mockCompSizeBits = 64;

    TestCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    using Base::compress;

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        std::unique_ptr<CompressionData> comp_data =
            std::make_unique<CompressionData>();
        comp_data->setSizeBits(mockCompSizeBits);
        comp_lat = Cycles(1);
        decomp_lat = Cycles(1);
        return comp_data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {}

    void
    setMockSampling(uint64_t uncompBits, uint64_t compBits)
    {
        sampledUncompressedBits = uncompBits;
        sampledCompressedBits = compBits;
    }
};

TEST(BaseCompressorTest, HysteresisStateTransitions)
{
    BaseCacheCompressorParams p;
    p.name = "test_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0f;
    p.hysteresis_delta = 0.1f; // Low watermark = 0.9, High watermark = 1.1
    p.sampling_interval = 1;
    p.decay_shift = 0;

    TestCompressor compressor(p);
    compressor.regStats();

    // Initial state: adaptive bypass inactive
    EXPECT_FALSE(compressor.isBypassActive());

    Cycles comp_lat, decomp_lat;
    uint64_t dummy_data[8] = {0};

    // 1. Sampling with ratio > high watermark (e.g. 1.25): bypass stays
    // inactive
    compressor.setMockSampling(1000, 800); // 1000 / 800 = 1.25 > 1.1
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    EXPECT_FALSE(compressor.isBypassActive());

    // 2. Sampling with ratio < low watermark (e.g. 0.8): bypass activates
    compressor.setMockSampling(800, 1000); // 800 / 1000 = 0.8 < 0.9
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    EXPECT_TRUE(compressor.isBypassActive());

    // 3. Sampling with ratio in hysteresis band (e.g. 1.0): bypass remains
    // active (dampening)
    compressor.setMockSampling(
        1000, 1000); // 1000 / 1000 = 1.0 (between 0.9 and 1.1)
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    EXPECT_TRUE(compressor.isBypassActive());

    // 4. Sampling with ratio > high watermark (e.g. 1.2): bypass deactivates
    compressor.setMockSampling(1200, 1000); // 1200 / 1000 = 1.2 > 1.1
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    EXPECT_FALSE(compressor.isBypassActive());
}
