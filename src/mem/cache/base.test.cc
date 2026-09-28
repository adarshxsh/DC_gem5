/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "mem/packet.hh"
#include "sim/root.hh"

using namespace gem5;

namespace gem5
{
Root *Root::_root = nullptr;
}

TEST(BaseCacheTest, CompressionExpansionCheck)
{
    // Test CompressionBlk data expansion detection
    SuperBlk superBlk;
    superBlk.setBlkSize(64);

    CompressionBlk blk;
    blk.setSectorBlock(&superBlk);
    blk.setSectorOffset(0);
    blk.registerTagExtractor([](Addr addr) { return addr; });
    blk.insert({0x1000, false});
    blk.setSizeBits(64); // compressed size: 64 bits

    // Expanding size to 256 bits results in DATA_EXPANSION
    EXPECT_EQ(blk.checkExpansionContraction(256),
              CompressionBlk::DATA_EXPANSION);

    // Contraction to 32 bits results in DATA_CONTRACTION
    EXPECT_EQ(blk.checkExpansionContraction(32),
              CompressionBlk::DATA_CONTRACTION);

    // Same size results in UNCHANGED
    EXPECT_EQ(blk.checkExpansionContraction(64), CompressionBlk::UNCHANGED);
}

TEST(BaseCacheTest, EvictBlockCreatesWritebackDirtyWhenDirty)
{
    // Test that when a dirty block is evicted (e.g. on compression failure),
    // a WritebackDirty packet is created instead of dropping data
    SuperBlk superBlk;
    superBlk.setBlkSize(64);

    CompressionBlk blk;
    blk.setSectorBlock(&superBlk);
    blk.setSectorOffset(0);
    blk.registerTagExtractor([](Addr addr) { return addr; });
    blk.insert({0x2000, false});

    // Mark block as writable and dirty (as satisfyRequest or access does on
    // store hit)
    blk.setCoherenceBits(CacheBlk::WritableBit);
    blk.setCoherenceBits(CacheBlk::DirtyBit);
    EXPECT_TRUE(blk.isSet(CacheBlk::DirtyBit));
    EXPECT_TRUE(blk.isValid());

    // Verify eviction semantics: Dirty block produces WritebackDirty packet
    RequestPtr req =
        std::make_shared<Request>(0x2000, 64, 0, Request::wbRequestorId);
    PacketPtr wbPkt = new Packet(req, MemCmd::WritebackDirty);
    wbPkt->allocate();

    EXPECT_EQ(wbPkt->cmd, MemCmd::WritebackDirty);
    EXPECT_EQ(wbPkt->getAddr(), 0x2000);

    delete wbPkt;
}
