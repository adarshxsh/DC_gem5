/*
 * Copyright (c) 2024 gem5 Project
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
#include <cstdint>
#include <iostream>

namespace gem5
{
namespace memory
{

class TestMemCtrl
{
  public:
    static void calculateDynamicThresholds(
        bool adaptiveQueuePressure, int pressureSensitivity,
        uint32_t writeBufferSize, uint32_t readBufferSize,
        uint32_t writeHighThreshold, uint32_t writeLowThreshold,
        uint32_t minReadsPerSwitch, uint32_t minWritesPerSwitch,
        uint32_t writeQueueSize, uint32_t readQueueSize,
        uint32_t &effectiveWriteHighThreshold,
        uint32_t &effectiveMinReadsPerSwitch,
        uint32_t &effectiveMinWritesPerSwitch)
    {
        effectiveWriteHighThreshold = writeHighThreshold;
        effectiveMinReadsPerSwitch = minReadsPerSwitch;
        effectiveMinWritesPerSwitch = minWritesPerSwitch;

        if (adaptiveQueuePressure && writeBufferSize > 0) {
            uint32_t writeUtilPct = (writeQueueSize * 100) / writeBufferSize;
            uint32_t readUtilPct = (readBufferSize > 0) ?
                (readQueueSize * 100) / readBufferSize : 0;

            uint32_t sensitivity = (pressureSensitivity > 0) ?
                static_cast<uint32_t>(pressureSensitivity) : 50;

            if (writeUtilPct > sensitivity) {
                uint32_t pressureDelta = writeUtilPct - sensitivity;
                uint32_t maxPressureRange = (100 > sensitivity) ?
                    (100 - sensitivity) : 1;

                uint32_t minAllowedHigh = writeLowThreshold + 1;
                if (writeHighThreshold > minAllowedHigh) {
                    uint32_t maxReduction = writeHighThreshold - minAllowedHigh;
                    uint32_t reduction = (maxReduction * pressureDelta) /
                        maxPressureRange;
                    effectiveWriteHighThreshold = writeHighThreshold - reduction;
                }

                uint32_t writeAllocIncrease = (minWritesPerSwitch * pressureDelta) /
                    maxPressureRange;
                effectiveMinWritesPerSwitch = minWritesPerSwitch +
                    writeAllocIncrease;

                if (readUtilPct < writeUtilPct) {
                    uint32_t readReduction = (minReadsPerSwitch * pressureDelta) /
                        (maxPressureRange * 2);
                    if (minReadsPerSwitch > readReduction) {
                        effectiveMinReadsPerSwitch = std::max(
                            1U, minReadsPerSwitch - readReduction);
                    } else {
                        effectiveMinReadsPerSwitch = 1;
                    }
                }
            }
            effectiveWriteHighThreshold = std::clamp(
                effectiveWriteHighThreshold, writeLowThreshold + 1,
                writeBufferSize);
        }
    }
};

TEST(MemCtrlAdaptiveTest, BaselineDisabledMode)
{
    uint32_t writeBufferSize = 32;
    uint32_t readBufferSize = 32;
    uint32_t writeHighThreshold = 27; // 85%
    uint32_t writeLowThreshold = 16;  // 50%
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    uint32_t effHigh, effMinReads, effMinWrites;

    // Test with adaptiveQueuePressure = false and high write queue utilization (28/32)
    TestMemCtrl::calculateDynamicThresholds(
        false, 50, writeBufferSize, readBufferSize,
        writeHighThreshold, writeLowThreshold,
        minReadsPerSwitch, minWritesPerSwitch,
        28, 5, effHigh, effMinReads, effMinWrites);

    EXPECT_EQ(effHigh, writeHighThreshold);
    EXPECT_EQ(effMinReads, minReadsPerSwitch);
    EXPECT_EQ(effMinWrites, minWritesPerSwitch);
}

TEST(MemCtrlAdaptiveTest, AdaptiveLowPressureMode)
{
    uint32_t writeBufferSize = 32;
    uint32_t readBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    uint32_t effHigh, effMinReads, effMinWrites;

    // Test with adaptiveQueuePressure = true, pressureSensitivity = 50, write size 12/32 (37.5% <= 50%)
    TestMemCtrl::calculateDynamicThresholds(
        true, 50, writeBufferSize, readBufferSize,
        writeHighThreshold, writeLowThreshold,
        minReadsPerSwitch, minWritesPerSwitch,
        12, 10, effHigh, effMinReads, effMinWrites);

    EXPECT_EQ(effHigh, writeHighThreshold);
    EXPECT_EQ(effMinReads, minReadsPerSwitch);
    EXPECT_EQ(effMinWrites, minWritesPerSwitch);
}

TEST(MemCtrlAdaptiveTest, AdaptiveHighPressureMode)
{
    uint32_t writeBufferSize = 32;
    uint32_t readBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    uint32_t effHigh, effMinReads, effMinWrites;

    // Test with adaptiveQueuePressure = true, pressureSensitivity = 50, write size 27/32 (84% > 50%)
    TestMemCtrl::calculateDynamicThresholds(
        true, 50, writeBufferSize, readBufferSize,
        writeHighThreshold, writeLowThreshold,
        minReadsPerSwitch, minWritesPerSwitch,
        27, 4, effHigh, effMinReads, effMinWrites);

    // High threshold should scale down under pressure
    EXPECT_LT(effHigh, writeHighThreshold);
    EXPECT_GT(effHigh, writeLowThreshold);

    // Min writes per switch should increase
    EXPECT_GT(effMinWrites, minWritesPerSwitch);

    // Min reads per switch should decrease
    EXPECT_LT(effMinReads, minReadsPerSwitch);
    EXPECT_GE(effMinReads, 1U);

    // Verify physical invariant: writeLowThreshold < effectiveWriteHighThreshold <= writeBufferSize
    EXPECT_GT(effHigh, writeLowThreshold);
    EXPECT_LE(effHigh, writeBufferSize);
}

TEST(MemCtrlAdaptiveTest, ExtremePressureStallPrevention)
{
    uint32_t writeBufferSize = 32;
    uint32_t readBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    uint32_t effHigh, effMinReads, effMinWrites;

    // Test extreme write queue pressure (32/32 = 100%)
    TestMemCtrl::calculateDynamicThresholds(
        true, 50, writeBufferSize, readBufferSize,
        writeHighThreshold, writeLowThreshold,
        minReadsPerSwitch, minWritesPerSwitch,
        32, 2, effHigh, effMinReads, effMinWrites);

    // Under extreme pressure, high threshold drops to writeLowThreshold + 1
    EXPECT_EQ(effHigh, writeLowThreshold + 1);

    // Write burst allocation expands significantly to flush write queue
    EXPECT_GE(effMinWrites, minWritesPerSwitch + 14);

    // Physical invariant check
    EXPECT_GT(effHigh, writeLowThreshold);
    EXPECT_LE(effHigh, writeBufferSize);
}

} // namespace memory
} // namespace gem5
