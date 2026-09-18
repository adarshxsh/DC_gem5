#include <gtest/gtest.h>

#include "mem/cache/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "mem/cache/write_queue.hh"
#include "mem/cache/write_queue_entry.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

static Tick currentTestTick = 0;

bool
BaseCache::sendWriteQueuePacket(WriteQueueEntry *wq_entry)
{
    return false;
}

class WriteQueueTest : public ::testing::Test
{
  protected:
    WriteQueue writeQueue;

    WriteQueueTest() : writeQueue("test_wq", 8, 0, "test_cache")
    {
        Gem5Internal::_curTickPtr = &currentTestTick;
    }
};

TEST_F(WriteQueueTest, SubBlockCoalescingAndBitmaskTracking)
{
    SuperBlk superBlk;
    superBlk.setBlkSize(64);
    superBlk.blks.resize(4, nullptr);

    Addr superBlkAddr = 0x1000;
    Addr subBlk0Addr = 0x1000;
    Addr subBlk1Addr = 0x1040;
    Addr subBlk2Addr = 0x1080;

    RequestPtr req0 = std::make_shared<Request>(subBlk0Addr, 64, 0, 0);
    PacketPtr pkt0 = new Packet(req0, MemCmd::WritebackDirty);
    pkt0->allocate();

    RequestPtr req1 = std::make_shared<Request>(subBlk1Addr, 64, 0, 0);
    PacketPtr pkt1 = new Packet(req1, MemCmd::WritebackDirty);
    pkt1->allocate();

    RequestPtr req2 = std::make_shared<Request>(subBlk2Addr, 64, 0, 0);
    PacketPtr pkt2 = new Packet(req2, MemCmd::WritebackDirty);
    pkt2->allocate();

    // Allocate initial entry for sub-block 0
    WriteQueueEntry *entry = writeQueue.allocate(
        subBlk0Addr, 64, pkt0, 100, 1, &superBlk, superBlkAddr, 0, 256);

    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isSuperBlockEntry());
    EXPECT_EQ(entry->getSuperBlock(), &superBlk);
    EXPECT_EQ(entry->getSuperBlockAddr(), superBlkAddr);
    EXPECT_EQ(entry->getSubBlkMask(), (1ULL << 0));
    EXPECT_EQ(entry->getNumTargets(), 1);
    EXPECT_EQ(entry->getTotalCompressedSizeBits(), 256);
    EXPECT_EQ(entry->getReadyTime(), 100);

    // Find match for sub-block 1
    WriteQueueEntry *match1 = writeQueue.findMatch(subBlk1Addr, false, true,
                                                   superBlkAddr, &superBlk);
    EXPECT_EQ(match1, entry);

    // Coalesce sub-block 1 with delay = 10 (when_ready = 120 -> 120 + 10 =
    // 130)
    entry->coalesceSubBlock(pkt1, 120, 2, 1, 128, 10);
    EXPECT_EQ(entry->getNumTargets(), 2);
    EXPECT_EQ(entry->getSubBlkMask(), (1ULL << 0) | (1ULL << 1));
    EXPECT_EQ(entry->getTotalCompressedSizeBits(), 256 + 128);
    EXPECT_EQ(entry->getReadyTime(), 130);

    // Coalesce sub-block 2 with delay = 15 (when_ready = 130 -> 130 + 15 =
    // 145)
    entry->coalesceSubBlock(pkt2, 130, 3, 2, 192, 15);
    EXPECT_EQ(entry->getNumTargets(), 3);
    EXPECT_EQ(entry->getSubBlkMask(), (1ULL << 0) | (1ULL << 1) | (1ULL << 2));
    EXPECT_EQ(entry->getTotalCompressedSizeBits(), 256 + 128 + 192);
    EXPECT_EQ(entry->getReadyTime(), 145);

    delete pkt0;
    delete pkt1;
    delete pkt2;
}

TEST_F(WriteQueueTest, UncacheableGuardrailPreventsCoalescing)
{
    Addr addr = 0x2000;
    RequestPtr reqUnc =
        std::make_shared<Request>(addr, 64, Request::UNCACHEABLE, 0);
    PacketPtr pktUnc = new Packet(reqUnc, MemCmd::WriteReq);
    pktUnc->allocate();

    WriteQueueEntry *entry = writeQueue.allocate(addr, 64, pktUnc, 100, 1);

    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isUncacheable());

    // findMatch with ignore_uncacheable = true should not match uncacheable
    // entry
    WriteQueueEntry *match = writeQueue.findMatch(addr, false, true);
    EXPECT_EQ(match, nullptr);

    delete pktUnc;
}

} // namespace gem5
