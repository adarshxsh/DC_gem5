/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

TEST(BaseCompressorTest, BypassedStats)
{
    ZeroCompressorParams zero_p{};
    zero_p.eventq_index = 0;
    zero_p.block_size = 64;
    zero_p.chunk_size_bits = 64;
    zero_p.size_threshold_percentage = 100;
    zero_p.comp_chunks_per_cycle = 8;
    zero_p.comp_extra_latency = Cycles(1);
    zero_p.decomp_chunks_per_cycle = 8;
    zero_p.decomp_extra_latency = Cycles(1);
    zero_p.dictionary_size = 64;
    zero_p.enable_adaptive_bypass = false;
    zero_p.latency_breakeven_threshold = 1.0;
    zero_p.sampling_interval = 100;
    zero_p.decay_shift = 4;

    Zero compressor(zero_p);

    compressor.incBypassedCompressions();
    compressor.incBypassedCompressions();
    compressor.incBypassedDecompressions();

    SUCCEED();
}
