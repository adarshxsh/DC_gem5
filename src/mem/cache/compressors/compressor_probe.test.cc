/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/cpack.hh"
#include "params/CPack.hh"
#include "sim/probe/probe.hh"

using namespace gem5;
using namespace compression;

class TestCPackProbe : public CPack
{
  public:
    using Base::compress;
    using CPack::CPack;
    using CPack::decompress;
    using Base::isMemoryCongested;
    using Base::handleMemoryCongestion;
};

TEST(CompressorProbeTest, MemoryPressureThrottlingBypass)
{
    CPackParams p;
    p.name = "cpack_probe";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_memory_pressure_throttling = true;

    TestCPackProbe compressor(p);

    EXPECT_FALSE(compressor.isMemoryCongested);

    uint64_t data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                        0x2122232425262728ULL, 0x2930313233343536ULL,
                        0x3738394041424344ULL, 0x4546474849505152ULL,
                        0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);

    // 1. When uncongested, compression performs normally
    auto comp_data = compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));

    // 2. Trigger memory congestion notification
    compressor.handleMemoryCongestion(true);
    EXPECT_TRUE(compressor.isMemoryCongested);

    // 3. When congested, compression is bypassed with 0 latency
    comp_lat = Cycles(10);
    decomp_lat = Cycles(10);
    auto comp_data_congested = compressor.compress(data, comp_lat, decomp_lat);

    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data_congested->getSizeBits(), 64 * 8);

    // 4. Trigger memory congestion clearance notification
    compressor.handleMemoryCongestion(false);
    EXPECT_FALSE(compressor.isMemoryCongested);

    // 5. Compression resumes standard latencies when congestion is resolved
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    auto comp_data_resolved = compressor.compress(data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
}
