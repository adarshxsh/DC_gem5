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

#include "mem/cache/compressors/zero.hh"
#include "mem/cache/tags/super_blk.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

TEST(BaseCompressorTest, MemoryQueuePressureDecompressionBypass)
{
    ZeroCompressorParams p{};
    p.eventq_index = 0;
    p.block_size = 64;
    p.chunk_size_bits = 64;
    p.size_threshold_percentage = 100;
    p.comp_chunks_per_cycle = 1;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 1;
    p.decomp_extra_latency = Cycles(2);

    Zero compressor(p);

    // Verify initial pressure level is 0
    EXPECT_EQ(compressor.getMemoryQueuePressure(), 0);
    EXPECT_FALSE(compressor.isMemoryPressureHigh());

    // Create a mock compression block
    CompressionBlk blk;
    blk.setSizeBits(0); // zero block
    blk.setCompressed();
    blk.setDecompressionLatency(Cycles(2));

    // Under normal pressure, getDecompressionLatency should return the block's
    // latency (2 cycles)
    EXPECT_EQ(compressor.getDecompressionLatency(&blk), Cycles(2));

    // Set memory pressure to HIGH (level 3)
    compressor.setMemoryQueuePressure(3);
    EXPECT_TRUE(compressor.isMemoryPressureHigh());

    // Under high memory pressure, getDecompressionLatency should bypass
    // latency (return 0 cycles)
    EXPECT_EQ(compressor.getDecompressionLatency(&blk), Cycles(0));

    // Set memory pressure back to LOW (level 1)
    compressor.setMemoryQueuePressure(1);
    EXPECT_FALSE(compressor.isMemoryPressureHigh());
    EXPECT_EQ(compressor.getDecompressionLatency(&blk), Cycles(2));
}
