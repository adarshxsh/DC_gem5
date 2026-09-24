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
#include <list>
#include <string>

#include "mem/cache/queue.hh"
#include "mem/cache/queue_entry.hh"

namespace gem5
{

class TestQueueEntry : public QueueEntry
{
  public:
    typedef std::list<TestQueueEntry *> List;
    typedef List::iterator Iterator;

    Iterator readyIter;
    Iterator allocIter;

    TestQueueEntry(const std::string &name = "test") : QueueEntry(name) {}

    bool
    sendPacket(BaseCache &cache) override
    {
        return true;
    }
    bool
    matchBlockAddr(Addr addr, bool is_secure) const override
    {
        return false;
    }
    bool
    matchBlockAddr(const PacketPtr pkt) const override
    {
        return false;
    }
    bool
    conflictAddr(const QueueEntry *entry) const override
    {
        return false;
    }
    Target *
    getTarget() override
    {
        return nullptr;
    }
    void
    deallocate()
    {}
};

TEST(QueueTest, OccupancyAndCapacity)
{
    // Queue with 10 entries and 2 reserved overflow entries
    Queue<TestQueueEntry> queue("test_queue", 10, 2, "test");

    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_EQ(queue.capacity(), 10);
    EXPECT_DOUBLE_EQ(queue.occupancyRatio(), 0.0);
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
}

TEST(QueueTest, ThresholdComparison)
{
    Queue<TestQueueEntry> queue("test_queue", 10, 0, "test");
    double threshold = 0.8;

    // Simulate occupancy and check ratio relative to 80% threshold
    EXPECT_FALSE(queue.occupancyRatio() > threshold);
}

} // namespace gem5
