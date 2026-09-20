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

#include <memory>

#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

class PacketTest : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(PacketTest, UncompressedPacketPayloadSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.hasCompressedSize());
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getPayloadSize(), 64);
}

TEST_F(PacketTest, CompressedPacketPayloadSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    EXPECT_FALSE(pkt.hasCompressedSize());
    pkt.setCompressedSize(24);

    EXPECT_TRUE(pkt.hasCompressedSize());
    EXPECT_EQ(pkt.getCompressedSize(), 24);
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getPayloadSize(), 24);
}

TEST_F(PacketTest, CopyPacketPreservesCompressedSize)
{
    RequestPtr req = std::make_shared<Request>(0x2000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);
    pkt.setCompressedSize(16);

    Packet copyPkt(&pkt, false, false);
    EXPECT_TRUE(copyPkt.hasCompressedSize());
    EXPECT_EQ(copyPkt.getCompressedSize(), 16);
    EXPECT_EQ(copyPkt.getPayloadSize(), 16);
    EXPECT_EQ(copyPkt.getSize(), 64);
}

} // namespace gem5
