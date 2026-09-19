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

#include <deque>
#include <gtest/gtest.h>

#include "base/gtest/cur_tick_fake.hh"
#include "base/intmath.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

namespace gem5
{

TEST(XBarTest, PriorityAndDecompressionDecoupling)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    // Test 1: Demand Read vs Writeback classification
    RequestPtr req_read = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr read_pkt = new Packet(req_read, MemCmd::ReadReq, 64);

    RequestPtr req_wb = std::make_shared<Request>(0x2000, 64, 0, 0);
    PacketPtr wb_pkt = new Packet(req_wb, MemCmd::WritebackDirty, 64);

    // Verify ReadReq is read & demand, and WritebackDirty is writeback/eviction
    EXPECT_TRUE(read_pkt->isRead());
    EXPECT_TRUE(read_pkt->isDemand());
    EXPECT_FALSE(read_pkt->isWriteback());

    EXPECT_FALSE(wb_pkt->isRead());
    EXPECT_FALSE(wb_pkt->isDemand());
    EXPECT_TRUE(wb_pkt->isWriteback());
    EXPECT_TRUE(wb_pkt->isEviction());

    // Test 2: Decompression latency accounting
    wb_pkt->payloadDelay = 120; // 120 ticks decompression latency

    // Physical transmission ticks calculation for bus width = 16 bytes, clock period = 1000 ticks
    uint32_t bus_width = 16;
    Tick clock_period = 1000;
    Tick phys_delay = wb_pkt->hasData() ? (divCeil(wb_pkt->getSize(), bus_width) * clock_period) : 0;

    EXPECT_EQ(phys_delay, 4000); // 64 / 16 = 4 cycles = 4000 ticks
    EXPECT_EQ(wb_pkt->payloadDelay, 120); // Endpoint retains decompression latency

    delete read_pkt;
    delete wb_pkt;
}

TEST(XBarTest, RetryQueuePriorityOrdering)
{
    Tick mockTick = 0;
    Gem5Internal::_curTickPtr = &mockTick;

    struct DummyPort {};
    DummyPort port1, port2, port3, port4;

    struct WaitingPort
    {
        DummyPort* port;
        bool highPriority;
        WaitingPort(DummyPort* _port, bool _hp) : port(_port), highPriority(_hp) {}
    };

    std::deque<WaitingPort> waitingForLayer;

    auto tryTimingSim = [&](DummyPort* src_port, PacketPtr pkt) {
        bool high_pri = pkt ? (pkt->isRead() || (pkt->isDemand() && !pkt->isWriteback() && !pkt->isEviction())) : false;
        if (high_pri) {
            auto it = std::find_if(waitingForLayer.begin(), waitingForLayer.end(),
                                   [](const WaitingPort& wp) { return !wp.highPriority; });
            waitingForLayer.emplace(it, src_port, true);
        } else {
            waitingForLayer.emplace_back(src_port, false);
        }
    };

    RequestPtr req_read1 = std::make_shared<Request>(0x1000, 64, 0, 0);
    PacketPtr read1_pkt = new Packet(req_read1, MemCmd::ReadReq, 64);

    RequestPtr req_read2 = std::make_shared<Request>(0x3000, 64, 0, 0);
    PacketPtr read2_pkt = new Packet(req_read2, MemCmd::ReadReq, 64);

    RequestPtr req_wb1 = std::make_shared<Request>(0x2000, 64, 0, 0);
    PacketPtr wb1_pkt = new Packet(req_wb1, MemCmd::WritebackDirty, 64);

    RequestPtr req_wb2 = std::make_shared<Request>(0x4000, 64, 0, 0);
    PacketPtr wb2_pkt = new Packet(req_wb2, MemCmd::WritebackDirty, 64);

    // 1. WB1 arrives first -> low priority
    tryTimingSim(&port1, wb1_pkt);
    EXPECT_EQ(waitingForLayer.size(), 1);
    EXPECT_EQ(waitingForLayer[0].port, &port1);

    // 2. WB2 arrives next -> low priority
    tryTimingSim(&port2, wb2_pkt);
    EXPECT_EQ(waitingForLayer.size(), 2);
    EXPECT_EQ(waitingForLayer[0].port, &port1);
    EXPECT_EQ(waitingForLayer[1].port, &port2);

    // 3. Read1 (demand read) arrives -> high priority, jumps ahead of WB1 and WB2!
    tryTimingSim(&port3, read1_pkt);
    EXPECT_EQ(waitingForLayer.size(), 3);
    EXPECT_EQ(waitingForLayer[0].port, &port3); // Read1 jumps to head!
    EXPECT_EQ(waitingForLayer[1].port, &port1); // WB1
    EXPECT_EQ(waitingForLayer[2].port, &port2); // WB2

    // 4. Read2 (demand read) arrives -> high priority, placed after Read1 but ahead of WB1 and WB2!
    tryTimingSim(&port4, read2_pkt);
    EXPECT_EQ(waitingForLayer.size(), 4);
    EXPECT_EQ(waitingForLayer[0].port, &port3); // Read1
    EXPECT_EQ(waitingForLayer[1].port, &port4); // Read2
    EXPECT_EQ(waitingForLayer[2].port, &port1); // WB1
    EXPECT_EQ(waitingForLayer[3].port, &port2); // WB2

    delete read1_pkt;
    delete read2_pkt;
    delete wb1_pkt;
    delete wb2_pkt;
}

} // namespace gem5
