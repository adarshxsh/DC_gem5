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

#include <cstdint>

namespace gem5
{
namespace memory
{

class TestPreemptionEvaluator
{
  public:
    static bool
    evalPreemption(bool enablePreemption, uint32_t readQueueSize,
                   uint64_t oldestReadLatency, uint64_t latencyThreshold,
                   double readOccupancy, double pressureThreshold)
    {
        if (readQueueSize == 0) {
            return false;
        }
        if (!enablePreemption) {
            return false;
        }

        if (oldestReadLatency >= latencyThreshold ||
            readOccupancy >= pressureThreshold) {
            return true;
        }

        return false;
    }
};

TEST(MemCtrlPreemptionTest, PreemptionDisabled)
{
    // When enablePreemption is false, shouldPreempt returns false
    bool result = TestPreemptionEvaluator::evalPreemption(false, 5, 100000,
                                                          50000, 0.9, 0.8);
    EXPECT_FALSE(result);
}

TEST(MemCtrlPreemptionTest, ReadQueueEmpty)
{
    // Empty read queue should not preempt
    bool result = TestPreemptionEvaluator::evalPreemption(true, 0, 100000,
                                                          50000, 0.9, 0.8);
    EXPECT_FALSE(result);
}

TEST(MemCtrlPreemptionTest, LatencyExceedsThreshold)
{
    // Read queue latency > threshold triggers preemption
    bool result = TestPreemptionEvaluator::evalPreemption(true, 2, 60000,
                                                          50000, 0.2, 0.8);
    EXPECT_TRUE(result);
}

TEST(MemCtrlPreemptionTest, OccupancyExceedsThreshold)
{
    // Read queue occupancy > threshold triggers preemption
    bool result = TestPreemptionEvaluator::evalPreemption(true, 8, 10000,
                                                          50000, 0.85, 0.8);
    EXPECT_TRUE(result);
}

TEST(MemCtrlPreemptionTest, BelowThresholds)
{
    // Both latency and occupancy below threshold -> no preemption
    bool result = TestPreemptionEvaluator::evalPreemption(true, 2, 20000,
                                                          50000, 0.3, 0.8);
    EXPECT_FALSE(result);
}

} // namespace memory
} // namespace gem5
