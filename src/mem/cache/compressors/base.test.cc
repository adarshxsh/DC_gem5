/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include "mem/cache/compressors/zero.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class BaseCacheCompressorQueuePressureTest : public ::testing::Test
{
  protected:
    uint64_t zeroLine[8];

    void
    SetUp() override
    {
        std::memset(zeroLine, 0, sizeof(zeroLine));
    }

    Base *
    createZeroCompressor(bool enable_throttling, int threshold)
    {
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
        p.enable_adaptive_bypass = false;
        p.latency_breakeven_threshold = 1.0;
        p.sampling_interval = 100;
        p.decay_shift = 4;
        p.enable_queue_pressure_throttling = enable_throttling;
        p.queue_pressure_threshold = threshold;

        Zero *comp = new Zero(p);
        comp->regStats();
        return comp;
    }
};

/**
 * Test default behavior when dynamic queue pressure throttling is disabled.
 */
TEST_F(BaseCacheCompressorQueuePressureTest, DisabledThrottling)
{
    std::unique_ptr<Base> comp(createZeroCompressor(false, 80));
    comp->setQueuePressure(95.0);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    auto comp_data = comp->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // Dynamic throttling disabled -> compression proceeds normally
    EXPECT_EQ(comp_data->getSizeBits(), 0);
    EXPECT_GT((uint64_t)comp_lat, 0);
}

/**
 * Test behavior when queue pressure is below threshold.
 */
TEST_F(BaseCacheCompressorQueuePressureTest, PressureBelowThreshold)
{
    std::unique_ptr<Base> comp(createZeroCompressor(true, 80));
    comp->setQueuePressure(50.0);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    auto comp_data = comp->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // 50% pressure < 80% threshold -> normal compression
    EXPECT_EQ(comp_data->getSizeBits(), 0);
    EXPECT_GT((uint64_t)comp_lat, 0);
}

/**
 * Test compression bypass when queue pressure exceeds threshold.
 */
TEST_F(BaseCacheCompressorQueuePressureTest, PressureExceedsThreshold)
{
    std::unique_ptr<Base> comp(createZeroCompressor(true, 80));
    comp->setQueuePressure(85.0);

    Cycles comp_lat(10);
    Cycles decomp_lat(10);

    auto comp_data = comp->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // 85% pressure >= 80% threshold -> compression bypassed
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(),
              64 * 8); // Uncompressed size (512 bits)
}

/**
 * Test dynamic pressure transitions.
 */
TEST_F(BaseCacheCompressorQueuePressureTest, DynamicPressureTransition)
{
    std::unique_ptr<Base> comp(createZeroCompressor(true, 80));

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // High pressure -> bypass
    comp->setQueuePressure(90.0);
    auto comp_data1 = comp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data1->getSizeBits(), 512);

    // Low pressure -> normal compression
    comp->setQueuePressure(40.0);
    auto comp_data2 = comp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_GT((uint64_t)comp_lat, 0);
    EXPECT_EQ(comp_data2->getSizeBits(), 0);

    // High pressure again -> bypass
    comp->setQueuePressure(80.0);
    auto comp_data3 = comp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data3->getSizeBits(), 512);
}
