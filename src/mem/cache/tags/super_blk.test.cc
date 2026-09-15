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

#include <list>

#include "mem/cache/queue.hh"
#include "mem/cache/tags/super_blk.hh"

using namespace gem5;

class TestQueueEntry : public QueueEntry
{
  public:
    using List = std::list<TestQueueEntry *>;
    using Iterator = List::iterator;

    Iterator allocIter;
    Iterator readyIter;

    TestQueueEntry(const std::string &name = "test") : QueueEntry(name) {}

    void
    deallocate()
    {}
    bool
    matchBlockAddr(const Addr addr, const bool is_secure) const override
    {
        return false;
    }
    bool
    matchBlockAddr(const PacketPtr pkt) const override
    {
        return false;
    }
    bool
    conflictAddr(const QueueEntry *entry) const override
    {
        return false;
    }
    bool
    sendPacket(BaseCache &cache) override
    {
        return false;
    }
    Target *
    getTarget() override
    {
        return nullptr;
    }
};

class TestQueue : public Queue<TestQueueEntry>
{
  public:
    TestQueue(const std::string &label, int num_entries, int reserve,
              const std::string &name)
        : Queue<TestQueueEntry>(label, num_entries, reserve, name)
    {}
};

/**
 * Test that CompressionBlk correctly sets compression status for uncompressed
 * vs compressed sizes.
 */
TEST(SuperBlkTest, UncompressedSubBlockDetection)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(2));

    // Uncompressed line (512 bits for a 64-byte line)
    blk.setSizeBits(512);
    EXPECT_FALSE(blk.isCompressed());
    EXPECT_EQ(blk.getSizeBits(), 512);
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(2));

    // Compressed line (256 bits)
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getSizeBits(), 256);
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(2));
}

TEST(SuperBlkTest, SetUncompressedClearsCompressed)
{
    CompressionBlk blk;
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());

    blk.setUncompressed();
    EXPECT_FALSE(blk.isCompressed());
}

TEST(QueueTest, OccupancyAndSaturation)
{
    TestQueue queue("test_buffer", 8, 0, "test_queue");
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_EQ(queue.capacity(), 8);
    EXPECT_DOUBLE_EQ(queue.saturationRatio(), 0.0);
    EXPECT_FALSE(queue.isFull());
}
