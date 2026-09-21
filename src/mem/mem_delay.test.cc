/*
 * Copyright (c) 2024
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/mem_delay.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "params/SimpleMemDelay.hh"

namespace gem5
{

class TestSimpleMemDelay : public SimpleMemDelay
{
  public:
    TestSimpleMemDelay(const SimpleMemDelayParams &p) : SimpleMemDelay(p) {}

    using MemDelay::reqQueue;
    using MemDelay::respQueue;

    Tick
    testDelayReq(PacketPtr pkt)
    {
        return delayReq(pkt);
    }
    Tick
    testDelayResp(PacketPtr pkt)
    {
        return delayResp(pkt);
    }
};

class MemDelayTest : public ::testing::Test
{
  protected:
    RequestPtr req;
    PacketPtr readReqPkt;
    PacketPtr writeReqPkt;
    PacketPtr readRespPkt;
    PacketPtr writeRespPkt;

    void
    SetUp() override
    {
        req = std::make_shared<Request>(0x1000, 64, 0, 0);
        readReqPkt = Packet::createRead(req);
        writeReqPkt = Packet::createWrite(req);

        readRespPkt = Packet::createRead(req);
        readRespPkt->makeResponse();

        writeRespPkt = Packet::createWrite(req);
        writeRespPkt->makeResponse();
    }

    void
    TearDown() override
    {
        delete readReqPkt;
        delete writeReqPkt;
        delete readRespPkt;
        delete writeRespPkt;
    }

    SimpleMemDelayParams
    createParams(bool enable_bp = false, unsigned threshold = 0,
                 double multiplier = 1.0, bool bypass_comp = true)
    {
        SimpleMemDelayParams p;
        p.name = "test_mem_delay";
        p.read_req = 10;
        p.read_resp = 20;
        p.write_req = 15;
        p.write_resp = 25;
        p.enable_backpressure = enable_bp;
        p.backpressure_threshold = threshold;
        p.backpressure_multiplier = multiplier;
        p.bypass_compressed = bypass_comp;
        return p;
    }
};

TEST_F(MemDelayTest, StaticDelayWhenBackpressureDisabled)
{
    SimpleMemDelayParams p = createParams(false, 0, 1.5, true);
    TestSimpleMemDelay memDelay(p);

    EXPECT_EQ(memDelay.getReqQueueSize(), 0);
    EXPECT_EQ(memDelay.getRespQueueSize(), 0);
    EXPECT_EQ(memDelay.getTotalQueueSize(), 0);

    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 10);
    EXPECT_EQ(memDelay.testDelayReq(writeReqPkt), 15);
    EXPECT_EQ(memDelay.testDelayResp(readRespPkt), 20);
    EXPECT_EQ(memDelay.testDelayResp(writeRespPkt), 25);
}

TEST_F(MemDelayTest, DynamicDelayScalingWhenThresholdExceeded)
{
    SimpleMemDelayParams p = createParams(true, 1, 1.5, true);
    TestSimpleMemDelay memDelay(p);

    // Queue size 0 <= threshold 1
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 10);

    // Add dummy packets to reqQueue
    memDelay.reqQueue.schedSendTiming(readReqPkt, 100);
    memDelay.reqQueue.schedSendTiming(writeReqPkt, 200);

    // Queue size 2 > threshold 1 -> excess = 1 -> multiplier = 1.5
    // Read req: 10 * 1.5 = 15
    EXPECT_EQ(memDelay.getReqQueueSize(), 2);
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 15);
    // Write req: 15 * 1.5 = 22
    EXPECT_EQ(memDelay.testDelayReq(writeReqPkt), 22);

    // Add another packet -> Queue size 3 > threshold 1 -> excess = 2 -> factor
    // = 1.0 + 0.5*2 = 2.0
    memDelay.reqQueue.schedSendTiming(readReqPkt, 300);
    EXPECT_EQ(memDelay.getReqQueueSize(), 3);
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 20);
}

TEST_F(MemDelayTest, BypassCompressedPacketDelayPenalty)
{
    SimpleMemDelayParams p = createParams(true, 0, 2.0, true);
    TestSimpleMemDelay memDelay(p);

    memDelay.reqQueue.schedSendTiming(readReqPkt, 100);
    EXPECT_GT(memDelay.getReqQueueSize(), 0);

    // Uncompressed packet gets scaled delay
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 20);

    // Compressed packet bypasses dynamic penalty
    readReqPkt->setCompressed();
    EXPECT_TRUE(readReqPkt->isCompressed());
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 10);
}

TEST_F(MemDelayTest, BypassCacheBypassedPacketDelayPenalty)
{
    SimpleMemDelayParams p = createParams(true, 0, 2.0, true);
    TestSimpleMemDelay memDelay(p);

    memDelay.reqQueue.schedSendTiming(readReqPkt, 100);

    // Set GLC_BIT on request to indicate cache bypass
    req->setCacheCoherenceFlags(Request::GLC_BIT);
    EXPECT_TRUE(readReqPkt->isBypassed());
    EXPECT_EQ(memDelay.testDelayReq(readReqPkt), 10);
}

} // namespace gem5
