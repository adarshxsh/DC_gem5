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

class TestMemCtrl
{
  public:
    static uint32_t
    calculateDynamicWriteHighThreshold(bool enableAdaptiveTurnaround,
                                       uint32_t writeHighThreshold,
                                       uint32_t writeLowThreshold,
                                       uint32_t writeBufferSize,
                                       double read_pressure,
                                       double write_pressure)
    {
        if (!enableAdaptiveTurnaround) {
            return writeHighThreshold;
        }

        double pressure_diff = write_pressure - read_pressure;
        double high_thresh = writeHighThreshold;

        if (pressure_diff > 0.0) {
            double range = writeHighThreshold - writeLowThreshold;
            high_thresh = writeHighThreshold - (range * pressure_diff);
        } else {
            double range = writeBufferSize - writeHighThreshold;
            high_thresh =
                writeHighThreshold + (range * (-pressure_diff) * 0.8);
        }

        uint32_t min_thresh = std::max((uint32_t)1, writeLowThreshold);
        uint32_t max_thresh =
            writeBufferSize > 0 ? writeBufferSize - 1 : writeHighThreshold;

        uint32_t res = static_cast<uint32_t>(std::round(high_thresh));
        return std::clamp(res, min_thresh, max_thresh);
    }

    static uint32_t
    calculateDynamicWriteLowThreshold(bool enableAdaptiveTurnaround,
                                      uint32_t writeHighThreshold,
                                      uint32_t writeLowThreshold,
                                      uint32_t writeBufferSize,
                                      double read_pressure,
                                      double write_pressure)
    {
        if (!enableAdaptiveTurnaround) {
            return writeLowThreshold;
        }

        double pressure_diff = write_pressure - read_pressure;
        double low_thresh = writeLowThreshold;

        if (pressure_diff > 0.0) {
            double range = writeLowThreshold * 0.5;
            low_thresh = writeLowThreshold - (range * pressure_diff);
        } else {
            double range = writeHighThreshold - writeLowThreshold;
            low_thresh = writeLowThreshold + (range * (-pressure_diff) * 0.5);
        }

        uint32_t min_thresh = 1;
        uint32_t max_thresh =
            writeHighThreshold > 1 ? writeHighThreshold - 1 : 1;

        uint32_t res = static_cast<uint32_t>(std::round(low_thresh));
        return std::clamp(res, min_thresh, max_thresh);
    }

    static uint32_t
    calculateDynamicMinWritesPerSwitch(bool enableAdaptiveTurnaround,
                                       uint32_t minWritesPerSwitch,
                                       double read_pressure,
                                       double write_pressure)
    {
        if (!enableAdaptiveTurnaround) {
            return minWritesPerSwitch;
        }

        double pressure_diff = write_pressure - read_pressure;
        double min_writes = minWritesPerSwitch;

        if (pressure_diff > 0.0) {
            min_writes = minWritesPerSwitch * (1.0 + pressure_diff);
        } else {
            min_writes = minWritesPerSwitch * (1.0 + pressure_diff * 0.5);
        }

        uint32_t res = static_cast<uint32_t>(std::round(min_writes));
        return std::max((uint32_t)1, res);
    }

    static uint32_t
    calculateDynamicMinReadsPerSwitch(bool enableAdaptiveTurnaround,
                                      uint32_t minReadsPerSwitch,
                                      double read_pressure,
                                      double write_pressure)
    {
        if (!enableAdaptiveTurnaround) {
            return minReadsPerSwitch;
        }

        double pressure_diff = read_pressure - write_pressure;
        double min_reads = minReadsPerSwitch;

        if (pressure_diff > 0.0) {
            min_reads = minReadsPerSwitch * (1.0 + pressure_diff);
        } else {
            min_reads = minReadsPerSwitch * (1.0 + pressure_diff * 0.5);
        }

        uint32_t res = static_cast<uint32_t>(std::round(min_reads));
        return std::max((uint32_t)1, res);
    }
};

TEST(MemCtrlAdaptiveTest, BaselineDisabledMode)
{
    uint32_t writeBufferSize = 32;
    uint32_t writeHighThreshold = 27; // 85%
    uint32_t writeLowThreshold = 16;  // 50%
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    // Disabled mode: enableAdaptiveTurnaround = false
    uint32_t effHigh = TestMemCtrl::calculateDynamicWriteHighThreshold(
        false, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.8,
        0.2);
    uint32_t effLow = TestMemCtrl::calculateDynamicWriteLowThreshold(
        false, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.8,
        0.2);
    uint32_t effWrites = TestMemCtrl::calculateDynamicMinWritesPerSwitch(
        false, minWritesPerSwitch, 0.8, 0.2);
    uint32_t effReads = TestMemCtrl::calculateDynamicMinReadsPerSwitch(
        false, minReadsPerSwitch, 0.8, 0.2);

    EXPECT_EQ(effHigh, writeHighThreshold);
    EXPECT_EQ(effLow, writeLowThreshold);
    EXPECT_EQ(effWrites, minWritesPerSwitch);
    EXPECT_EQ(effReads, minReadsPerSwitch);
}

TEST(MemCtrlAdaptiveTest, WritePressureBurst)
{
    uint32_t writeBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    // High write pressure (0.9), low read pressure (0.1)
    uint32_t effHigh = TestMemCtrl::calculateDynamicWriteHighThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.1,
        0.9);
    uint32_t effLow = TestMemCtrl::calculateDynamicWriteLowThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.1,
        0.9);
    uint32_t effWrites = TestMemCtrl::calculateDynamicMinWritesPerSwitch(
        true, minWritesPerSwitch, 0.1, 0.9);
    uint32_t effReads = TestMemCtrl::calculateDynamicMinReadsPerSwitch(
        true, minReadsPerSwitch, 0.1, 0.9);

    // Dynamic write high threshold should drop to trigger write draining
    // earlier
    EXPECT_LT(effHigh, writeHighThreshold);
    EXPECT_GE(effHigh, writeLowThreshold);

    // Min writes per switch should increase
    EXPECT_GT(effWrites, minWritesPerSwitch);

    // Min reads per switch should decrease
    EXPECT_LT(effReads, minReadsPerSwitch);
}

TEST(MemCtrlAdaptiveTest, ReadPressureBacklog)
{
    uint32_t writeBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;
    uint32_t minReadsPerSwitch = 16;
    uint32_t minWritesPerSwitch = 16;

    // High read pressure (0.9), low write pressure (0.1)
    uint32_t effHigh = TestMemCtrl::calculateDynamicWriteHighThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.9,
        0.1);
    uint32_t effLow = TestMemCtrl::calculateDynamicWriteLowThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.9,
        0.1);
    uint32_t effWrites = TestMemCtrl::calculateDynamicMinWritesPerSwitch(
        true, minWritesPerSwitch, 0.9, 0.1);
    uint32_t effReads = TestMemCtrl::calculateDynamicMinReadsPerSwitch(
        true, minReadsPerSwitch, 0.9, 0.1);

    // Dynamic write high threshold should increase to hold writes and
    // prioritize reads
    EXPECT_GT(effHigh, writeHighThreshold);
    EXPECT_LE(effHigh, writeBufferSize - 1);

    // Min reads per switch should increase
    EXPECT_GT(effReads, minReadsPerSwitch);

    // Min writes per switch should decrease
    EXPECT_LT(effWrites, minWritesPerSwitch);
}

TEST(MemCtrlAdaptiveTest, PhysicalBoundsAndInvariants)
{
    uint32_t writeBufferSize = 32;
    uint32_t writeHighThreshold = 27;
    uint32_t writeLowThreshold = 16;

    // Extreme write pressure (1.0 write, 0.0 read)
    uint32_t effHigh = TestMemCtrl::calculateDynamicWriteHighThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.0,
        1.0);
    uint32_t effLow = TestMemCtrl::calculateDynamicWriteLowThreshold(
        true, writeHighThreshold, writeLowThreshold, writeBufferSize, 0.0,
        1.0);

    EXPECT_GE(effHigh, writeLowThreshold);
    EXPECT_LE(effHigh, writeBufferSize - 1);
    EXPECT_GE(effLow, 1U);
    EXPECT_LE(effLow, writeHighThreshold - 1);
}

} // namespace memory
} // namespace gem5
