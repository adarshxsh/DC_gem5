/*
 * Copyright (c) 2026 gem5 Project
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
#include <cstdint>

namespace gem5
{
namespace memory
{

class TestMemCtrlArrivalRate
{
  public:
    static double
    calculateInstantArrivalRate(uint32_t currentQueueSize,
                                uint32_t lastQueueSize, uint64_t currentTick,
                                uint64_t lastArrivalTick)
    {
        if (currentTick <= lastArrivalTick) {
            return 0.0;
        }
        uint64_t delta_t = currentTick - lastArrivalTick;
        int32_t delta_q = static_cast<int32_t>(currentQueueSize) -
                          static_cast<int32_t>(lastQueueSize);
        return static_cast<double>(delta_q) / static_cast<double>(delta_t);
    }

    static double
    updateEMAArrivalRate(double currentAlpha, double instantRate,
                         double prevArrivalRate)
    {
        return currentAlpha * instantRate +
               (1.0 - currentAlpha) * prevArrivalRate;
    }

    static double
    calculateEffectiveQueueOccupancy(bool enableArrivalRateTracking,
                                     uint32_t currentQueueSize,
                                     double arrivalRate,
                                     uint64_t leadTimeHorizon,
                                     uint32_t queueBufferSize)
    {
        if (!enableArrivalRateTracking) {
            return static_cast<double>(currentQueueSize);
        }
        double proj_q = static_cast<double>(currentQueueSize) +
                        arrivalRate * static_cast<double>(leadTimeHorizon);
        return std::clamp(proj_q, 0.0, static_cast<double>(queueBufferSize));
    }

    static double
    calculateQueuePressure(double effectiveOccupancy, uint32_t queueBufferSize)
    {
        if (queueBufferSize == 0) {
            return 0.0;
        }
        return effectiveOccupancy / static_cast<double>(queueBufferSize);
    }

    static bool
    evaluatePredictiveTurnaround(bool enablePredictiveTurnaround,
                                 double effectiveOccupancy,
                                 uint32_t writeHighThreshold,
                                 uint32_t currentQueueSize)
    {
        double evaluated_q = enablePredictiveTurnaround
                                 ? effectiveOccupancy
                                 : static_cast<double>(currentQueueSize);
        return evaluated_q >= static_cast<double>(writeHighThreshold);
    }
};

TEST(MemCtrlArrivalRateTest, SteadyArrivalRateCalculation)
{
    uint32_t writeBufferSize = 32;
    uint64_t leadTimeHorizon = 5000;
    double alpha = 0.5;

    uint32_t q0 = 0;
    uint64_t t0 = 1000;

    // First arrival at t0 = 1000, queue size 2
    uint32_t q1 = 2;
    uint64_t t1 = 2000;
    double rate1 =
        TestMemCtrlArrivalRate::calculateInstantArrivalRate(q1, q0, t1, t0);
    double ema1 =
        TestMemCtrlArrivalRate::updateEMAArrivalRate(alpha, rate1, 0.0);

    EXPECT_DOUBLE_EQ(rate1, 0.002); // 2 requests / 1000 ticks = 0.002
    EXPECT_DOUBLE_EQ(ema1, 0.001);  // 0.5 * 0.002 = 0.001

    // Second arrival at t2 = 3000, queue size 4
    uint32_t q2 = 4;
    uint64_t t2 = 3000;
    double rate2 =
        TestMemCtrlArrivalRate::calculateInstantArrivalRate(q2, q1, t2, t1);
    double ema2 =
        TestMemCtrlArrivalRate::updateEMAArrivalRate(alpha, rate2, ema1);

    EXPECT_DOUBLE_EQ(rate2, 0.002);
    EXPECT_DOUBLE_EQ(ema2, 0.0015);

    // Calculate effective queue occupancy Q_eff = 4 + 0.0015 * 5000 = 11.5
    double eff_q = TestMemCtrlArrivalRate::calculateEffectiveQueueOccupancy(
        true, q2, ema2, leadTimeHorizon, writeBufferSize);
    EXPECT_DOUBLE_EQ(eff_q, 11.5);
}

TEST(MemCtrlArrivalRateTest, BurstyWritebackSurgePredictiveTurnaround)
{
    uint32_t writeBufferSize = 32;
    uint32_t writeHighThreshold = 27; // 85% of 32 = 27.2 -> 27
    uint64_t leadTimeHorizon = 5000;
    double alpha = 1.0; // instant tracking for test

    // Rapid writeback burst: queue size jumps from 10 to 20 in 1000 ticks
    uint32_t q0 = 10;
    uint64_t t0 = 10000;
    uint32_t q1 = 20;
    uint64_t t1 = 11000;

    double rate =
        TestMemCtrlArrivalRate::calculateInstantArrivalRate(q1, q0, t1, t0);
    double ema =
        TestMemCtrlArrivalRate::updateEMAArrivalRate(alpha, rate, 0.0);

    EXPECT_DOUBLE_EQ(rate, 0.01); // 10 reqs / 1000 ticks = 0.01 reqs/tick

    // Effective occupancy Q_eff = 20 + 0.01 * 5000 = 70, clamped to
    // writeBufferSize (32)
    double eff_q = TestMemCtrlArrivalRate::calculateEffectiveQueueOccupancy(
        true, q1, ema, leadTimeHorizon, writeBufferSize);

    EXPECT_DOUBLE_EQ(eff_q, 32.0);

    // Predictive turnaround evaluates eff_q (32) >= writeHighThreshold (27) ->
    // true!
    bool trigger_turnaround =
        TestMemCtrlArrivalRate::evaluatePredictiveTurnaround(
            true, eff_q, writeHighThreshold, q1);
    EXPECT_TRUE(trigger_turnaround);

    // Without predictive turnaround, physical queue size 20 < 27 -> false
    bool legacy_turnaround =
        TestMemCtrlArrivalRate::evaluatePredictiveTurnaround(
            false, eff_q, writeHighThreshold, q1);
    EXPECT_FALSE(legacy_turnaround);
}

TEST(MemCtrlArrivalRateTest, PressureCalculationAndClamping)
{
    uint32_t writeBufferSize = 32;

    double eff_q = 27.2;
    double pressure =
        TestMemCtrlArrivalRate::calculateQueuePressure(eff_q, writeBufferSize);

    EXPECT_DOUBLE_EQ(pressure, 0.85);

    // Verify clamping at upper bound
    double over_q = TestMemCtrlArrivalRate::calculateEffectiveQueueOccupancy(
        true, 30, 0.05, 5000, writeBufferSize);
    EXPECT_DOUBLE_EQ(over_q, 32.0);

    // Verify clamping at lower bound when queue is draining
    double draining_q =
        TestMemCtrlArrivalRate::calculateEffectiveQueueOccupancy(
            true, 2, -0.01, 5000, writeBufferSize);
    EXPECT_DOUBLE_EQ(draining_q, 0.0);
}

TEST(MemCtrlArrivalRateTest, DisabledArrivalRateTrackingFallback)
{
    uint32_t writeBufferSize = 32;
    uint32_t currentQueue = 15;
    double arrivalRate = 0.005;
    uint64_t leadTime = 5000;

    double eff_q = TestMemCtrlArrivalRate::calculateEffectiveQueueOccupancy(
        false, currentQueue, arrivalRate, leadTime, writeBufferSize);

    EXPECT_DOUBLE_EQ(eff_q, 15.0);
}

} // namespace memory
} // namespace gem5
