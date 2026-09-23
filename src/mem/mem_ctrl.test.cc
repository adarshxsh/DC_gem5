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

#include "mem/mem_interface.hh"

namespace gem5
{
namespace memory
{

class DummyMemInterface : public MemInterface
{
  public:
    double customDrainFactor = 1.0;

    DummyMemInterface() : MemInterface(createParams()) {}

    static MemInterfaceParams
    createParams()
    {
        MemInterfaceParams p;
        p.write_buffer_size = 64;
        p.read_buffer_size = 32;
        p.devices_per_rank = 1;
        p.burst_length = 8;
        p.device_bus_width = 8;
        p.device_size = 1024;
        p.device_rowbuffer_size = 256;
        p.ranks_per_channel = 1;
        p.banks_per_rank = 8;
        p.tCK = 1000;
        p.tCS = 1000;
        p.tBURST = 1000;
        p.tRTW = 1000;
        p.tWTR = 1000;
        p.addr_mapping = enums::RoRaBaCoCh;
        return p;
    }

    void
    setupRank(const uint8_t rank, const bool is_read) override
    {}
    bool
    allRanksDrained() const override
    {
        return true;
    }
    Tick
    commandOffset() const override
    {
        return tBURST;
    }
    bool
    burstReady(MemPacket *pkt) const override
    {
        return true;
    }
    bool
    isBusy(bool read_queue_empty, bool all_writes_nvm) override
    {
        return false;
    }
    std::pair<MemPacketQueue::iterator, Tick>
    chooseNextFRFCFS(MemPacketQueue &queue, Tick min_col_at) const override
    {
        return std::make_pair(queue.end(), 0);
    }
    std::pair<Tick, Tick>
    doBurstAccess(MemPacket *pkt, Tick next_burst_at,
                  const std::vector<MemPacketQueue> &queue) override
    {
        return std::make_pair(curTick(), curTick() + tBURST);
    }
    void
    drainRanks() override
    {}
    void
    suspend() override
    {}
    void
    startup() override
    {}

    double
    getWriteDrainFactor() const override
    {
        return customDrainFactor;
    }
};

TEST(MemCtrlTest, DefaultWriteDrainFactor)
{
    DummyMemInterface mem;
    EXPECT_DOUBLE_EQ(mem.getWriteDrainFactor(), 1.0);
}

TEST(MemCtrlTest, WritePressureCalculation)
{
    DummyMemInterface mem;
    mem.writeQueueSize = 16; // 16 out of 64 entries = 25% occupancy
    mem.customDrainFactor = 1.0;
    EXPECT_DOUBLE_EQ(mem.getWritePressure(), 0.25);

    // Weighted pressure with drain factor 2.0 (e.g. NVM latency penalty)
    mem.customDrainFactor = 2.0;
    EXPECT_DOUBLE_EQ(mem.getWritePressure(), 0.50);
}

TEST(MemCtrlTest, ZeroWriteBufferSize)
{
    DummyMemInterface mem;
    const_cast<uint32_t &>(mem.writeBufferSize) = 0;
    mem.writeQueueSize = 10;
    EXPECT_DOUBLE_EQ(mem.getWritePressure(), 0.0);
}

} // namespace memory
} // namespace gem5
