/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/packet.hh"

namespace gem5
{

class PacketTest : public ::testing::Test
{
  protected:
    Tick testTick = 0;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &testTick;
    }
};

TEST_F(PacketTest, CompressedSizeDefaultsToUncompressedSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq, 64);

    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSize(), 64);
    EXPECT_FALSE(pkt.isCompressed());
}

TEST_F(PacketTest, SetAndGetCompressedSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq, 64);

    pkt.setCompressedSize(16);
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSize(), 16);
    EXPECT_TRUE(pkt.isCompressed());

    pkt.setCompressedSize(32);
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSize(), 32);
    EXPECT_TRUE(pkt.isCompressed());

    pkt.setCompressedSize(64);
    EXPECT_EQ(pkt.getCompressedSize(), 64);
    EXPECT_FALSE(pkt.isCompressed());
}

} // namespace gem5
