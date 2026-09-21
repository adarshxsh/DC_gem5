/*
 * Copyright (c) 2026 gem5
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

#include <cstring>
#include <memory>
#include <vector>

#include "base/output.hh"
#include "mem/cache/compressors/base.hh"
#include "mem/cache/compressors/zero.hh"
#include "mem/cache/tags/super_blk.hh"
#include "params/BaseCacheCompressor.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class BaseCompressorQueuePressureTest : public ::testing::Test
{
  protected:
    std::unique_ptr<Base> zeroComp;
    uint64_t zeroLine[8];
    uint64_t nonZeroLine[8];

    void SetUp() override
    {
        std::memset(zeroLine, 0, sizeof(zeroLine));
        for (int i = 0; i < 8; i++) {
            nonZeroLine[i] = 0x123456789ABCDEF0ULL + i;
        }

        ZeroCompressorParams zero_p{};
        zero_p.eventq_index = 0;
        zero_p.block_size = 64;
        zero_p.chunk_size_bits = 32;
        zero_p.size_threshold_percentage = 100;
        zero_p.comp_chunks_per_cycle = 1;
        zero_p.comp_extra_latency = Cycles(1);
        zero_p.decomp_chunks_per_cycle = 1;
        zero_p.decomp_extra_latency = Cycles(1);
        zero_p.enable_adaptive_bypass = false;
        zero_p.latency_breakeven_threshold = 1.0;
        zero_p.sampling_interval = 100;
        zero_p.decay_shift = 4;
        zero_p.mem_ctrl = nullptr;

        zeroComp = std::make_unique<Zero>(zero_p);
        zeroComp->regStats();
    }
};

TEST_F(BaseCompressorQueuePressureTest, NormalCompressionWithoutPressure)
{
    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Initial state: memory queue pressure inactive
    EXPECT_FALSE(zeroComp->isMemQueuePressureActive());

    // Compress zero line: should compress to 0 bits
    auto comp_data = zeroComp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data->getSizeBits(), 0);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_GT(decomp_lat, Cycles(0));
}

TEST_F(BaseCompressorQueuePressureTest, BypassCompressionWithPressure)
{
    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Trigger queue pressure notification
    zeroComp->handleQueuePressure(true);
    EXPECT_TRUE(zeroComp->isMemQueuePressureActive());

    // Compress zero line while pressure active: should bypass compression
    auto comp_data = zeroComp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data->getSizeBits(), 64 * 8); // 512 bits uncompressed
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));

    // Clear queue pressure notification
    zeroComp->handleQueuePressure(false);
    EXPECT_FALSE(zeroComp->isMemQueuePressureActive());

    // Compress zero line after pressure cleared: standard compression restored
    auto comp_data_normal = zeroComp->compress(zeroLine, comp_lat, decomp_lat);
    EXPECT_EQ(comp_data_normal->getSizeBits(), 0);
    EXPECT_GT(comp_lat, Cycles(0));
}

TEST_F(BaseCompressorQueuePressureTest, BypassDecompressionWithPressure)
{
    CompressionBlk blk;
    blk.setSizeBits(256); // 2:1 compressed
    blk.setDecompressionLatency(Cycles(3));

    // Without pressure: returns cached decompression latency (3 cycles)
    EXPECT_EQ(zeroComp->getDecompressionLatency(&blk), Cycles(3));

    // With pressure active: returns zero decompression latency
    zeroComp->handleQueuePressure(true);
    EXPECT_EQ(zeroComp->getDecompressionLatency(&blk), Cycles(0));

    // Clear pressure: returns cached decompression latency again
    zeroComp->handleQueuePressure(false);
    EXPECT_EQ(zeroComp->getDecompressionLatency(&blk), Cycles(3));
}
