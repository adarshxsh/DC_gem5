/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/eventq.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

EventQueue testEventQueue("testEventQueue");

class DummyCompressor : public Base
{
  public:
    using Base::compress;
    using Base::getEWMARatio;
    using Base::getLastDQdt;
    using Base::getLastQueueOccupancy;

    DummyCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk>& chunks,
             Cycles& comp_lat, Cycles& decomp_lat) override
    {
        std::unique_ptr<CompressionData> comp_data =
            std::make_unique<CompressionData>();
        // Compressed size is 256 bits (for 512-bit block, ratio is 2.0)
        comp_data->setSizeBits(256);
        comp_lat = Cycles(2);
        decomp_lat = Cycles(1);
        return comp_data;
    }

    void
    decompress(const CompressionData* comp_data, uint64_t* data) override
    {
        std::memset(data, 0, blkSize);
    }
};

TEST(BaseCacheCompressorTest, ContinuousEWMATracking)
{
    curEventQueue(&testEventQueue);

    BaseCacheCompressorParams p{};
    p.name = "dummy_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(0);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(0);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.5;
    p.sampling_interval = 100;
    p.decay_shift = 4;
    p.ewma_alpha = 0.2;
    p.predictive_gain = 0.0;
    p.latency_xbar = Cycles(10);

    DummyCompressor compressor(p);
    compressor.regStats();

    double initial_ratio = compressor.getEWMARatio();
    EXPECT_DOUBLE_EQ(initial_ratio, 2.5); // latencyBreakevenThreshold + 1.0

    uint64_t dummy_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    Cycles comp_lat(0), decomp_lat(0);

    // Request 1: should update EWMA ratio immediately without waiting for interval 100
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    double ratio_1 = compressor.getEWMARatio();

    // Sample ratio is 512 / 256 = 2.0.
    // ewmaRatio = (1 - 0.2) * 2.5 + 0.2 * 2.0 = 2.0 + 0.4 = 2.4
    EXPECT_NEAR(ratio_1, 2.4, 1e-5);

    // Request 2: continuous EWMA update
    compressor.compress(dummy_data, comp_lat, decomp_lat);
    double ratio_2 = compressor.getEWMARatio();
    // ewmaRatio = (1 - 0.2) * 2.4 + 0.2 * 2.0 = 1.92 + 0.4 = 2.32
    EXPECT_NEAR(ratio_2, 2.32, 1e-5);
}

TEST(BaseCacheCompressorTest, BackwardCompatibilityWhenPredictiveGainZero)
{
    curEventQueue(&testEventQueue);

    BaseCacheCompressorParams p{};
    p.name = "dummy_compressor_compat";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(0);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(0);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 10;
    p.decay_shift = 4;
    p.ewma_alpha = 0.1;
    p.predictive_gain = 0.0; // Predictive feedback disabled
    p.latency_xbar = Cycles(10);

    DummyCompressor compressor(p);
    compressor.regStats();

    uint64_t dummy_data[8] = {0};
    Cycles comp_lat(0), decomp_lat(0);

    auto comp_data = compressor.compress(dummy_data, comp_lat, decomp_lat);
    EXPECT_NE(comp_data, nullptr);
    EXPECT_GT(compressor.getEWMARatio(), 0.0);
}
