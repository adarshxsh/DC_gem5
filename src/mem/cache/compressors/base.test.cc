/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 *
 * Direct memory controller queue pressure probe unit tests
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "mem/cache/compressors/cpack.hh"
#include "mem/mem_ctrl.hh"
#include "params/CPack.hh"
#include "params/MemCtrl.hh"

using namespace gem5;
using namespace compression;

namespace
{

class TestCPack : public CPack
{
  public:
    using Base::compress;
    using CPack::CPack;
    using CPack::decompress;
};

class MockMemCtrl : public memory::MemCtrl
{
  public:
    MockMemCtrl(const MemCtrlParams &p) : memory::MemCtrl(p) {}

    void
    setPressureState(uint64_t total_writes, uint32_t buffer_size)
    {
        totalWriteQueueSize = total_writes;
        writeBufferSize = buffer_size;
    }
};

} // anonymous namespace

TEST(MemCtrlTest, WriteQueuePressureQuery)
{
    MemCtrlParams p;
    p.name = "test_mem_ctrl";

    MockMemCtrl ctrl(p);

    // Initial state: empty write queue
    ctrl.setPressureState(0, 64);
    EXPECT_DOUBLE_EQ(ctrl.getWriteQueuePressure(), 0.0);

    // 50% capacity
    ctrl.setPressureState(32, 64);
    EXPECT_DOUBLE_EQ(ctrl.getWriteQueuePressure(), 0.5);

    // 80% capacity
    ctrl.setPressureState(80, 100);
    EXPECT_DOUBLE_EQ(ctrl.getWriteQueuePressure(), 0.8);

    // 100% full capacity
    ctrl.setPressureState(64, 64);
    EXPECT_DOUBLE_EQ(ctrl.getWriteQueuePressure(), 1.0);

    // Edge case: zero buffer size
    ctrl.setPressureState(0, 0);
    EXPECT_DOUBLE_EQ(ctrl.getWriteQueuePressure(), 0.0);
}

TEST(CompressorMemPressureTest, MemoryPressureBypassTriggered)
{
    MemCtrlParams ctrl_p;
    ctrl_p.name = "ctrl";
    MockMemCtrl mem_ctrl(ctrl_p);

    CPackParams p;
    p.name = "cpack";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_mem_pressure_bypass = true;
    p.mem_pressure_threshold = 80; // 80%
    p.mem_ctrl = &mem_ctrl;

    TestCPack compressor(p);

    uint64_t data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                        0x2122232425262728ULL, 0x2930313233343536ULL,
                        0x3738394041424344ULL, 0x4546474849505152ULL,
                        0x5354555657585960ULL, 0x6162636465666768ULL};

    // 1. Write queue occupancy at 85% (exceeds 80% threshold)
    mem_ctrl.setPressureState(85, 100);
    Cycles comp_lat(10), decomp_lat(10);
    auto comp_data = compressor.compress(data, comp_lat, decomp_lat);

    // Compression latency and decompression latency must both be 0 cycles
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(), 64 * 8);

    // 2. Write queue occupancy at 50% (below 80% threshold)
    mem_ctrl.setPressureState(50, 100);
    comp_data = compressor.compress(data, comp_lat, decomp_lat);

    // Normal compression latency should apply
    EXPECT_GT(comp_lat, Cycles(0));
}

TEST(CompressorMemPressureTest, NullMemCtrlFallback)
{
    CPackParams p;
    p.name = "cpack";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_mem_pressure_bypass = true;
    p.mem_pressure_threshold = 80;
    p.mem_ctrl = nullptr; // Null mem_ctrl pointer

    TestCPack compressor(p);

    uint64_t data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                        0x2122232425262728ULL, 0x2930313233343536ULL,
                        0x3738394041424344ULL, 0x4546474849505152ULL,
                        0x5354555657585960ULL, 0x6162636465666768ULL};

    Cycles comp_lat(0), decomp_lat(0);
    // Should execute normal compression without throwing runtime exceptions
    EXPECT_NO_THROW({
        auto comp_data = compressor.compress(data, comp_lat, decomp_lat);
        EXPECT_GT(comp_lat, Cycles(0));
    });
}
