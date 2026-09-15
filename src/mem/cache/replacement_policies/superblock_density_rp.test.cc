/*
 * Copyright (c) 2026
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

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <cassert>
#include <memory>
#include <vector>

#include "mem/cache/replacement_policies/superblock_density_rp.hh"
#include "mem/cache/tags/super_blk.hh"
#include "params/SuperblockDensityRP.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

/// Common fixture for SuperblockDensityRP tests
class SuperblockDensityRPTestF : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

  public:
    std::shared_ptr<replacement_policy::SuperblockDensity> rp;

    SuperblockDensityRPTestF()
    {
        Gem5Internal::_curTickPtr = &mockTick;
        SuperblockDensityRPParams params;
        params.eventq_index = 0;
        rp = std::make_shared<replacement_policy::SuperblockDensity>(params);
    }
};

/// Helper structure representing a SuperBlk with attached sub-blocks for testing
struct TestSuperBlock
{
    SuperBlk sb;
    std::vector<CompressionBlk> subBlks;

    TestSuperBlock(unsigned num_sub_blks = 4) : subBlks(num_sub_blks)
    {
        sb.setBlkSize(64);
        sb.blks.resize(num_sub_blks);
        for (unsigned k = 0; k < num_sub_blks; ++k) {
            sb.blks[k] = &subBlks[k];
            subBlks[k].setSectorBlock(&sb);
            subBlks[k].setSectorOffset(k);
            subBlks[k].registerTagExtractor([](Addr addr) { return addr; });
        }
        sb.registerTagExtractor([](Addr addr) { return addr; });
    }

    void setValidCount(unsigned count)
    {
        for (unsigned k = 0; k < subBlks.size(); ++k) {
            if (k < count) {
                if (!subBlks[k].isValid()) {
                    subBlks[k].insert({0x1000, false});
                    subBlks[k].setSizeBits(64);
                }
            } else {
                if (subBlks[k].isValid()) {
                    subBlks[k].invalidate();
                }
            }
        }
    }
};

/// Test that instantiating an entry generates non-null replacement data
TEST_F(SuperblockDensityRPTestF, InstantiatedEntry)
{
    const auto repl_data = rp->instantiateEntry();
    ASSERT_NE(repl_data, nullptr);
}

/// Test single candidate selection
TEST_F(SuperblockDensityRPTestF, GetVictim1Candidate)
{
    TestSuperBlock tsb;
    tsb.sb.replacementData = rp->instantiateEntry();
    tsb.setValidCount(1);

    ReplacementCandidates candidates;
    candidates.push_back(&tsb.sb);

    ASSERT_EQ(rp->getVictim(candidates), &tsb.sb);
}

/// Test that candidates with lower valid sub-block counts are prioritized for eviction
/// over candidates with higher valid sub-block counts regardless of recency differences
TEST_F(SuperblockDensityRPTestF, GetVictimDensityAware)
{
    constexpr unsigned num_candidates = 4;
    std::vector<std::unique_ptr<TestSuperBlock>> superblocks;
    ReplacementCandidates candidates;

    // candidate 0: 4 valid sub-blocks, touched at tick 100
    // candidate 1: 3 valid sub-blocks, touched at tick 200
    // candidate 2: 1 valid sub-block,  touched at tick 400 (most recent)
    // candidate 3: 2 valid sub-blocks, touched at tick 300
    std::vector<unsigned> valid_counts = {4, 3, 1, 2};

    for (unsigned i = 0; i < num_candidates; ++i) {
        auto tsb = std::make_unique<TestSuperBlock>();
        tsb->setValidCount(valid_counts[i]);
        tsb->sb.replacementData = rp->instantiateEntry();
        candidates.push_back(&tsb->sb);
        superblocks.push_back(std::move(tsb));
    }

    // Set last touch ticks explicitly
    mockTick = 100;
    rp->touch(superblocks[0]->sb.replacementData);

    mockTick = 200;
    rp->touch(superblocks[1]->sb.replacementData);

    mockTick = 400; // most recent
    rp->touch(superblocks[2]->sb.replacementData);

    mockTick = 300;
    rp->touch(superblocks[3]->sb.replacementData);

    // Verify valid counts
    ASSERT_EQ(superblocks[0]->sb.getNumValid(), 4);
    ASSERT_EQ(superblocks[1]->sb.getNumValid(), 3);
    ASSERT_EQ(superblocks[2]->sb.getNumValid(), 1);
    ASSERT_EQ(superblocks[3]->sb.getNumValid(), 2);

    // Candidate 2 has only 1 valid sub-block (lowest density)
    // It must be chosen as victim despite being touched most recently (tick 400).
    ASSERT_EQ(rp->getVictim(candidates), &superblocks[2]->sb);
}

/// Test that recency timestamps are used as secondary tie-breakers
/// when multiple candidate superblocks have equal valid sub-block counts
TEST_F(SuperblockDensityRPTestF, GetVictimRecencyTieBreaker)
{
    constexpr unsigned num_candidates = 3;
    std::vector<std::unique_ptr<TestSuperBlock>> superblocks;
    ReplacementCandidates candidates;

    // All superblocks have 1 valid sub-block
    for (unsigned i = 0; i < num_candidates; ++i) {
        auto tsb = std::make_unique<TestSuperBlock>();
        tsb->setValidCount(1);
        tsb->sb.replacementData = rp->instantiateEntry();
        candidates.push_back(&tsb->sb);
        superblocks.push_back(std::move(tsb));
    }

    // candidate 0 touched at tick 100 (oldest)
    // candidate 1 touched at tick 300
    // candidate 2 touched at tick 200
    mockTick = 100;
    rp->touch(superblocks[0]->sb.replacementData);

    mockTick = 300;
    rp->touch(superblocks[1]->sb.replacementData);

    mockTick = 200;
    rp->touch(superblocks[2]->sb.replacementData);

    // All candidates have 1 valid sub-block, so candidate 0 (tick 100) should be victim
    ASSERT_EQ(rp->getVictim(candidates), &superblocks[0]->sb);
}


/// Test fallback to standard LRU recency evaluation when candidate entries
/// are not SuperBlk instances
TEST_F(SuperblockDensityRPTestF, GetVictimNonSuperBlockFallback)
{
    constexpr unsigned num_candidates = 3;
    std::vector<ReplaceableEntry> entries(num_candidates);
    ReplacementCandidates candidates;

    for (unsigned i = 0; i < num_candidates; ++i) {
        entries[i].replacementData = rp->instantiateEntry();
        candidates.push_back(&entries[i]);
    }

    mockTick = 300;
    rp->touch(entries[0].replacementData);

    mockTick = 100; // oldest
    rp->touch(entries[1].replacementData);

    mockTick = 200;
    rp->touch(entries[2].replacementData);

    // Entries are standard ReplaceableEntry (non-SuperBlk)
    // Should select entry 1 as victim (tick 100)
    ASSERT_EQ(rp->getVictim(candidates), &entries[1]);
}

typedef SuperblockDensityRPTestF SuperblockDensityRPDeathTest;

TEST_F(SuperblockDensityRPDeathTest, NoCandidates)
{
    ReplacementCandidates candidates;
    ASSERT_DEATH(rp->getVictim(candidates), "");
}
