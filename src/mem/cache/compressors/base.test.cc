/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/tags/super_blk.hh"
#include "mem/packet.hh"
#include "mem/request.hh"

using namespace gem5;
using namespace compression;

namespace gem5
{
namespace compression
{

class DummyCompressor : public Base
{
  public:
    Cycles compLat = Cycles(2);
    Cycles decompLat = Cycles(3);

    DummyCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        comp_lat = compLat;
        decomp_lat = decompLat;
        auto comp_data = std::make_unique<CompressionData>();
        comp_data->setSizeBits(256);
        return comp_data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {}
};

TEST(BaseCompressorTest, DecompressionLatencyQuery)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(4));

    // Uncompressed block (64 bytes = 512 bits)
    blk.setSizeBits(512);
    EXPECT_FALSE(blk.isCompressed());

    // Compressed block (256 bits)
    blk.setSizeBits(256);
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(4));
}

TEST(BaseCompressorTest, TargetCompressionLatencyRead)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256); // Compressed block

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet readPkt(req, MemCmd::ReadReq);

    // Read target on compressed block should return decompression latency
    EXPECT_TRUE(blk.isCompressed());
    EXPECT_EQ(blk.getDecompressionLatency(), Cycles(5));
}

TEST(BaseCompressorTest, TargetCompressionLatencyWholeLineWrite)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256);

    RequestPtr req = std::make_shared<Request>(0x1000, 64, 0, 0);
    Packet writePkt(req, MemCmd::WriteReq);

    // Whole line write (64 bytes out of 64 bytes) bypasses decompression
    EXPECT_TRUE(writePkt.isWholeLineWrite(64));
}

TEST(BaseCompressorTest, TargetCompressionLatencyPartialWrite)
{
    CompressionBlk blk;
    blk.setDecompressionLatency(Cycles(5));
    blk.setSizeBits(256);

    RequestPtr req = std::make_shared<Request>(0x1000, 8, 0, 0);
    Packet writePkt(req, MemCmd::WriteReq);

    // Partial write (8 bytes out of 64 bytes) requires decompression +
    // recompression
    EXPECT_FALSE(writePkt.isWholeLineWrite(64));
}

} // namespace compression
} // namespace gem5
