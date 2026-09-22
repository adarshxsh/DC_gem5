/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "mem/cache/base.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/BaseCacheCompressor.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;

namespace compression
{
class TestZero : public Zero
{
  public:
    bool mockCongested = false;

    using Base::compress;
    using Zero::Zero;

    bool isDownstreamCongested() const override
    {
        return mockCongested;
    }
};
}
}

using namespace gem5;
using namespace compression;

TEST(BaseCacheCompressorTest, MemCtrlCongestionBypassTest)
{
    ZeroCompressorParams p{};
    p.name = "zero_comp";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.dictionary_size = 1;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(0);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(0);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.queue_congestion_threshold_pct = 80;
    p.enable_congestion_bypass = true;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    TestZero compressor(p);
    compressor.regStats();

    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);

    // 1. Normal state (non-congested downstream memory)
    compressor.mockCongested = false;
    auto comp_data = compressor.compress(zero_data, comp_lat, decomp_lat);
    EXPECT_LT(comp_data->getSizeBits(), 512);

    // 2. Downstream memory congestion state
    compressor.mockCongested = true;
    comp_data = compressor.compress(zero_data, comp_lat, decomp_lat);

    // Latency must be bypassed (0 cycles) and size set to uncompressed line (512 bits)
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(), 512);
}
