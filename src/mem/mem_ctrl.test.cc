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

#include "mem/mem_ctrl.hh"
#include "mem/mem_interface.hh"

namespace gem5
{
namespace memory
{

class MemCtrlUnitTest : public ::testing::Test
{
  protected:
    Tick mockTick = 0;

    void
    SetUp() override
    {
        Gem5Internal::_curTickPtr = &mockTick;
    }

    void
    TearDown() override
    {
        Gem5Internal::_curTickPtr = nullptr;
    }
};

TEST_F(MemCtrlUnitTest, BaselineEquivalenceWhenDisabled)
{
    // Test that when enable_adaptive_thresholds is false, static thresholds
    // remain unchanged
    MemCtrlParams p;
    p.write_high_thresh_perc = 85;
    p.write_low_thresh_perc = 50;
    p.min_writes_per_switch = 16;
    p.min_reads_per_switch = 16;
    p.enable_adaptive_thresholds = false;
    p.pressure_threshold = 0.7;

    EXPECT_FALSE(p.enable_adaptive_thresholds);
    EXPECT_EQ(p.write_high_thresh_perc, 85);
    EXPECT_EQ(p.write_low_thresh_perc, 50);
}

} // namespace memory
} // namespace gem5
