/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/mem_ctrl.hh"
#include "mem/packet.hh"
#include "sim/cur_tick.hh"

namespace gem5
{
namespace memory
{

Tick
computeBurstTime(unsigned int compressedSize, uint32_t burstSize,
                 uint32_t burstLength, Tick tBURST)
{
    unsigned int c = compressedSize;
    if (c == 0 || c >= burstSize) {
        return tBURST;
    }

    uint32_t bl = burstLength ? burstLength : 8;
    uint32_t bytes_per_beat = burstSize / bl;
    if (bytes_per_beat == 0) {
        return tBURST;
    }

    uint32_t beats = (c + bytes_per_beat - 1) / bytes_per_beat;
    if (beats < 2) {
        beats = 2;
    } else if (beats % 2 != 0) {
        beats++;
    }
    if (beats > bl) {
        beats = bl;
    }

    return (tBURST * beats) / bl;
}

static Tick testTick = 0;

TEST(CompressedDramBurstTest, PacketCompressedSize)
{
    gem5::Gem5Internal::_curTickPtr = &testTick;
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);
    EXPECT_FALSE(pkt.hasCompressedSize());
    EXPECT_EQ(pkt.getCompressedSize(), 64);

    pkt.setCompressedSize(32);
    EXPECT_TRUE(pkt.hasCompressedSize());
    EXPECT_EQ(pkt.getCompressedSize(), 32);
}

TEST(CompressedDramBurstTest, MemPacketCompressedSize)
{
    gem5::Gem5Internal::_curTickPtr = &testTick;
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->setCompressedSize(24);

    MemPacket mem_pkt(pkt, true, true, 0, 0, 0, 0, 0, 0x1000, 64);
    EXPECT_EQ(mem_pkt.getAddr(), 0x1000);
    EXPECT_EQ(mem_pkt.getSize(), 64);
    EXPECT_EQ(mem_pkt.getCompressedSize(), 24);
    EXPECT_EQ(mem_pkt.compressedSize, 24);

    delete pkt;
}

TEST(CompressedDramBurstTest, DRAMBurstScalingFormula)
{
    uint32_t burstSize = 64;
    uint32_t burstLength = 8;
    Tick tBURST = 4000; // 4000 ticks

    // Full 64B uncompressed -> 8 beats = 4000 ticks
    EXPECT_EQ(computeBurstTime(64, burstSize, burstLength, tBURST), 4000);

    // 32B compressed -> 4 beats = 2000 ticks
    EXPECT_EQ(computeBurstTime(32, burstSize, burstLength, tBURST), 2000);

    // 16B compressed -> 2 beats = 1000 ticks
    EXPECT_EQ(computeBurstTime(16, burstSize, burstLength, tBURST), 1000);

    // 24B compressed -> 3 beats rounded up to 4 beats = 2000 ticks
    EXPECT_EQ(computeBurstTime(24, burstSize, burstLength, tBURST), 2000);

    // 3B compressed -> 1 beat rounded up to min 2 beats = 1000 ticks
    EXPECT_EQ(computeBurstTime(3, burstSize, burstLength, tBURST), 1000);
}

} // namespace memory
} // namespace gem5
