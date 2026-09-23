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

TEST(MSHRQueueTest, DynamicPrefetchGatingDefaultBehavior)
{
    // num_entries = 12, reserve = 1, demand_reserve = 1
    // base_limit = 12 - (1 + 1 + 1) = 9
    MSHRQueue queue("mshr_queue", 12, 1, 1, "test_cache");

    EXPECT_TRUE(queue.canPrefetch());
    EXPECT_TRUE(queue.canPrefetch(0, false, Cycles(0)));
}

TEST(MSHRQueueTest, MemorySideRetryBlocksPrefetch)
{
    MSHRQueue queue("mshr_queue", 12, 1, 1, "test_cache");

    // Memory side retry state must immediately block prefetch issuance
    EXPECT_FALSE(queue.canPrefetch(0, true, Cycles(0)));
}

TEST(MSHRQueueTest, WriteBufferOccupancyThrottling)
{
    MSHRQueue queue("mshr_queue", 12, 1, 1, "test_cache");

    // With zero MSHRs allocated, default threshold is 9
    EXPECT_TRUE(queue.canPrefetch(0, false, Cycles(0)));

    // High write buffer occupancy reduces prefetch eligibility headroom
    // write_buf_alloc = 16 => wb_penalty = 8 or 9 => effective_limit drops
    EXPECT_FALSE(queue.canPrefetch(16, false, Cycles(0)));
}

TEST(MSHRQueueTest, DecompressionLatencyThrottling)
{
    MSHRQueue queue("mshr_queue", 12, 1, 1, "test_cache");

    // High decompression latency reduces prefetch eligibility threshold
    EXPECT_TRUE(queue.canPrefetch(0, false, Cycles(0)));
    EXPECT_FALSE(queue.canPrefetch(0, false, Cycles(10)));
}
