/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include "mem/cache/compressors/base.hh"
#include "mem/packet.hh"
#include "params/BaseCacheCompressor.hh"

namespace gem5
{

TEST(PacketPressureTest, FlagBitDefinitions)
{
    EXPECT_EQ(0x00020000, Packet::MEM_PRESSURE_MODERATE);
    EXPECT_EQ(0x00040000, Packet::MEM_PRESSURE_HIGH);

    // Verify bitwise flags fit within responder flags
    EXPECT_TRUE((Packet::RESPONDER_FLAGS & Packet::MEM_PRESSURE_MODERATE) !=
                0);
    EXPECT_TRUE((Packet::RESPONDER_FLAGS & Packet::MEM_PRESSURE_HIGH) != 0);
}

TEST(PacketPressureTest, FlagSetAndClear)
{
    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet pkt(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt.isMemPressureModerate());
    EXPECT_FALSE(pkt.isMemPressureHigh());

    pkt.setMemPressureModerate();
    EXPECT_TRUE(pkt.isMemPressureModerate());
    EXPECT_FALSE(pkt.isMemPressureHigh());

    pkt.clearMemPressureFlags();
    EXPECT_FALSE(pkt.isMemPressureModerate());
    EXPECT_FALSE(pkt.isMemPressureHigh());

    pkt.setMemPressureHigh();
    EXPECT_FALSE(pkt.isMemPressureModerate());
    EXPECT_TRUE(pkt.isMemPressureHigh());

    pkt.clearMemPressureFlags();
    EXPECT_FALSE(pkt.isMemPressureModerate());
    EXPECT_FALSE(pkt.isMemPressureHigh());
}

TEST(PacketPressureTest, CopyResponderFlags)
{
    RequestPtr req1 = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet reqPkt(req1, MemCmd::ReadReq);

    RequestPtr req2 = std::make_shared<Request>(0x2000, 64, 0, 0);
    Packet respPkt(req2, MemCmd::ReadResp);
    respPkt.setMemPressureHigh();

    reqPkt.copyResponderFlags(&respPkt);
    EXPECT_TRUE(reqPkt.isMemPressureHigh());
}

class TestCompressor : public compression::Base
{
  public:
    TestCompressor(const BaseCacheCompressorParams &p) : compression::Base(p)
    {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        return nullptr;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {}
};

TEST(PacketPressureTest, CacheCompressorPolicyReaction)
{
    BaseCacheCompressorParams p{};
    p.name = "test_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 50;
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    TestCompressor compressor(p);

    EXPECT_EQ(compression::Base::PRESSURE_NONE,
              compressor.getMemoryPressureState());
    EXPECT_EQ(0, compressor.getMemoryPressureControlRegister());

    compressor.setMemoryPressure(true, false);
    EXPECT_EQ(compression::Base::PRESSURE_MODERATE,
              compressor.getMemoryPressureState());
    EXPECT_EQ(1, compressor.getMemoryPressureControlRegister());

    compressor.setMemoryPressure(false, true);
    EXPECT_EQ(compression::Base::PRESSURE_HIGH,
              compressor.getMemoryPressureState());
    EXPECT_EQ(2, compressor.getMemoryPressureControlRegister());

    compressor.setMemoryPressure(false, false);
    EXPECT_EQ(compression::Base::PRESSURE_NONE,
              compressor.getMemoryPressureState());
    EXPECT_EQ(0, compressor.getMemoryPressureControlRegister());
}

} // namespace gem5
