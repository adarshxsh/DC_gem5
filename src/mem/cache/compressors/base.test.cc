/*
 * Copyright (c) 2026 gem5
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

#include <memory>

#include "mem/cache/queue.hh"
#include "mem/packet.hh"

namespace gem5
{

TEST(PacketTest, CompressionBackpressureFlags)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressionBackpressure());

    pkt.setCompressionBackpressure();
    EXPECT_TRUE(pkt.isCompressionBackpressure());

    pkt.clearCompressionBackpressure();
    EXPECT_FALSE(pkt.isCompressionBackpressure());
}

class TestEntry : public QueueEntry
{
  public:
    TestEntry() : QueueEntry() {}
    bool
    matchBlockAddr(Addr b_addr, bool is_sec) const override
    {
        return false;
    }
    bool
    matchBlockAddr(const PacketPtr p) const override
    {
        return false;
    }
    bool
    trySatisfyFunctional(PacketPtr p) override
    {
        return false;
    }
    bool
    sendPacket(BaseCache &c) override
    {
        return true;
    }
};

TEST(QueueTest, OccupancyRatioAndCapacity)
{
    Queue<TestEntry> queue("test_queue", 10, 0, "test");

    EXPECT_EQ(queue.capacity(), 10);
    EXPECT_EQ(queue.numAllocated(), 0);
    EXPECT_DOUBLE_EQ(queue.getOccupancyRatio(), 0.0);
}

} // namespace gem5
