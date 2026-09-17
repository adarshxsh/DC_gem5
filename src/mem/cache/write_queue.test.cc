/*
 * Copyright (c) 2026
 * All rights reserved
 */

#include <gtest/gtest.h>

#include "mem/cache/base.hh"
#include "mem/cache/write_queue.hh"
#include "mem/cache/write_queue_entry.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"
#include "sim/root.hh"

using namespace gem5;

namespace gem5 {
bool BaseCache::sendWriteQueuePacket(WriteQueueEntry* wq_entry) { return false; }
}

class WriteQueueTest : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

    void SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }
};

TEST_F(WriteQueueTest, SubBlockDirtyTrackingOnAllocate)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 8, 0, 0);
    PacketPtr pkt = new Packet(req, MemCmd::WritebackDirty);
    pkt->allocate();

    WriteQueueEntry entry("test_entry");
    entry.allocate(0x1000, 64, pkt, 10, 1);

    // 64-byte block with default 16-byte sub-block size = 4 sub-blocks
    EXPECT_EQ(entry.getNumSubBlocks(), 4);
    EXPECT_TRUE(entry.isSubBlockDirty(0));
    EXPECT_FALSE(entry.isSubBlockDirty(1));
    EXPECT_FALSE(entry.isSubBlockDirty(2));
    EXPECT_FALSE(entry.isSubBlockDirty(3));
    EXPECT_EQ(entry.getNumDirtySubBlocks(), 1);

    delete pkt;
}

TEST_F(WriteQueueTest, CoalesceWithinSameSubBlock)
{
    RequestPtr req1 = std::make_shared<Request>(0x1000, 8, 0, 0);
    PacketPtr pkt1 = new Packet(req1, MemCmd::WritebackDirty);
    pkt1->allocate();

    WriteQueueEntry entry("test_entry");
    entry.allocate(0x1000, 64, pkt1, 10, 1);

    // Write to byte offset 8 (still within sub-block 0: bytes 0..15)
    RequestPtr req2 = std::make_shared<Request>(0x1008, 8, 0, 0);
    PacketPtr pkt2 = new Packet(req2, MemCmd::WritebackDirty);
    pkt2->allocate();

    entry.coalesceSubBlock(pkt2, 12, 2);

    EXPECT_EQ(entry.getNumSubBlocks(), 4);
    EXPECT_TRUE(entry.isSubBlockDirty(0));
    EXPECT_FALSE(entry.isSubBlockDirty(1));
    EXPECT_FALSE(entry.isSubBlockDirty(2));
    EXPECT_FALSE(entry.isSubBlockDirty(3));
    EXPECT_EQ(entry.getNumDirtySubBlocks(), 1);

    delete pkt1;
    delete pkt2;
}

TEST_F(WriteQueueTest, CoalesceDifferentSubBlock)
{
    RequestPtr req1 = std::make_shared<Request>(0x1000, 8, 0, 0);
    PacketPtr pkt1 = new Packet(req1, MemCmd::WritebackDirty);
    pkt1->allocate();

    WriteQueueEntry entry("test_entry");
    entry.allocate(0x1000, 64, pkt1, 10, 1);

    // Write to byte offset 32 (sub-block 2: bytes 32..47)
    RequestPtr req2 = std::make_shared<Request>(0x1020, 8, 0, 0);
    PacketPtr pkt2 = new Packet(req2, MemCmd::WritebackDirty);
    pkt2->allocate();

    entry.coalesceSubBlock(pkt2, 15, 2);

    EXPECT_EQ(entry.getNumSubBlocks(), 4);
    EXPECT_TRUE(entry.isSubBlockDirty(0));
    EXPECT_FALSE(entry.isSubBlockDirty(1));
    EXPECT_TRUE(entry.isSubBlockDirty(2));
    EXPECT_FALSE(entry.isSubBlockDirty(3));
    EXPECT_EQ(entry.getNumDirtySubBlocks(), 2);

    delete pkt1;
    delete pkt2;
}

TEST_F(WriteQueueTest, FindCoalesceInWriteQueue)
{
    WriteQueue wq("test_wq", 4, 0, "test");

    RequestPtr req1 = std::make_shared<Request>(0x2000, 8, 0, 0);
    PacketPtr pkt1 = new Packet(req1, MemCmd::WritebackDirty);
    pkt1->allocate();

    WriteQueueEntry *entry = wq.allocate(0x2000, 64, pkt1, 10, 1);
    EXPECT_NE(entry, nullptr);

    // Searching for same block address should find entry
    RequestPtr req2 = std::make_shared<Request>(0x2008, 8, 0, 0);
    PacketPtr pkt2 = new Packet(req2, MemCmd::WritebackDirty);
    pkt2->allocate();

    WriteQueueEntry *found = wq.findCoalesce(0x2000, 64, pkt2, false);
    EXPECT_EQ(found, entry);

    // Searching for different block address should return nullptr
    WriteQueueEntry *not_found = wq.findCoalesce(0x3000, 64, pkt2, false);
    EXPECT_EQ(not_found, nullptr);

    delete pkt1;
    delete pkt2;
}
