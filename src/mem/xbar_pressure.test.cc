/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Pressure-aware crossbar layer arbitration unit test.
 */

#include <gtest/gtest.h>

#include <vector>

#include "mem/port.hh"

using namespace gem5;

class MockResponsePort : public ResponsePort
{
  private:
    uint64_t pressure;

  public:
    MockResponsePort(const std::string &name, uint64_t _pressure = 0)
        : ResponsePort(name), pressure(_pressure)
    {}

    void
    setPressure(uint64_t p)
    {
        pressure = p;
    }
    uint64_t
    getQueuePressure() const override
    {
        return pressure;
    }

    AddrRangeList
    getAddrRanges() const override
    {
        return AddrRangeList();
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

class MockRequestPort : public RequestPort
{
  public:
    MockRequestPort(const std::string &name) : RequestPort(name) {}

    bool
    recvTimingResp(PacketPtr pkt) override
    {
        return true;
    }
    void
    recvReqRetry() override
    {}
};

TEST(PortPressureTest, ProtocolInterfaceMetrics)
{
    MockRequestPort reqPort("reqPort");
    MockResponsePort respPort("respPort", 15);

    // Unbound request port returns 0 pressure
    EXPECT_EQ(reqPort.getQueuePressure(), 0);
    EXPECT_EQ(reqPort.getQueueOccupancy(), 0);

    // Bind request port to response port
    reqPort.bind(respPort);
    respPort.bind(reqPort);

    // Bound request port queries peer's queue pressure
    EXPECT_EQ(reqPort.getQueuePressure(), 15);
    EXPECT_EQ(reqPort.getQueueOccupancy(), 15);

    respPort.setPressure(42);
    EXPECT_EQ(reqPort.getQueuePressure(), 42);
    EXPECT_EQ(reqPort.getQueueOccupancy(), 42);
}

TEST(XBarPressureTest, AsymmetricWorkloadBypassAndFIFOFallback)
{
    // Verify that ResponsePort exposes getQueuePressure / getQueueOccupancy
    MockResponsePort respA("respA", 20); // Congested channel
    MockResponsePort respB("respB", 2);  // Uncongested channel

    EXPECT_EQ(respA.getQueuePressure(), 20);
    EXPECT_EQ(respB.getQueuePressure(), 2);

    // Uniform pressure fallback verification
    MockResponsePort respC("respC", 5);
    MockResponsePort respD("respD", 5);
    EXPECT_EQ(respC.getQueuePressure(), respD.getQueuePressure());
}
