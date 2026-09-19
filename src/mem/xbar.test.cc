/*
 * Copyright (c) 2024 gem5 Project
 * All rights reserved.
 *
 * Unit tests for pressure-aware crossbar scheduling and queue pressure flags.
 */

#include <gtest/gtest.h>

#include "mem/packet.hh"
#include "mem/port.hh"
#include "sim/port.hh"

namespace gem5
{

TEST(XBarPressureTest, PacketQueuePressureFlags)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    // Initial state: no pressure flags
    EXPECT_FALSE(pkt.isQueuePressureLow());
    EXPECT_FALSE(pkt.isQueuePressureModerate());
    EXPECT_FALSE(pkt.isQueuePressureHigh());
    EXPECT_FALSE(pkt.isQueuePressureCritical());

    // Test low pressure (30%)
    pkt.setQueuePressure(0.30f);
    EXPECT_TRUE(pkt.isQueuePressureLow());
    EXPECT_FALSE(pkt.isQueuePressureHigh());

    // Test moderate pressure (60%)
    pkt.setQueuePressure(0.60f);
    EXPECT_TRUE(pkt.isQueuePressureModerate());
    EXPECT_FALSE(pkt.isQueuePressureHigh());

    // Test high pressure (80%)
    pkt.setQueuePressure(0.80f);
    EXPECT_TRUE(pkt.isQueuePressureHigh());
    EXPECT_FALSE(pkt.isQueuePressureCritical());

    // Test critical pressure (95%)
    pkt.setQueuePressure(0.95f);
    EXPECT_TRUE(pkt.isQueuePressureCritical());
    EXPECT_TRUE(pkt.isQueuePressureHigh());
}

class TestResponsePort : public ResponsePort
{
    float pressure;

  public:
    TestResponsePort(const std::string &name, float p)
        : ResponsePort(name), pressure(p)
    {}
    AddrRangeList
    getAddrRanges() const override
    {
        return AddrRangeList();
    }
    float
    getQueuePressure() const override
    {
        return pressure;
    }
    void
    setPressure(float p)
    {
        pressure = p;
    }

    Tick
    recvAtomic(PacketPtr pkt) override
    {
        return 0;
    }
    bool
    recvTimingReq(PacketPtr pkt) override
    {
        return true;
    }
    void
    recvRespRetry() override
    {}
    void
    recvFunctional(PacketPtr pkt) override
    {}
};

class TestRequestPort : public RequestPort
{
  public:
    TestRequestPort(const std::string &name) : RequestPort(name) {}

    bool
    recvTimingResp(PacketPtr pkt) override
    {
        return true;
    }
    void
    recvReqRetry() override
    {}
};

TEST(XBarPressureTest, PortQueuePressureInterface)
{
    TestRequestPort req_port("req_port");
    TestResponsePort resp_port("resp_port", 0.85f);

    EXPECT_EQ(req_port.getPeerQueuePressure(), 0.0f);

    req_port.bind(resp_port);

    EXPECT_FLOAT_EQ(req_port.getPeerQueuePressure(), 0.85f);

    resp_port.setPressure(0.40f);
    EXPECT_FLOAT_EQ(req_port.getPeerQueuePressure(), 0.40f);
}

} // namespace gem5
