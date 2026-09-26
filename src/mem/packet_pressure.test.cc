/*
 * Copyright (c) 2024 ARM Limited
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

#include "base/gtest/cur_tick_fake.hh"
#include "mem/packet_queue.hh"
#include "mem/request.hh"

namespace gem5
{

class TestPacketQueue : public PacketQueue
{
  public:
    TestPacketQueue(EventManager& _em, const std::string& _label)
        : PacketQueue(_em, _label, _label + "-sendEvent")
    {}

    const std::string name() const override { return "TestPacketQueue"; }

    bool sendTiming(PacketPtr pkt) override
    {
        return true;
    }
};

class PacketPressureTest : public ::testing::Test
{
  protected:
    GTestTickHandler tickHandler;
    EventQueue eq;
    EventManager em;

    PacketPressureTest() : eq("EQ"), em(&eq)
    {
        tickHandler.setCurTick(0);
    }
};

TEST_F(PacketPressureTest, PacketQueuePressureBypass)
{
    TestPacketQueue queue(em, "test_queue");

    // Initially queue is empty
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.isQueuePressureDecompressBypassActive());

    queue.setPressureThreshold(2);
    EXPECT_FALSE(queue.isQueuePressureDecompressBypassActive());

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr pkt1 = new Packet(req, MemCmd::ReadReq);
    PacketPtr pkt2 = new Packet(req, MemCmd::ReadReq);

    queue.schedSendTiming(pkt1, 10);
    EXPECT_EQ(queue.size(), 1);
    EXPECT_FALSE(queue.isQueuePressureDecompressBypassActive());

    queue.schedSendTiming(pkt2, 20);
    EXPECT_EQ(queue.size(), 2);
    // Meets threshold of 2
    EXPECT_TRUE(queue.isQueuePressureDecompressBypassActive());

    // Explicit threshold testing
    EXPECT_TRUE(queue.isQueuePressureDecompressBypassActive(uint32_t(2)));
    EXPECT_FALSE(queue.isQueuePressureDecompressBypassActive(uint32_t(3)));

    // Ratio threshold testing
    EXPECT_TRUE(queue.isQueuePressureDecompressBypassActive(0.1)); // 0.1 * 16 = 1 <= 2
    EXPECT_FALSE(queue.isQueuePressureDecompressBypassActive(0.5)); // 0.5 * 16 = 8 > 2

    while (!eq.empty()) {
        tickHandler.setCurTick(eq.nextTick());
        eq.serviceOne();
    }

    delete pkt1;
    delete pkt2;
}

} // namespace gem5
