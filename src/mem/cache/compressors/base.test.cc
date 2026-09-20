/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/super_blk.hh"
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
    DummyCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        return nullptr;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {}
};

TEST(BaseCompressorTest, AdaptiveDecompressionThrottlingDisabled)
{
    BaseCacheCompressorParams p{};
    p.name = "dummy_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.enable_adaptive_decompression_throttling = false;
    p.decompression_throttle_threshold = 80;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    DummyCompressor compressor(p);
    compressor.regStats();

    CompressionBlk blk;
    blk.setCompressed();
    blk.setSizeBits(128);
    blk.setDecompressionLatency(Cycles(9));

    Cycles decomp_lat = compressor.getDecompressionLatency(&blk);
    EXPECT_EQ(decomp_lat, Cycles(9));
}

TEST(BaseCompressorTest, AdaptiveDecompressionThrottlingEnabled)
{
    BaseCacheCompressorParams p{};
    p.name = "dummy_compressor_throttled";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.enable_adaptive_decompression_throttling = true;
    p.decompression_throttle_threshold = 0; // 0% threshold forces throttling
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    DummyCompressor compressor(p);
    compressor.regStats();

    CompressionBlk blk;
    blk.setCompressed();
    blk.setSizeBits(128);
    blk.setDecompressionLatency(Cycles(9));

    Cycles decomp_lat = compressor.getDecompressionLatency(&blk);
    EXPECT_EQ(decomp_lat, Cycles(0));
}
