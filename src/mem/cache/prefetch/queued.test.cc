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

#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "mem/cache/prefetch/queued.hh"

static gem5::Tick mockTick = 0;
namespace gem5
{
namespace Gem5Internal
{
__thread Tick *_curTickPtr = &mockTick;
}
}

using namespace gem5;
using namespace prefetch;

TEST(CompressionConfidenceTableTest, InitialAndSaturatingCounter)
{
    Queued::CompressionConfidenceTable table;
    // enable = true, threshold = 4, entries = 16, counter_bits = 3 (0..7)
    table.init(true, 4, 16, 3);

    Addr pc1 = 0x1000;

    // Unknown PC defaults to true (allow)
    EXPECT_TRUE(table.check(pc1));

    // Update with uncompressed fill: initial confidence = 3.
    // 3 < threshold(4) -> check returns false.
    table.update(pc1, false);
    EXPECT_FALSE(table.check(pc1));

    // Multiple uncompressed fills saturate at 0
    for (int i = 0; i < 10; ++i) {
        table.update(pc1, false);
    }
    EXPECT_FALSE(table.check(pc1));

    // Compressed fills increase confidence up to max (7)
    for (int i = 0; i < 10; ++i) {
        table.update(pc1, true);
    }
    // Now confidence = 7 >= threshold 4
    EXPECT_TRUE(table.check(pc1));
}

TEST(CompressionConfidenceTableTest, ThresholdFiltering)
{
    Queued::CompressionConfidenceTable table;
    // threshold = 6
    table.init(true, 6, 16, 3);

    Addr pc1 = 0x2000;

    // First compressed fill creates entry with confidence = init_counter + 1 = 5.
    table.update(pc1, true);
    // 5 < threshold 6 -> false
    EXPECT_FALSE(table.check(pc1));

    // Another compressed fill increases 5 -> 6.
    table.update(pc1, true);
    // 6 >= threshold 6 -> true
    EXPECT_TRUE(table.check(pc1));
}

TEST(CompressionConfidenceTableTest, DisabledTable)
{
    Queued::CompressionConfidenceTable table;
    table.init(false, 4, 16, 3);

    Addr pc1 = 0x3000;
    table.update(pc1, false);

    // Table disabled -> check always returns true
    EXPECT_TRUE(table.check(pc1));
}
