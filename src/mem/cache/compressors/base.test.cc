/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
#include "enums/MemoryQueuePressure.hh"
#include "sim/root.hh"

#ifndef GEM5_ROOT_DEFINED
#define GEM5_ROOT_DEFINED
namespace gem5
{
Root *Root::_root = nullptr;
}
#endif

using namespace gem5;
using namespace gem5::compression;

class BaseCompressorProbeTest : public ::testing::Test
{
  protected:
    std::unique_ptr<Zero> zeroComp;
    uint64_t zeroLine[8];
    uint64_t randomLine[8];

    void SetUp() override
    {
        std::memset(zeroLine, 0, sizeof(zeroLine));
        for (int i = 0; i < 8; ++i) {
            randomLine[i] = 0x1122334455667788ULL + i;
        }

        ZeroCompressorParams p{};
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 64;
        p.size_threshold_percentage = 100;
        p.comp_chunks_per_cycle = 8;
        p.comp_extra_latency = Cycles(1);
        p.decomp_chunks_per_cycle = 8;
        p.decomp_extra_latency = Cycles(1);
        p.dictionary_size = 64;
        p.enable_adaptive_bypass = true;
        p.latency_breakeven_threshold = 1.0f;
        p.sampling_interval = 10;
        p.decay_shift = 4;
        p.mem_ctrl = nullptr;
        p.high_pressure_multiplier = 1.5f;
        p.critical_pressure_multiplier = 3.0f;

        zeroComp = std::make_unique<Zero>(p);
        zeroComp->regStats();
    }
};

TEST_F(BaseCompressorProbeTest, InitialStateIsNormal)
{
    EXPECT_EQ(zeroComp->getBackpressureState(), enums::NORMAL);
    EXPECT_FLOAT_EQ(zeroComp->getBackpressureMultiplier(), 1.0f);
    EXPECT_FLOAT_EQ(zeroComp->getEffectiveBreakevenThreshold(), 1.0f);
    EXPECT_FALSE(zeroComp->isInstantaneousBypass());
}

TEST_F(BaseCompressorProbeTest, HighPressureAdjustment)
{
    zeroComp->handleMemoryQueuePressure(enums::HIGH_PRESSURE);

    EXPECT_EQ(zeroComp->getBackpressureState(), enums::HIGH_PRESSURE);
    EXPECT_FLOAT_EQ(zeroComp->getBackpressureMultiplier(), 1.5f);
    EXPECT_FLOAT_EQ(zeroComp->getEffectiveBreakevenThreshold(), 1.5f);
    EXPECT_FALSE(zeroComp->isInstantaneousBypass());
}

TEST_F(BaseCompressorProbeTest, CriticalPressureInstantaneousBypass)
{
    zeroComp->handleMemoryQueuePressure(enums::CRITICAL_PRESSURE);

    EXPECT_EQ(zeroComp->getBackpressureState(), enums::CRITICAL_PRESSURE);
    EXPECT_FLOAT_EQ(zeroComp->getBackpressureMultiplier(), 3.0f);
    EXPECT_FLOAT_EQ(zeroComp->getEffectiveBreakevenThreshold(), 3.0f);
    EXPECT_TRUE(zeroComp->isInstantaneousBypass());

    Cycles comp_lat, decomp_lat;
    auto comp_data = zeroComp->Base::compress(randomLine, comp_lat, decomp_lat);

    // Under instantaneous bypass, compressed size is uncompressed (512 bits) and latencies are 0
    EXPECT_EQ(comp_data->getSizeBits(), 512);
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
}

TEST_F(BaseCompressorProbeTest, RecoveryToNormalState)
{
    zeroComp->handleMemoryQueuePressure(enums::HIGH_PRESSURE);
    EXPECT_FLOAT_EQ(zeroComp->getEffectiveBreakevenThreshold(), 1.5f);

    zeroComp->handleMemoryQueuePressure(enums::NORMAL);
    EXPECT_EQ(zeroComp->getBackpressureState(), enums::NORMAL);
    EXPECT_FLOAT_EQ(zeroComp->getBackpressureMultiplier(), 1.0f);
    EXPECT_FLOAT_EQ(zeroComp->getEffectiveBreakevenThreshold(), 1.0f);
    EXPECT_FALSE(zeroComp->isInstantaneousBypass());
}

TEST_F(BaseCompressorProbeTest, StressTest1000CycleWriteBurstThrottling)
{
    // Simulate 1000 cycles of transient write bursts
    for (int cycle = 0; cycle < 1000; ++cycle) {
        if (cycle % 200 == 0) {
            zeroComp->handleMemoryQueuePressure(enums::HIGH_PRESSURE);
        } else if (cycle % 200 == 50) {
            zeroComp->handleMemoryQueuePressure(enums::CRITICAL_PRESSURE);
        } else if (cycle % 200 == 100) {
            zeroComp->handleMemoryQueuePressure(enums::NORMAL);
        }

        Cycles comp_lat, decomp_lat;
        auto comp_data = zeroComp->Base::compress(zeroLine, comp_lat, decomp_lat);

        if (zeroComp->isInstantaneousBypass()) {
            EXPECT_EQ(comp_data->getSizeBits(), 512);
        } else {
            EXPECT_EQ(comp_data->getSizeBits(), 0);
        }
    }
}
