/*
 * Copyright (c) 2026
 * All rights reserved
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

#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

static Tick mockTick = 0;

class PacketTestFixture : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(PacketTestFixture, UncompressedDefaults)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSize(), 64);
    EXPECT_EQ(pkt.getTransferSize(), 64);
    EXPECT_EQ(pkt.getCompressedSizeBits(), 0);
}

TEST_F(PacketTestFixture, SetCompressedSizeBits)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    // Set compressed size to 256 bits (32 bytes)
    pkt.setCompressedSizeBits(256);

    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSizeBits(), 256);
    EXPECT_EQ(pkt.getCompressedSize(), 32);
    EXPECT_EQ(pkt.getTransferSize(), 32);
}

TEST_F(PacketTestFixture, SetCompressedSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    // Set compressed size to 16 bytes (128 bits)
    pkt.setCompressedSize(16);

    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_EQ(pkt.getCompressedSizeBits(), 128);
    EXPECT_EQ(pkt.getCompressedSize(), 16);
    EXPECT_EQ(pkt.getTransferSize(), 16);
}

TEST_F(PacketTestFixture, CopyConstructorPreservesCompressedMetadata)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr pkt1 = new Packet(req, MemCmd::WritebackDirty);
    pkt1->setCompressedSizeBits(256); // 32 bytes

    PacketPtr pkt2 = new Packet(pkt1, false, false);

    EXPECT_TRUE(pkt2->isCompressed());
    EXPECT_EQ(pkt2->getSize(), 64);
    EXPECT_EQ(pkt2->getCompressedSizeBits(), 256);
    EXPECT_EQ(pkt2->getCompressedSize(), 32);
    EXPECT_EQ(pkt2->getTransferSize(), 32);

    delete pkt1;
    delete pkt2;
}

TEST_F(PacketTestFixture, DecoupledDecompressionLatencyAndTransferTiming)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    // Set compressed size to 16 bytes (128 bits)
    pkt.setCompressedSize(16);

    // Simulate writeback assigning decompression latency (e.g. 20 cycles) to headerDelay
    pkt.headerDelay += 20;

    EXPECT_EQ(pkt.headerDelay, 20);
    EXPECT_EQ(pkt.payloadDelay, 0);

    // Verify transfer size reflects compressed size (16 bytes) instead of full line (64 bytes)
    EXPECT_EQ(pkt.getTransferSize(), 16);
}
