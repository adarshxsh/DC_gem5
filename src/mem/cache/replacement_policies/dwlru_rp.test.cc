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

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "mem/cache/replacement_policies/dwlru_rp.hh"
#include "mem/cache/tags/sector_blk.hh"
#include "params/DWLRURP.hh"
#include "sim/cur_tick.hh"

using namespace gem5;

class DWLRURPTestF : public ::testing::Test
{
  public:
    Tick mockTick = 1000;
    std::shared_ptr<replacement_policy::DWLRU> rp;

    DWLRURPTestF()
    {
        Gem5Internal::_curTickPtr = &mockTick;
        DWLRURPParams params;
        params.eventq_index = 0;
        params.queue_pressure_threshold = 0.7;
        params.writeback_penalty_weight = 2.0;
        rp = std::make_shared<replacement_policy::DWLRU>(params);
    }
};

TEST_F(DWLRURPTestF, InstantiatedEntry)
{
    const auto repl_data = rp->instantiateEntry();
    ASSERT_NE(repl_data, nullptr);

    auto dw_data =
        std::dynamic_pointer_cast<replacement_policy::DWLRU::DWLRUReplData>(
            repl_data);
    ASSERT_NE(dw_data, nullptr);
    ASSERT_EQ(dw_data->validSubBlkCount, 0);
    ASSERT_EQ(dw_data->maxSubBlks, 1);
}

TEST_F(DWLRURPTestF, GetVictim1Candidate)
{
    ReplaceableEntry entry;
    entry.replacementData = rp->instantiateEntry();
    ReplacementCandidates candidates;
    candidates.push_back(&entry);

    ASSERT_EQ(rp->getVictim(candidates), &entry);

    rp->invalidate(entry.replacementData);
    ASSERT_EQ(rp->getVictim(candidates), &entry);

    rp->reset(entry.replacementData);
    ASSERT_EQ(rp->getVictim(candidates), &entry);

    rp->touch(entry.replacementData);
    ASSERT_EQ(rp->getVictim(candidates), &entry);
}

class DWLRUVictimizationTestF : public DWLRURPTestF
{
  protected:
    std::vector<SectorBlk> entries;
    ReplacementCandidates candidates;

  public:
    DWLRUVictimizationTestF() : DWLRURPTestF(), entries(4)
    {
        for (auto &entry : entries) {
            entry.replacementData = rp->instantiateEntry();
            entry.blks.resize(4, nullptr);
            candidates.push_back(&entry);
        }
    }
};

TEST_F(DWLRUVictimizationTestF, GetVictimDensityWeighted)
{
    // Reset all entries at the same tick (equal recency)
    mockTick = 500;
    for (auto &entry : candidates) {
        rp->reset(entry->replacementData);
    }

    // Set varying valid sub-block counts:
    // Entry 0: 4 valid sub-blocks (full, density = 1.0)
    // Entry 1: 3 valid sub-blocks (density = 0.75)
    // Entry 2: 1 valid sub-block  (sparse, density = 0.25)
    // Entry 3: 2 valid sub-blocks (density = 0.5)
    for (int k = 0; k < 4; ++k) {
        entries[0].validateSubBlk();
    }
    for (int k = 0; k < 3; ++k) {
        entries[1].validateSubBlk();
    }
    for (int k = 0; k < 1; ++k) {
        entries[2].validateSubBlk();
    }
    for (int k = 0; k < 2; ++k) {
        entries[3].validateSubBlk();
    }

    mockTick = 1000;
    // Entry 2 has fewest valid sub-blocks (lowest density), so it must be
    // chosen
    ASSERT_EQ(rp->getVictim(candidates), &entries[2]);

    // Invalidate Entry 2 and make Entry 3 have 0 valid sub-blocks
    entries[2].invalidateSubBlk(); // now 0 valid sub-blocks
    ASSERT_EQ(rp->getVictim(candidates), &entries[2]);
}

TEST_F(DWLRUVictimizationTestF, GetVictimRecencyWeighted)
{
    // All entries have 2 valid sub-blocks out of 4 (equal density = 0.5)
    for (auto &entry : entries) {
        for (int k = 0; k < 2; ++k) {
            entry.validateSubBlk();
        }
    }

    // Reset entries at different ticks
    mockTick = 100;
    rp->reset(entries[0].replacementData);

    mockTick = 200;
    rp->reset(entries[1].replacementData);

    mockTick = 300;
    rp->reset(entries[2].replacementData);

    mockTick = 400;
    rp->reset(entries[3].replacementData);

    mockTick = 500;
    // Entry 0 was touched earliest (tick 100, oldest), so it is selected
    ASSERT_EQ(rp->getVictim(candidates), &entries[0]);
}

TEST_F(DWLRUVictimizationTestF, AutomaticMetadataUpdate)
{
    SectorBlk secBlk;
    secBlk.replacementData = rp->instantiateEntry();
    secBlk.blks.resize(4, nullptr);

    auto dw_data =
        std::dynamic_pointer_cast<replacement_policy::DWLRU::DWLRUReplData>(
            secBlk.replacementData);
    ASSERT_NE(dw_data, nullptr);
    ASSERT_EQ(dw_data->validSubBlkCount, 0);

    secBlk.validateSubBlk();
    ASSERT_EQ(dw_data->validSubBlkCount, 1);
    ASSERT_EQ(dw_data->maxSubBlks, 4);

    secBlk.validateSubBlk();
    ASSERT_EQ(dw_data->validSubBlkCount, 2);

    secBlk.invalidateSubBlk();
    ASSERT_EQ(dw_data->validSubBlkCount, 1);

    secBlk.invalidateSubBlk();
    ASSERT_EQ(dw_data->validSubBlkCount, 0);
}

TEST_F(DWLRURPTestF, QueuePressureGetterSetter)
{
    ASSERT_DOUBLE_EQ(rp->getQueuePressure(), 0.0);
    ASSERT_DOUBLE_EQ(rp->getMemWriteQueuePressure(), 0.0);

    rp->setQueuePressure(0.2);
    ASSERT_DOUBLE_EQ(rp->getQueuePressure(), 0.2);
    ASSERT_DOUBLE_EQ(rp->getMemWriteQueuePressure(), 0.2);

    rp->setMemWriteQueuePressure(0.9);
    ASSERT_DOUBLE_EQ(rp->getQueuePressure(), 0.9);
    ASSERT_DOUBLE_EQ(rp->getMemWriteQueuePressure(), 0.9);
}

TEST_F(DWLRUVictimizationTestF, QueuePressureCongestionLevels)
{
    mockTick = 500;
    for (auto &entry : candidates) {
        rp->reset(entry->replacementData);
    }

    // Setup candidate entries:
    // Entry 0: 2 valid sub-blocks (density 0.5), clean (0 dirty sub-blocks)
    // Entry 1: 1 valid sub-block (density 0.25), dirty (3 writeback chunks)
    // Entry 2: 3 valid sub-blocks (density 0.75), clean
    // Entry 3: 4 valid sub-blocks (density 1.0), clean
    for (int k = 0; k < 2; ++k) {
        entries[0].validateSubBlk();
    }
    entries[1].validateSubBlk();
    for (int k = 0; k < 3; ++k) {
        entries[2].validateSubBlk();
    }
    for (int k = 0; k < 4; ++k) {
        entries[3].validateSubBlk();
    }

    auto data0 =
        std::dynamic_pointer_cast<replacement_policy::DWLRU::DWLRUReplData>(
            entries[0].replacementData);
    auto data1 =
        std::dynamic_pointer_cast<replacement_policy::DWLRU::DWLRUReplData>(
            entries[1].replacementData);
    data0->dirtySubBlkCount = 0;
    data1->dirtySubBlkCount = 3;

    mockTick = 1000;

    // 1. Low Queue Pressure (P_queue = 0.2 < threshold 0.7)
    // No writeback penalty. Entry 1 has lowest density (0.25) -> highest base
    // score.
    rp->setQueuePressure(0.2);
    ASSERT_EQ(rp->getVictim(candidates), &entries[1]);

    // 2. Moderate Queue Pressure (P_queue = 0.6 < threshold 0.7)
    // Still below threshold 0.7, so no penalty applied. Entry 1 selected as
    // victim.
    rp->setQueuePressure(0.6);
    ASSERT_EQ(rp->getVictim(candidates), &entries[1]);

    // 3. Severe Queue Pressure (P_queue = 0.9 > threshold 0.7)
    // Excess pressure = 0.2. Weight = 2.0.
    // Entry 1 base score = 501 / 0.25 = 2004.
    // Entry 1 penalty factor = 1.0 + (2.0 * 0.2 * 3) = 2.2.
    // Entry 1 penalized score = 2004 / 2.2 = 910.9.
    // Entry 0 base score = 501 / 0.5 = 1002.
    // Entry 0 is clean -> penalty factor = 1.0 -> penalized score = 1002.
    // Therefore, clean Entry 0 (score 1002) is prioritized over dirty Entry 1
    // (score 910.9).
    rp->setQueuePressure(0.9);
    ASSERT_EQ(rp->getVictim(candidates), &entries[0]);
}
