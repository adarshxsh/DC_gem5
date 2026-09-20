/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"

using namespace gem5;
using namespace compression;

class TestBaseCompressor : public Base
{
  public:
    using Base::Base;
    using Base::compress;

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        auto data = std::make_unique<CompressionData>();
        data->setSizeBits(blkSize * 8 / 2);
        comp_lat = Cycles(1);
        decomp_lat = Cycles(1);
        return data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {
        std::memset(cache_line, 0, blkSize);
    }
};

TEST(BaseCompressorTest, PressureThresholdParams)
{
    BaseCacheCompressorParams p;
    p.name = "test_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.size_threshold_percentage = 50;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;
    p.enable_queue_pressure_throttling = true;
    p.queue_pressure_threshold = 85;
    p.decay_factor = 0.8;
    p.ewma_alpha = 0.05;
    p.hysteresis_margin_perc = 5;

    TestBaseCompressor compressor(p);

    uint64_t test_data[8] = {0};
    Cycles comp_lat(0), decomp_lat(0);
    auto comp_data = compressor.compress(test_data, comp_lat, decomp_lat);

    EXPECT_NE(comp_data, nullptr);
}
