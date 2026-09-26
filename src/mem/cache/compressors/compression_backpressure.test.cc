/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/zero.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

class TestZeroCompressor : public Zero
{
  public:
    using Base::compress;
    using Zero::Zero;
};

TEST(CompressionBackpressureTest, PacketFlagSignaling)
{
    RequestPtr req = std::make_shared<Request>();
    req->setPaddr(0x1000);
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);

    EXPECT_FALSE(pkt->isCompressionThrottled());

    pkt->setCompressionThrottled();
    EXPECT_TRUE(pkt->isCompressionThrottled());

    pkt->clearCompressionThrottled();
    EXPECT_FALSE(pkt->isCompressionThrottled());

    delete pkt;
}

TEST(CompressionBackpressureTest, CopyResponderFlagsPreservesThrottle)
{
    RequestPtr req = std::make_shared<Request>();
    req->setPaddr(0x2000);
    PacketPtr reqPkt = new Packet(req, MemCmd::ReadReq);
    PacketPtr respPkt = new Packet(req, MemCmd::ReadResp);

    respPkt->setCompressionThrottled();
    EXPECT_TRUE(respPkt->isCompressionThrottled());

    reqPkt->copyResponderFlags(respPkt);
    EXPECT_TRUE(reqPkt->isCompressionThrottled());

    delete reqPkt;
    delete respPkt;
}

TEST(CompressionBackpressureTest, AdaptiveBypassThrottlingState)
{
    ZeroCompressorParams p{};
    p.name = "zero_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.dictionary_size = 8;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 2.0;
    p.sampling_interval = 1;
    p.decay_shift = 0;

    TestZeroCompressor compressor(p);
    compressor.regStats();

    EXPECT_FALSE(compressor.isCompressionBypassed());

    uint64_t non_zero_data[8] = {
        0x0123456789ABCDEF, 0xFEDCBA9876543210,
        0x1122334455667788, 0x99AABBCCDDEEFF00,
        0x0F1E2D3C4B5A6978, 0x8796A5B4C3D2E1F0,
        0xA5A5A5A55A5A5A5A, 0x5A5A5A5AA5A5A5A5
    };

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    for (int i = 0; i < 5; ++i) {
        compressor.compress(non_zero_data, comp_lat, decomp_lat);
    }

    EXPECT_TRUE(compressor.isCompressionBypassed());
}
