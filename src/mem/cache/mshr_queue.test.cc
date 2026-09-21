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

#include "mem/cache/mshr_queue.hh"

using namespace gem5;

TEST(MSHRQueueTest, CanPrefetchWithDownstreamAndDecompressionFeedback)
{
    // MSHRQueue label, num_entries, reserve, demand_reserve, cache_name
    MSHRQueue mshrQueue("test_mshr_queue", 16, 0, 1, "test_cache");

    // Initially with 0 allocated MSHRs and 0 occupancy / 0 latency,
    // canPrefetch is true
    EXPECT_TRUE(mshrQueue.canPrefetch());

    // 1. Downstream memory queue occupancy >= high_watermark
    // High watermark threshold set to 8
    EXPECT_TRUE(mshrQueue.canPrefetch(7, 8, Cycles(0), Cycles(0), 0));
    EXPECT_FALSE(mshrQueue.canPrefetch(8, 8, Cycles(0), Cycles(0), 0));
    EXPECT_FALSE(mshrQueue.canPrefetch(10, 8, Cycles(0), Cycles(0), 0));

    // 2. Decompression latency > threshold
    // Threshold set to 10 cycles
    EXPECT_TRUE(mshrQueue.canPrefetch(0, 0, Cycles(10), Cycles(10), 0));
    EXPECT_FALSE(mshrQueue.canPrefetch(0, 0, Cycles(11), Cycles(10), 0));

    // 3. Dynamic extra reserve adjustment
    // numEntries = 16, numReserve = 0, demandReserve = 1, +1 default offset =
    // 2 Max prefetchable allocated entries = 16 - 2 - extra_reserve = 14 -
    // extra_reserve
    EXPECT_TRUE(mshrQueue.canPrefetch(0, 0, Cycles(0), Cycles(0), 4));
}
