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
#include "mem/cache/compressors/base_delta.hh"
#include "mem/cache/compressors/multi.hh"
#include "mem/cache/compressors/repeated_qwords.hh"
#include "mem/cache/compressors/zero.hh"
#include "params/Base16Delta8.hh"
#include "params/Base64Delta8.hh"
#include "params/MultiCompressor.hh"
#include "params/RepeatedQwordsCompressor.hh"
#include "params/ZeroCompressor.hh"
#include "sim/root.hh"

namespace gem5 {
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace gem5::compression;

class MultiCompressorTest : public ::testing::Test
{
  protected:
    std::unique_ptr<Multi> multi;
    Zero *zeroComp;
    RepeatedQwords *rqComp;
    Base16Delta8 *bdiComp;

    uint64_t zeroLine[8];
    uint64_t repeatedLine[8];
    uint64_t deltaLine[8];
    uint64_t randomLine[8];

    void SetUp() override
    {
        // Zero line: all 0s
        std::memset(zeroLine, 0, sizeof(zeroLine));

        // Repeated line: 8 identical qwords with large 16-bit deltas
        for (int i = 0; i < 8; i++) {
            repeatedLine[i] = 0x123456789ABCDEF0ULL;
        }

        // Delta line: consecutive 64-bit values with small byte deltas
        for (int i = 0; i < 8; i++) {
            deltaLine[i] = i * 2;
        }

        // Random line: uncompressible
        randomLine[0] = 0x1122334455667788ULL;
        randomLine[1] = 0x99AABBCCDDEEFF00ULL;
        randomLine[2] = 0x0123456789ABCDEFULL;
        randomLine[3] = 0xFEDCBA9876543210ULL;
        randomLine[4] = 0x1234567812345678ULL;
        randomLine[5] = 0x8765432187654321ULL;
        randomLine[6] = 0xA1B2C3D4E5F60718ULL;
        randomLine[7] = 0x9F8E7D6C5B4A3928ULL;
    }

    void createMulti(unsigned threshold, unsigned probe_interval)
    {
        ZeroCompressorParams zero_p;
        zero_p.eventq_index = 0;
        zero_p.block_size = 64;
        zero_p.chunk_size_bits = 64;
        zero_p.size_threshold_percentage = 100;
        zero_p.comp_chunks_per_cycle = 8;
        zero_p.comp_extra_latency = Cycles(1);
        zero_p.decomp_chunks_per_cycle = 8;
        zero_p.decomp_extra_latency = Cycles(1);
        zero_p.dictionary_size = 64;
        zeroComp = new Zero(zero_p);

        RepeatedQwordsCompressorParams rq_p;
        rq_p.eventq_index = 0;
        rq_p.block_size = 64;
        rq_p.chunk_size_bits = 64;
        rq_p.size_threshold_percentage = 100;
        rq_p.comp_chunks_per_cycle = 8;
        rq_p.comp_extra_latency = Cycles(2);
        rq_p.decomp_chunks_per_cycle = 8;
        rq_p.decomp_extra_latency = Cycles(2);
        rq_p.dictionary_size = 64;
        rqComp = new RepeatedQwords(rq_p);

        Base16Delta8Params bdi_p;
        bdi_p.eventq_index = 0;
        bdi_p.block_size = 64;
        bdi_p.chunk_size_bits = 16;
        bdi_p.size_threshold_percentage = 100;
        bdi_p.comp_chunks_per_cycle = 8;
        bdi_p.comp_extra_latency = Cycles(3);
        bdi_p.decomp_chunks_per_cycle = 8;
        bdi_p.decomp_extra_latency = Cycles(3);
        bdi_p.dictionary_size = 64;
        bdiComp = new Base16Delta8(bdi_p);

        MultiCompressorParams multi_p;
        multi_p.eventq_index = 0;
        multi_p.block_size = 64;
        multi_p.chunk_size_bits = 32;
        multi_p.size_threshold_percentage = 100;
        multi_p.comp_chunks_per_cycle = 0;
        multi_p.comp_extra_latency = Cycles(1);
        multi_p.decomp_chunks_per_cycle = 0;
        multi_p.decomp_extra_latency = Cycles(1);
        multi_p.encoding_in_tags = false;
        multi_p.unpromising_threshold = threshold;
        multi_p.probe_interval = probe_interval;
        multi_p.compressors = {zeroComp, rqComp, bdiComp};

        zeroComp->regStats();
        rqComp->regStats();
        bdiComp->regStats();

        multi = std::make_unique<Multi>(multi_p);
        multi->regStats();
    }
};

/**
 * Test compressor selection and decompression/compression latency accounting.
 */
TEST_F(MultiCompressorTest, SelectionAndLatency)
{
    createMulti(3, 10);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Zero line should select ZeroCompressor (index 0)
    auto comp_data = multi->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // Decompression latency must include winner's decomp extra latency (1) + Multi extra latency (1) + chunk cycles
    ASSERT_GE((uint64_t)decomp_lat, 2);
    // Compression latency must include max sub-compressor latency + Multi comp extra latency (1)
    ASSERT_GE((uint64_t)comp_lat, 2);
}

/**
 * Test per-compressor effectiveness tracking and persistent unpromising skipping.
 */
TEST_F(MultiCompressorTest, EffectivenessTrackingAndSkipping)
{
    createMulti(3, 100);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // ZeroCompressor (index 0) is initially promising
    ASSERT_FALSE(multi->isCompressorUnpromising(0));
    ASSERT_EQ(multi->getConsecutiveFailures(0), 0);

    // Compress repeated line 3 times. ZeroCompressor cannot compress repeatedLine.
    for (int i = 0; i < 3; i++) {
        multi->compress(repeatedLine, comp_lat, decomp_lat);
    }

    // ZeroCompressor (index 0) has failed 3 consecutive times and should be marked unpromising
    ASSERT_GE(multi->getConsecutiveFailures(0), 3);
    ASSERT_TRUE(multi->isCompressorUnpromising(0));

    // RepeatedQwordsCompressor (index 1) succeeded, so it must remain promising
    ASSERT_FALSE(multi->isCompressorUnpromising(1));
    ASSERT_EQ(multi->getConsecutiveFailures(1), 0);
}

/**
 * Test fallback behavior when active sub-compressors fail.
 */
TEST_F(MultiCompressorTest, FallbackOnActiveFailure)
{
    createMulti(2, 100);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Compress repeatedLine 2 times.
    // ZeroCompressor (0) fails 2 times -> unpromising.
    // RepeatedQwords (1) succeeds 2 times -> active.
    // Base16Delta8 (2) fails 2 times (16 bytes > 12.8 bytes threshold) -> unpromising.
    for (int i = 0; i < 2; i++) {
        multi->compress(repeatedLine, comp_lat, decomp_lat);
    }

    ASSERT_TRUE(multi->isCompressorUnpromising(2));
    ASSERT_FALSE(multi->isCompressorUnpromising(1));

    // Now compress deltaLine. Active compressor (index 1 RepeatedQwords) fails on deltaLine.
    // Fallback pass evaluates skipped Base16Delta8 (index 2).
    auto comp_data = multi->compress(deltaLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // Base16Delta8 (index 2) should succeed, be selected as best, and recover from unpromising state
    ASSERT_FALSE(multi->isCompressorUnpromising(2));
    ASSERT_EQ(multi->getConsecutiveFailures(2), 0);
}

/**
 * Test fallback behavior when all sub-compressors are marked unpromising.
 */
TEST_F(MultiCompressorTest, AllUnpromisingFallback)
{
    createMulti(2, 100);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Compress randomLine 2 times. All compressors fail 2 times and become unpromising.
    for (int i = 0; i < 2; i++) {
        multi->compress(randomLine, comp_lat, decomp_lat);
    }
    for (unsigned i = 0; i < multi->getCompressors().size(); i++) {
        ASSERT_TRUE(multi->isCompressorUnpromising(i));
    }

    // Call compress again with zeroLine.
    // Fallback logic for all-unpromising state forces evaluation of sub-compressors.
    auto comp_data = multi->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // ZeroCompressor (index 0) succeeds and recovers
    ASSERT_FALSE(multi->isCompressorUnpromising(0));
    ASSERT_EQ(multi->getConsecutiveFailures(0), 0);
}

/**
 * Test periodic probing of skipped sub-compressors.
 */
TEST_F(MultiCompressorTest, PeriodicProbing)
{
    // Threshold = 2, Probe interval = 4
    createMulti(2, 4);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Compress repeatedLine 2 times to mark ZeroCompressor (index 0) unpromising
    multi->compress(repeatedLine, comp_lat, decomp_lat); // Call 1
    multi->compress(repeatedLine, comp_lat, decomp_lat); // Call 2
    ASSERT_TRUE(multi->isCompressorUnpromising(0));

    // Call 3: ZeroCompressor remains skipped
    multi->compress(repeatedLine, comp_lat, decomp_lat); // Call 3

    // Call 4: Probe interval (totalCompressions = 4). Probing evaluates ZeroCompressor.
    // Pass zeroLine so ZeroCompressor succeeds and recovers.
    multi->compress(zeroLine, comp_lat, decomp_lat); // Call 4
    ASSERT_FALSE(multi->isCompressorUnpromising(0));
}

/**
 * Test decompression correctness even when a compressor was skipped.
 */
TEST_F(MultiCompressorTest, DecompressionCorrectness)
{
    createMulti(2, 100);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Compress zeroLine with ZeroCompressor (index 0)
    auto comp_data = multi->compress(zeroLine, comp_lat, decomp_lat);
    ASSERT_NE(comp_data, nullptr);

    // Mark ZeroCompressor unpromising
    for (int i = 0; i < 2; i++) {
        multi->compress(repeatedLine, comp_lat, decomp_lat);
    }
    ASSERT_TRUE(multi->isCompressorUnpromising(0));

    // Decompress the previously created comp_data
    uint64_t decompLine[8];
    multi->decompress(comp_data.get(), decompLine);

    // Verify decompressed data matches original zeroLine
    ASSERT_EQ(std::memcmp(zeroLine, decompLine, sizeof(zeroLine)), 0);
}
