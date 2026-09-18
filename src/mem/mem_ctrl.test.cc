/*
 * Copyright (c) 2026 gem5 Project
 * All rights reserved.
 */

#include <cstdint>
#include <gtest/gtest.h>

namespace gem5
{

namespace memory
{

// Unit test for predictive linear lead time velocity thresholding calculations
class MemCtrlVelocityThresholdTest : public ::testing::Test
{
  protected:
    uint64_t leadTimeHorizon = 20000; // 20ns = 20000 ps
    uint32_t writeHighThreshold = 32;

    uint32_t
    calcEffectiveWriteQueueSize(uint32_t current_q, uint64_t curTick,
                                uint64_t lastWriteArrivalTick,
                                uint32_t lastWriteQueueSize,
                                uint64_t prevWriteArrivalTick,
                                uint32_t prevWriteQueueSize) const
    {
        uint64_t dt = 0;
        int64_t dq = 0;

        if (curTick > lastWriteArrivalTick) {
            if (lastWriteArrivalTick > 0) {
                dt = curTick - lastWriteArrivalTick;
                dq = (int64_t)current_q - (int64_t)lastWriteQueueSize;
            }
        } else if (curTick == lastWriteArrivalTick) {
            if (prevWriteArrivalTick > 0 && curTick > prevWriteArrivalTick) {
                dt = curTick - prevWriteArrivalTick;
                dq = (int64_t)current_q - (int64_t)prevWriteQueueSize;
            }
        }

        if (dt > 0 && dq > 0) {
            uint64_t v_tau = ((uint64_t)dq * leadTimeHorizon) / dt;
            return current_q + (uint32_t)v_tau;
        }

        return current_q;
    }
};

TEST_F(MemCtrlVelocityThresholdTest, SteadyStateLowVelocity)
{
    // Steady state: 1 write every 50000 ps (very slow rate)
    // Q_current = 5, last_q = 4, dt = 50000 ps
    // v_tau = (1 * 20000) / 50000 = 0
    uint32_t Q_eff = calcEffectiveWriteQueueSize(5, 100000, 50000, 4, 0, 0);
    EXPECT_EQ(Q_eff, 5);
    EXPECT_LT(Q_eff, writeHighThreshold);
}

TEST_F(MemCtrlVelocityThresholdTest, HighVelocityWritebackBurst)
{
    // High velocity burst: 4 writes arrive within 1000 ps
    // Q_current = 10, last_q = 6, dt = 1000 ps
    // v_tau = (4 * 20000) / 1000 = 80
    // Q_eff = 10 + 80 = 90
    uint32_t Q_eff = calcEffectiveWriteQueueSize(10, 51000, 50000, 6, 0, 0);
    EXPECT_EQ(Q_eff, 90);
    EXPECT_GE(Q_eff, writeHighThreshold);
}

TEST_F(MemCtrlVelocityThresholdTest, SameTickArrivalsHandling)
{
    // Multiple writes arriving in the same tick (curTick ==
    // lastWriteArrivalTick = 51000) prevWriteArrivalTick = 50000,
    // prevWriteQueueSize = 6, Q_current = 12 dt = 51000 - 50000 = 1000 ps dq =
    // 12 - 6 = 6 v_tau = (6 * 20000) / 1000 = 120 Q_eff = 12 + 120 = 132
    uint32_t Q_eff =
        calcEffectiveWriteQueueSize(12, 51000, 51000, 10, 50000, 6);
    EXPECT_EQ(Q_eff, 132);
    EXPECT_GE(Q_eff, writeHighThreshold);
}

TEST_F(MemCtrlVelocityThresholdTest, QueueDrainNegativeDeltaClamping)
{
    // Queue draining: Q_current = 4, last_q = 12, dt = 2000 ps
    // dq = 4 - 12 = -8 <= 0
    // Velocity must be clamped to 0, Q_eff = Q_current = 4
    uint32_t Q_eff = calcEffectiveWriteQueueSize(4, 52000, 50000, 12, 0, 0);
    EXPECT_EQ(Q_eff, 4);
    EXPECT_LT(Q_eff, writeHighThreshold);
}

TEST_F(MemCtrlVelocityThresholdTest, ZeroDeltaTickSafety)
{
    // First write arrival (lastWriteArrivalTick = 0, prevWriteArrivalTick = 0)
    // dt = 0, should safely return current_q without divide-by-zero
    uint32_t Q_eff = calcEffectiveWriteQueueSize(1, 1000, 0, 0, 0, 0);
    EXPECT_EQ(Q_eff, 1);
}

} // namespace memory
} // namespace gem5
