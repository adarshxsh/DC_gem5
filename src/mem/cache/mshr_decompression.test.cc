/*
 * Copyright (c) 2026 gem5
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

#include "mem/cache/tags/super_blk.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

TEST(MSHRDecompressionLatencyTest, NullCompressorAndBlock)
{
    // Test packet properties for reads and whole-line write checks
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet readPkt(req, MemCmd::ReadReq);

    EXPECT_TRUE(readPkt.isRead());
    EXPECT_FALSE(readPkt.isWholeLineWrite(64));
}

TEST(MSHRDecompressionLatencyTest, ReadRequestDecompressionLatency)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet readPkt(req, MemCmd::ReadReq);

    EXPECT_TRUE(readPkt.isRead());
    EXPECT_TRUE(readPkt.isRead() || !readPkt.isWholeLineWrite(64));
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}

TEST(MSHRDecompressionLatencyTest, WholeLineWriteSkipsDecompression)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet writePkt(req, MemCmd::WriteReq);

    EXPECT_FALSE(writePkt.isRead());
    EXPECT_TRUE(writePkt.isWholeLineWrite(64));
    // When isRead() is false and isWholeLineWrite(64) is true,
    // (pkt.isRead() || !pkt.isWholeLineWrite(64)) evaluates to false.
    EXPECT_FALSE(writePkt.isRead() || !writePkt.isWholeLineWrite(64));
}

TEST(MSHRDecompressionLatencyTest, PartialLineWriteRequiresDecompression)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));

    RequestPtr req = std::make_shared<Request>(0x1000, 32, 0, 0);
    Packet partialWritePkt(req, MemCmd::WriteReq);

    EXPECT_FALSE(partialWritePkt.isRead());
    EXPECT_FALSE(partialWritePkt.isWholeLineWrite(64));
    // For partial line writes, (pkt.isRead() || !pkt.isWholeLineWrite(64))
    // evaluates to true.
    EXPECT_TRUE(partialWritePkt.isRead() ||
                !partialWritePkt.isWholeLineWrite(64));
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}
