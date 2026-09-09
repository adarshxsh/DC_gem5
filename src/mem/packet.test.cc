/*
 * Copyright 2026 gem5
 * All rights reserved
 */

#include <gtest/gtest.h>
#include <memory>

#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

static Tick dummyTick = 0;

class PacketTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Gem5Internal::_curTickPtr = &dummyTick;
    }
};

TEST_F(PacketTest, PayloadSizeDefaultAndAccessors)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getPayloadSize(), 64);

    pkt.setPayloadSize(32);
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getPayloadSize(), 32);
}

TEST_F(PacketTest, CopyConstructorPreservesPayloadSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr pkt1 = new Packet(req, MemCmd::WritebackDirty, 64);
    pkt1->setPayloadSize(16);

    PacketPtr pkt2 = new Packet(pkt1, true, true);
    EXPECT_EQ(pkt2->getSize(), 64);
    EXPECT_EQ(pkt2->getPayloadSize(), 16);

    delete pkt1;
    delete pkt2;
}

} // namespace gem5
