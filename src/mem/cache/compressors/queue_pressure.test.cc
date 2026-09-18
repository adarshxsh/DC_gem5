/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/cpack.hh"
#include "mem/mem_ctrl.hh"
#include "params/CPack.hh"
#include "params/MemCtrl.hh"
#include "sim/clock_domain.hh"

using namespace gem5;
using namespace compression;

class TestMemCtrl : public memory::MemCtrl
{
  public:
    TestMemCtrl(const MemCtrlParams &p) : memory::MemCtrl(p) {}

    void
    setReadQueueState(uint64_t read_size, uint32_t buf_size)
    {
        totalReadQueueSize = read_size;
        readBufferSize = buf_size;
    }

    void
    setWriteQueueState(uint64_t write_size, uint32_t buf_size)
    {
        totalWriteQueueSize = write_size;
        writeBufferSize = buf_size;
    }
};

class TestCPackCompressor : public CPack
{
  public:
    using Base::compress;
    using CPack::CPack;
    using CPack::decompress;
};

static MemCtrlParams
createTestMemCtrlParams(const std::string &name)
{
    static SrcClockDomainParams clk_params;
    static SrcClockDomain clk_domain(clk_params);
    MemCtrlParams p;
    p.name = name;
    p.clk_domain = &clk_domain;
    return p;
}

TEST(MemCtrlQueuePressureTest, QueuePressureCalculation)
{
    MemCtrlParams p = createTestMemCtrlParams("test_mem_ctrl");

    TestMemCtrl mem_ctrl(p);

    // Initial state: 0% pressure
    EXPECT_DOUBLE_EQ(mem_ctrl.getQueuePressure(), 0.0);

    // Read queue pressure 70% (70 / 100), Write queue 20% (10 / 50) -> max is
    // 70%
    mem_ctrl.setReadQueueState(70, 100);
    mem_ctrl.setWriteQueueState(10, 50);
    EXPECT_DOUBLE_EQ(mem_ctrl.getQueuePressure(), 70.0);

    // Write queue pressure 90% (45 / 50), Read queue 70% -> max is 90%
    mem_ctrl.setWriteQueueState(45, 50);
    EXPECT_DOUBLE_EQ(mem_ctrl.getQueuePressure(), 90.0);
}

TEST(QueuePressureThrottlingTest,
     CompressionBypassWhenQueuePressureExceedsThreshold)
{
    class MockMemCtrl : public memory::MemCtrl
    {
      public:
        double mockPressure;
        MockMemCtrl(const MemCtrlParams &p)
            : memory::MemCtrl(p), mockPressure(0.0)
        {}
        double
        getQueuePressure() const override
        {
            return mockPressure;
        }
    };

    MemCtrlParams mem_p = createTestMemCtrlParams("mock_mem_ctrl");
    MockMemCtrl mock_mem_ctrl(mem_p);

    CPackParams p;
    p.name = "cpack";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.enable_queue_pressure_throttling = true;
    p.memory_queue_pressure_threshold = 80;
    p.mem_ctrl = &mock_mem_ctrl;

    TestCPackCompressor compressor(p);

    uint64_t test_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                             0x2122232425262728ULL, 0x2930313233343536ULL,
                             0x3738394041424344ULL, 0x4546474849505152ULL,
                             0x5354555657585960ULL, 0x6162636465666768ULL};

    // 1. Queue pressure below threshold (50% <= 80%): Compression NOT bypassed
    mock_mem_ctrl.mockPressure = 50.0;
    Cycles comp_lat(0), decomp_lat(0);
    auto comp_data = compressor.compress(test_data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_GT(decomp_lat, Cycles(0));

    // 2. Queue pressure above threshold (85% > 80%): Compression IS bypassed
    mock_mem_ctrl.mockPressure = 85.0;
    comp_lat = Cycles(10);
    decomp_lat = Cycles(10);
    comp_data = compressor.compress(test_data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(), 64 * 8);

    // 3. Queue pressure throttling disabled despite high pressure
    p.enable_queue_pressure_throttling = false;
    TestCPackCompressor compressor_disabled(p);
    mock_mem_ctrl.mockPressure = 90.0;
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    comp_data = compressor_disabled.compress(test_data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_GT(decomp_lat, Cycles(0));

    // 4. Unbound memory controller (mem_ctrl = nullptr)
    p.enable_queue_pressure_throttling = true;
    p.mem_ctrl = nullptr;
    TestCPackCompressor compressor_unbound(p);
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    comp_data = compressor_unbound.compress(test_data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_GT(decomp_lat, Cycles(0));
}
