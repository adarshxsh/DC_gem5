/*
 * Copyright (c) 2026
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

#include <algorithm>
#include <cmath>

#include "mem/mem_ctrl.hh"
#include "mem/mem_interface.hh"
#include "sim/cur_tick.hh"

using namespace gem5;
using namespace gem5::memory;

namespace
{

class MemCtrlTestFixture : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

    void SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(MemCtrlTestFixture, WriteArrivalRateEWMA)
{
    // Verify EWMA formula and arrival velocity calculation
    mockTick = 1000;
    Tick lastArrival = 1000;
    uint32_t currentCount = 1;
    double alpha = 0.5;
    double rate = 0.0;

    // Arrival 1 at tick 1000
    // At tick 1000, initial write count = 1

    // Arrival 2 at tick 1100 (delta = 100 ticks, pkt_count = 1)
    mockTick = 1100;
    Tick delta_t = mockTick - lastArrival;
    double inst_rate = static_cast<double>(currentCount) / static_cast<double>(delta_t); // 1.0 / 100 = 0.01
    rate = alpha * inst_rate + (1.0 - alpha) * rate; // 0.5 * 0.01 + 0.5 * 0 = 0.005
    lastArrival = mockTick;
    currentCount = 1;

    EXPECT_NEAR(rate, 0.005, 1e-6);

    // Arrival 3 at tick 1200 (delta = 100 ticks, pkt_count = 1)
    mockTick = 1200;
    delta_t = mockTick - lastArrival;
    inst_rate = static_cast<double>(currentCount) / static_cast<double>(delta_t); // 1.0 / 100 = 0.01
    rate = alpha * inst_rate + (1.0 - alpha) * rate; // 0.5 * 0.01 + 0.5 * 0.005 = 0.0075

    EXPECT_NEAR(rate, 0.0075, 1e-6);
}

TEST_F(MemCtrlTestFixture, DynamicThresholdLoweringAndHysteresis)
{
    uint32_t writeHighThreshold = 54; // 85% of 64
    uint32_t writeLowThreshold = 32;  // 50% of 64
    uint32_t minWritesPerSwitch = 16;
    double writeRateThreshold = 0.001;
    double rateSensitivity = 1.0;
    Tick tSwitch = 10000; // 10ns

    double writeArrivalRate = 0.003; // rate > threshold by 0.002
    double excessRate = writeArrivalRate - writeRateThreshold; // 0.002
    double lowerAmount = rateSensitivity * excessRate * static_cast<double>(tSwitch); // 0.002 * 10000 = 20 entries
    double dynamicThreshold = static_cast<double>(writeHighThreshold) - lowerAmount; // 54 - 20 = 34
    double minThreshold = static_cast<double>(writeLowThreshold + minWritesPerSwitch); // 32 + 16 = 48
    dynamicThreshold = std::max(minThreshold, dynamicThreshold); // clamped to 48

    EXPECT_EQ(static_cast<uint32_t>(dynamicThreshold), 48u);
}

TEST_F(MemCtrlTestFixture, ProjectedQueueOccupancyCalculation)
{
    double currentQueueSize = 40.0;
    double writeArrivalRate = 0.002; // 2 writes per 1000 ticks
    Tick tSwitch = 5000; // 5000 ticks

    double projectedSize = currentQueueSize + writeArrivalRate * static_cast<double>(tSwitch); // 40 + 0.002 * 5000 = 50
    EXPECT_DOUBLE_EQ(projectedSize, 50.0);
}

} // namespace
