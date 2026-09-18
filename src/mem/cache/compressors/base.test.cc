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

#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

namespace
{

class TestCompressorData : public Base::CompressionData
{
  public:
    TestCompressorData() : Base::CompressionData() {}
};

class TestCompressor : public Base
{
  public:
    using Base::compress;
    std::size_t targetCompressedSizeBits;

    TestCompressor(const BaseCacheCompressorParams &p)
        : Base(p), targetCompressedSizeBits(512)
    {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        auto comp_data = std::make_unique<TestCompressorData>();
        comp_data->setSizeBits(targetCompressedSizeBits);
        comp_lat = Cycles(1);
        decomp_lat = Cycles(1);
        return comp_data;
    }

    void
    decompress(const CompressionData *comp_data, uint64_t *cache_line) override
    {
        std::memset(cache_line, 0, blkSize);
    }
};

} // namespace

class BaseCompressorHysteresisTest : public ::testing::Test
{
  protected:
    uint64_t dummyLine[8];

    void
    SetUp() override
    {
        std::memset(dummyLine, 0, sizeof(dummyLine));
    }

    BaseCacheCompressorParams
    createParams(bool enable_bypass, float high_thresh, float low_thresh,
                 float alpha, unsigned sample_interval = 1)
    {
        BaseCacheCompressorParams p;
        std::memset(&p, 0, sizeof(p));
        p.eventq_index = 0;
        p.block_size = 64;
        p.chunk_size_bits = 64;
        p.size_threshold_percentage = 100;
        p.comp_chunks_per_cycle = 8;
        p.comp_extra_latency = Cycles(0);
        p.decomp_chunks_per_cycle = 8;
        p.decomp_extra_latency = Cycles(0);
        p.enable_adaptive_bypass = enable_bypass;
        p.latency_breakeven_threshold = 1.0;
        p.sampling_interval = sample_interval;
        p.ema_alpha = alpha;
        p.hysteresis_high_threshold = high_thresh;
        p.hysteresis_low_threshold = low_thresh;
        return p;
    }
};

TEST_F(BaseCompressorHysteresisTest, HysteresisStateTransitions)
{
    // high threshold = 1.25, low threshold = 1.50, alpha = 0.5,
    // sampling_interval = 1
    auto p = createParams(true, 1.25, 1.50, 0.5, 1);
    TestCompressor comp(p);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Initial compress with poor ratio (target size 512 bits -> 512/512 = 1.0x
    // ratio)
    comp.targetCompressedSizeBits = 512;
    comp.compress(dummyLine, comp_lat, decomp_lat);

    // After 1st sample, observed ratio = 1.0 < high threshold (1.25) => bypass
    // activated Second request with ratio still 1.0x -> should bypass
    comp.compress(dummyLine, comp_lat, decomp_lat);
    EXPECT_EQ((uint64_t)comp_lat, 0);

    // Now present good compressed line (256 bits -> 512/256 = 2.0x ratio)
    comp.targetCompressedSizeBits = 256;
    // Request 3: EMA updates uncomp=512, comp = 0.5*256 + 0.5*512 = 384 =>
    // observed ratio = 512/384 = 1.33x Since bypass was active, 1.33x < low
    // threshold (1.50x) => bypass remains ACTIVE!
    comp.compress(dummyLine, comp_lat, decomp_lat);
    EXPECT_EQ((uint64_t)comp_lat, 0); // Bypassed because 1.33x < 1.50x

    // Request 4: good line again. EMA updates comp = 0.5*256 + 0.5*384 = 320
    // => observed ratio for NEXT request will be 512/320 = 1.60x.
    // At start of Request 4, observed ratio was 1.33x (< 1.50x), so bypass is
    // still ACTIVE!
    comp.compress(dummyLine, comp_lat, decomp_lat);
    EXPECT_EQ((uint64_t)comp_lat, 0);

    // Request 5: At start of Request 5, observed ratio is 1.60x (> 1.50x low
    // threshold). Bypass is RE-DISABLED / compression active!
    comp.compress(dummyLine, comp_lat, decomp_lat);
    EXPECT_GT((uint64_t)comp_lat, 0); // Compression active!
}

TEST_F(BaseCompressorHysteresisTest, BaselineBehaviorWhenThresholdsEqual)
{
    // high threshold = 1.0, low threshold = 1.0, alpha = 1.0
    auto p = createParams(true, 1.0, 1.0, 1.0, 1);
    TestCompressor comp(p);
    comp.regStats();

    Cycles comp_lat(0), decomp_lat(0);

    // Poor ratio (512 bits -> 1.0x)
    comp.targetCompressedSizeBits = 512;
    comp.compress(dummyLine, comp_lat, decomp_lat);

    // Next request: ratio 1.0x is not < 1.0x, so not bypassed
    comp.targetCompressedSizeBits =
        600; // > sizeThreshold => failed compression (512 bits)
    comp.compress(dummyLine, comp_lat, decomp_lat);

    // Good ratio (256 bits -> 2.0x)
    comp.targetCompressedSizeBits = 256;
    comp.compress(dummyLine, comp_lat, decomp_lat);
    EXPECT_GT((uint64_t)comp_lat, 0);
}
