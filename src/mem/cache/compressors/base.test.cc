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

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "mem/cache/compressors/base.hh"
#include "params/BaseCacheCompressor.hh"
#include "params/SimObject.hh"
#include "sim/probe/probe.hh"
#include "sim/root.hh"
#include "sim/sim_object.hh"

namespace gem5
{
Root *Root::_root = nullptr;

using namespace compression;

class TestCompressor : public Base
{
  public:
    TestCompressor(const BaseCacheCompressorParams &p) : Base(p) {}

    std::unique_ptr<CompressionData>
    compress(const std::vector<Chunk> &chunks, Cycles &comp_lat,
             Cycles &decomp_lat) override
    {
        auto comp_data = std::make_unique<CompressionData>();
        comp_data->setSizeBits(256);
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

class MockProbeProducer : public SimObject
{
  public:
    ProbePointArg<double> *ppQueuePressure = nullptr;

    MockProbeProducer(const SimObjectParams &p) : SimObject(p)
    {
        ppQueuePressure =
            new ProbePointArg<double>(getProbeManager(), "ppQueuePressure");
    }
};

TEST(BaseCompressorTest, QueuePressureProbeAndThresholdScaling)
{
    BaseCacheCompressorParams p{};
    p.name = "test_compressor";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.size_threshold_percentage = 50;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(1);
    p.enable_adaptive_bypass = true;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;
    p.pressure_sensitivity = 0.5;
    p.max_pressure_threshold = 2.0;

    TestCompressor compressor(p);

    EXPECT_DOUBLE_EQ(compressor.getQueuePressure(), 0.0);
    EXPECT_DOUBLE_EQ(compressor.getEffectiveBreakevenThreshold(), 1.0);

    compressor.updateQueuePressure(0.8);
    EXPECT_DOUBLE_EQ(compressor.getQueuePressure(), 0.8);
    EXPECT_DOUBLE_EQ(compressor.getEffectiveBreakevenThreshold(), 0.6);

    compressor.updateQueuePressure(1.0);
    EXPECT_DOUBLE_EQ(compressor.getQueuePressure(), 1.0);
    EXPECT_DOUBLE_EQ(compressor.getEffectiveBreakevenThreshold(), 0.5);
}

TEST(BaseCompressorTest, PressureProbeSubscription)
{
    SimObjectParams producerP{};
    producerP.name = "test_producer";

    MockProbeProducer producer(producerP);

    BaseCacheCompressorParams compP{};
    compP.name = "test_compressor";
    compP.block_size = 64;
    compP.chunk_size_bits = 32;
    compP.size_threshold_percentage = 50;
    compP.comp_chunks_per_cycle = 1;
    compP.comp_extra_latency = Cycles(1);
    compP.decomp_chunks_per_cycle = 1;
    compP.decomp_extra_latency = Cycles(1);
    compP.enable_adaptive_bypass = true;
    compP.latency_breakeven_threshold = 1.0;
    compP.pressure_sensitivity = 0.5;
    compP.max_pressure_threshold = 2.0;

    TestCompressor compressor(compP);
    compressor.registerQueuePressureProbe(&producer);

    EXPECT_NE(producer.ppQueuePressure, nullptr);
    producer.ppQueuePressure->notify(0.6);

    EXPECT_DOUBLE_EQ(compressor.getQueuePressure(), 0.6);
    EXPECT_DOUBLE_EQ(compressor.getEffectiveBreakevenThreshold(), 0.7);
}

} // namespace gem5
