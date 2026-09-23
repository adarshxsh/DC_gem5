/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mem/packet.hh"
#include "mem/packet_queue.hh"
#include "sim/cur_tick.hh"
#include "sim/eventq.hh"

using namespace gem5;

class MockPacketQueue : public PacketQueue
{
  public:
    bool sendTimingSuccess = true;

    MockPacketQueue(EventManager &em, const std::string &label,
                    size_t high_watermark = 8, size_t low_watermark = 2)
        : PacketQueue(em, label, label + "-event", false, false,
                      high_watermark, low_watermark)
    {}

    const std::string
    name() const override
    {
        return "MockPacketQueue";
    }

    bool
    sendTiming(PacketPtr pkt) override
    {
        return sendTimingSuccess;
    }
};

class MockBackpressureListener : public PacketQueue::BackpressureListener
{
  public:
    int activeCount = 0;
    int inactiveCount = 0;
    bool lastState = false;

    void
    onBackpressure(bool active) override
    {
        lastState = active;
        if (active) {
            activeCount++;
        } else {
            inactiveCount++;
        }
    }
};

TEST(PacketQueueTest, WatermarkAccessors)
{
    EventQueue eq("test_eq");
    EventManager em(&eq);
    MockPacketQueue queue(em, "test_queue", 10, 3);

    EXPECT_EQ(queue.getHighWatermark(), 10);
    EXPECT_EQ(queue.getLowWatermark(), 3);
    EXPECT_FALSE(queue.isBackpressureActive());

    queue.setHighWatermark(12);
    EXPECT_EQ(queue.getHighWatermark(), 12);

    queue.setLowWatermark(4);
    EXPECT_EQ(queue.getLowWatermark(), 4);

    queue.setWatermarks(16, 5);
    EXPECT_EQ(queue.getHighWatermark(), 16);
    EXPECT_EQ(queue.getLowWatermark(), 5);
}

TEST(PacketQueueTest, BackpressureCallbackAndHysteresis)
{
    EventQueue eq("test_eq");
    EventManager em(&eq);
    Tick mockTick = 100;
    Gem5Internal::_curTickPtr = &mockTick;

    MockPacketQueue queue(em, "test_queue", 4, 2);

    bool callbackState = false;
    int callbackTransitions = 0;

    queue.registerBackpressureCallback([&](bool active) {
        callbackState = active;
        callbackTransitions++;
    });

    MockBackpressureListener listener;
    queue.registerBackpressureListener(&listener);

    EXPECT_FALSE(queue.isBackpressureActive());

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    std::vector<PacketPtr> pkts;
    for (int i = 0; i < 10; ++i) {
        pkts.push_back(new Packet(req, MemCmd::ReadReq));
    }

    // Add 1 packet (size 1 < highWatermark 4)
    queue.schedSendTiming(pkts[0], mockTick);
    EXPECT_FALSE(queue.isBackpressureActive());
    EXPECT_EQ(callbackTransitions, 0);

    // Add 2nd and 3rd packets
    queue.schedSendTiming(pkts[1], mockTick);
    queue.schedSendTiming(pkts[2], mockTick);
    EXPECT_FALSE(queue.isBackpressureActive());
    EXPECT_EQ(callbackTransitions, 0);

    // Add 4th packet -> reaches highWatermark (4)
    queue.schedSendTiming(pkts[3], mockTick);
    EXPECT_TRUE(queue.isBackpressureActive());
    EXPECT_TRUE(callbackState);
    EXPECT_EQ(callbackTransitions, 1);
    EXPECT_EQ(listener.activeCount, 1);

    // Add 5th packet (size 5 > highWatermark 4) -> stays active without
    // re-triggering callback
    queue.schedSendTiming(pkts[4], mockTick);
    EXPECT_TRUE(queue.isBackpressureActive());
    EXPECT_EQ(callbackTransitions, 1);

    // Transmit packets one by one
    // Send 1st packet: remaining size = 4 (stays active, > lowWatermark 2)
    queue.retry();
    EXPECT_TRUE(queue.isBackpressureActive());
    EXPECT_EQ(callbackTransitions, 1);

    // Send 2nd packet: remaining size = 3 (stays active, > lowWatermark 2)
    queue.retry();
    EXPECT_TRUE(queue.isBackpressureActive());
    EXPECT_EQ(callbackTransitions, 1);

    // Send 3rd packet: remaining size = 2 (reaches lowWatermark 2) ->
    // deactivates backpressure
    queue.retry();
    EXPECT_FALSE(queue.isBackpressureActive());
    EXPECT_FALSE(callbackState);
    EXPECT_EQ(callbackTransitions, 2);
    EXPECT_EQ(listener.inactiveCount, 1);

    // Clean up packets
    for (auto pkt : pkts) {
        delete pkt;
    }
}
