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

#include "mem/mem_ctrl.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

using namespace gem5;
using namespace gem5::memory;

namespace gem5 {
namespace Gem5Internal {
Tick _curTick = 0;
__thread Tick *_curTickPtr = &_curTick;
}
}

TEST(PacketCompressionTest, DefaultUncompressed)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSizeBits(), 0);
}

TEST(PacketCompressionTest, SetAndClearCompressed)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);

    pkt.setCompressedSizeBits(128); // 16 bytes compressed
    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSizeBits(), 128);

    pkt.setUncompressed();
    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSizeBits(), 0);

    pkt.setCompressedSizeBits(256);
    EXPECT_TRUE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSizeBits(), 256);

    pkt.setCompressedSizeBits(0);
    EXPECT_FALSE(pkt.isCompressed());
    EXPECT_EQ(pkt.getCompressedSizeBits(), 0);
}

TEST(PacketCompressionTest, CopyPacketRetainsCompression)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet orig_pkt(req, MemCmd::WritebackDirty);
    orig_pkt.setCompressedSizeBits(192);

    Packet copy_pkt(&orig_pkt, false, false);
    EXPECT_TRUE(copy_pkt.isCompressed());
    EXPECT_EQ(copy_pkt.getCompressedSizeBits(), 192);
}

TEST(PacketCompressionTest, CopyResponderFlags)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet req_pkt(req, MemCmd::ReadReq);

    Packet resp_pkt(req, MemCmd::ReadResp);
    resp_pkt.setCompressedSizeBits(128);

    req_pkt.copyResponderFlags(&resp_pkt);
    EXPECT_TRUE(req_pkt.isCompressed());
    EXPECT_EQ(req_pkt.getCompressedSizeBits(), 128);
}

TEST(MemPacketCompressionTest, CapturesCompressionFromPacket)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::WritebackDirty);
    pkt.setCompressedSizeBits(160);

    MemPacket mem_pkt(&pkt, false, true, 0, 0, 0, 0, 0, 0x1000, 64);
    EXPECT_TRUE(mem_pkt.isCompressed());
    EXPECT_EQ(mem_pkt.getCompressedSizeBits(), 160);
}
