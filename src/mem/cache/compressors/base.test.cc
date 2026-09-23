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

#include "base/output.hh"
#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class MockCompressor : public Base
{
  public:
    MockCompressor(const BaseCacheCompressorParams &p)
        : Base(p)
    {}

    double overridePressure = 0.0;

    double getMemQueuePressure() const override
    {
        return overridePressure;
    }

    std::unique_ptr<CompressionData> compress(
        const std::vector<Chunk>& chunks, Cycles& comp_lat,
        Cycles& decomp_lat) override
    {
        comp_lat = Cycles(2);
        decomp_lat = Cycles(2);
        auto comp_data = std::make_unique<CompressionData>();
        comp_data->setSizeBits(128); // Compressed to 128 bits
        return comp_data;
    }

    void decompress(const CompressionData* comp_data,
                    uint64_t* cache_line) override
    {}
};

TEST(BaseCompressorTest, MemPressureBypassTransitions)
{
    BaseCacheCompressorParams p{};
    p.name = "comp_pressure_test";
    p.eventq_index = 0;
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(2);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(2);
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;
    p.enable_mem_pressure_bypass = true;
    p.mem_pressure_threshold = 0.75;
    p.mem_ctrl = nullptr;

    auto test_comp = std::make_unique<MockCompressor>(p);
    test_comp->regStats();
    Base *comp = test_comp.get();

    uint64_t data[8];
    std::memset(data, 0, sizeof(data));

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Phase 1: Low memory pressure (0.20 < 0.75 threshold) -> Compression active (128 bits, 2 cycles)
    test_comp->overridePressure = 0.20;
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    auto comp_data_low = comp->compress(data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_EQ(comp_data_low->getSizeBits(), 128);

    // Phase 2: High memory pressure (0.80 >= 0.75 threshold) -> Compression bypassed (512 bits, 0 cycles)
    test_comp->overridePressure = 0.80;
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    auto comp_data_high = comp->compress(data, comp_lat, decomp_lat);
    EXPECT_EQ(comp_lat, Cycles(0));
    EXPECT_EQ(decomp_lat, Cycles(0));
    EXPECT_EQ(comp_data_high->getSizeBits(), 64 * 8); // uncompressed size (512 bits)

    // Phase 3: Pressure relief (0.30 < 0.75 threshold) -> Compression resumes
    test_comp->overridePressure = 0.30;
    comp_lat = Cycles(0);
    decomp_lat = Cycles(0);
    auto comp_data_relief = comp->compress(data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_EQ(comp_data_relief->getSizeBits(), 128);
}

TEST(BaseCompressorTest, MemPressureBypassDisabledBehavior)
{
    BaseCacheCompressorParams p{};
    p.name = "comp_disabled_test";
    p.eventq_index = 0;
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(2);
    p.decomp_chunks_per_cycle = 8;
    p.decomp_extra_latency = Cycles(2);
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;
    p.enable_mem_pressure_bypass = false; // Disabled
    p.mem_pressure_threshold = 0.75;
    p.mem_ctrl = nullptr;

    auto test_comp = std::make_unique<MockCompressor>(p);
    test_comp->regStats();
    Base *comp = test_comp.get();

    uint64_t data[8];
    std::memset(data, 0, sizeof(data));

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Even with 95% queue pressure, when enable_mem_pressure_bypass = false, compression is NOT bypassed
    test_comp->overridePressure = 0.95;
    auto comp_data = comp->compress(data, comp_lat, decomp_lat);
    EXPECT_GT(comp_lat, Cycles(0));
    EXPECT_EQ(comp_data->getSizeBits(), 128);
}
