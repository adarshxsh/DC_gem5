/*
 * Copyright (c) 2026 gem5 Research Node
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

#include <algorithm>

#include <gtest/gtest.h>

#include "base/intmath.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

static Tick dummyCurTick = 0;
struct TestInit
{
    TestInit() { Gem5Internal::_curTickPtr = &dummyCurTick; }
} testInit;

TEST(CompressedPacketTest, UncompressedPacketDefaults)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSize(), 64);
    EXPECT_EQ(pkt.getSize(), 64);
}

TEST(CompressedPacketTest, SetCompressedSize)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    pkt.setCompressedSize(32);

    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSize(), 32);
    EXPECT_EQ(pkt.getSize(), 64);
    EXPECT_TRUE(req->extraDataValid());
    EXPECT_EQ(req->getExtraData(), 32);
}

TEST(CompressedPacketTest, CopyCompressedPacket)
{
    RequestPtr req = std::make_shared<Request>(0x2000, 64, 0, 0);
    PacketPtr pkt1 = new Packet(req, MemCmd::WritebackDirty);
    pkt1->setCompressedSize(16);

    PacketPtr pkt2 = new Packet(pkt1, false, false);

    EXPECT_TRUE(pkt2->isCompressed());
    EXPECT_EQ(pkt2->getCompressedSize(), 16);

    delete pkt1;
    delete pkt2;
}

TEST(CompressedPacketTest, QueueSlotAllocation)
{
    // Test divCeil queue slot calculation for compressed vs uncompressed
    // packets
    unsigned burst_size = 64;

    // Uncompressed 64B packet
    unsigned uncomp_size = 64;
    unsigned offset = 0;
    unsigned uncomp_pkt_count = divCeil(offset + uncomp_size, burst_size);
    EXPECT_EQ(uncomp_pkt_count, 1);

    // Compressed 32B packet
    unsigned comp_size_32 = 32;
    unsigned comp_pkt_count_32 = divCeil(offset + comp_size_32, burst_size);
    EXPECT_EQ(comp_pkt_count_32, 1);

    // Uncompressed 128B packet across 2 bursts
    unsigned large_uncomp_size = 128;
    unsigned large_uncomp_count =
        divCeil(offset + large_uncomp_size, burst_size);
    EXPECT_EQ(large_uncomp_count, 2);

    // Compressed 32B representation of 128B line
    unsigned large_comp_size = 32;
    unsigned large_comp_count = divCeil(offset + large_comp_size, burst_size);
    EXPECT_EQ(large_comp_count, 1);
}

TEST(CompressedPacketTest, EffectiveBurstTiming)
{
    // Test effective_tBURST calculation scaling
    Tick tBURST = 4000;
    Tick tBURST_MIN = 2000;
    unsigned burst_bytes = 64;

    // 2:1 compressed (32 bytes)
    unsigned comp_bytes_32 = 32;
    Tick effective_32 = (tBURST * comp_bytes_32) / burst_bytes;
    effective_32 = std::max(effective_32, tBURST_MIN);
    EXPECT_EQ(effective_32, 2000);

    // 4:1 compressed (16 bytes) -> bound by tBURST_MIN
    unsigned comp_bytes_16 = 16;
    Tick effective_16 = (tBURST * comp_bytes_16) / burst_bytes;
    effective_16 = std::max(effective_16, tBURST_MIN);
    EXPECT_EQ(effective_16, 2000);

    // Zero-compressed (0 bytes) -> bound by tBURST_MIN
    unsigned comp_bytes_0 = 0;
    Tick effective_0 = (tBURST * comp_bytes_0) / burst_bytes;
    effective_0 = std::max(effective_0, tBURST_MIN);
    EXPECT_EQ(effective_0, 2000);
}

} // namespace gem5
