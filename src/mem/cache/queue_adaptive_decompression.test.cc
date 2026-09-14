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

#include "mem/cache/base.hh"
#include "mem/cache/mshr_queue.hh"
#include "mem/cache/queue.hh"
#include "mem/cache/write_queue.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
bool
BaseCache::sendWriteQueuePacket(WriteQueueEntry *wq_entry)
{
    return true;
}
} // namespace gem5

using namespace gem5;

TEST(QueueAdaptiveDecompressionTest, QueueOccupancyQuery)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    MSHRQueue mshrQueue("MSHRs", 16, 0, 1, "test_cache");
    EXPECT_EQ(mshrQueue.occupancy(), 0);
    EXPECT_TRUE(mshrQueue.isEmpty());

    WriteQueue writeQueue("write buffer", 8, 16, "test_cache");
    EXPECT_EQ(writeQueue.occupancy(), 0);
    EXPECT_TRUE(writeQueue.isEmpty());
}

TEST(QueueAdaptiveDecompressionTest, CongestionThresholdEvaluation)
{
    unsigned threshold = 12;

    // Uncongested scenario: MSHR = 5, WriteBuffer = 4 (Total = 9 < 12)
    unsigned mshr_occ = 5;
    unsigned wq_occ = 4;
    bool is_congested = (mshr_occ >= threshold) || (wq_occ >= threshold) ||
                         (mshr_occ + wq_occ >= threshold);
    EXPECT_FALSE(is_congested);

    // Congested scenario via total queue occupancy: MSHR = 8, WriteBuffer = 5 (Total = 13 >= 12)
    mshr_occ = 8;
    wq_occ = 5;
    is_congested = (mshr_occ >= threshold) || (wq_occ >= threshold) ||
                   (mshr_occ + wq_occ >= threshold);
    EXPECT_TRUE(is_congested);

    // Congested scenario via individual MSHR queue occupancy: MSHR = 12, WriteBuffer = 0
    mshr_occ = 12;
    wq_occ = 0;
    is_congested = (mshr_occ >= threshold) || (wq_occ >= threshold) ||
                   (mshr_occ + wq_occ >= threshold);
    EXPECT_TRUE(is_congested);
}
