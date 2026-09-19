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

#include <cstddef>
#include <cstdint>

#include "mem/cache/mshr.hh"
#include "mem/cache/mshr_queue.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

class MSHRTestFixture : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(MSHRTestFixture, TargetCompressionMetadataInitialization)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    MSHR::Target target1(&pkt, 0, 1, MSHR::Target::FromCPU, true, true, 256, 2);
    EXPECT_EQ(target1.compressedSizeBits, 256);
    EXPECT_EQ(target1.compressionFactor, 2);

    MSHR::Target target2(&pkt, 0, 2, MSHR::Target::FromCPU, true, true, 0, 1);
    // When 0 is passed, defaults to pkt size in bits (64 * 8 = 512)
    EXPECT_EQ(target2.compressedSizeBits, 512);
    EXPECT_EQ(target2.compressionFactor, 1);
}

TEST_F(MSHRTestFixture, TargetListCompressionMerging)
{
    RequestPtr req1 = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt1(req1, MemCmd::ReadReq);

    RequestPtr req2 = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt2(req2, MemCmd::ReadReq);

    MSHR::TargetList targetList("testList");
    targetList.init(0x1000, 64);

    // Initial reset state defaults to uncompressed size (64 * 8 = 512 bits)
    EXPECT_EQ(targetList.getCompressedSizeBits(), 512);
    EXPECT_EQ(targetList.getCompressionFactor(), 1);

    // Add first target with 256 bits compressed size
    targetList.add(&pkt1, 0, 1, MSHR::Target::FromCPU, true, true, 256, 2);
    EXPECT_EQ(targetList.getCompressedSizeBits(), 256);
    EXPECT_EQ(targetList.getCompressionFactor(), 2);

    // Add second target requiring 384 bits (less compressed)
    targetList.add(&pkt2, 0, 2, MSHR::Target::FromCPU, true, true, 384, 1);
    // Aggregate required size must accommodate the larger target size (384 bits)
    EXPECT_EQ(targetList.getCompressedSizeBits(), 384);
    EXPECT_EQ(targetList.getCompressionFactor(), 1);
}

TEST_F(MSHRTestFixture, MSHRCompressionAccessors)
{
    RequestPtr req = std::make_shared<Request>(0x2000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    MSHR mshr("testMSHR");
    mshr.allocate(0x2000, 64, &pkt, 0, 1, true, 128, 4);

    EXPECT_EQ(mshr.getCompressedSizeBits(), 128);
    EXPECT_EQ(mshr.getCompressionFactor(), 4);
}
