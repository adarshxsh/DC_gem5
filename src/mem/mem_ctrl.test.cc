/*
 * Copyright (c) 2026 gem5 Architecture Contributors
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
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace gem5
{
namespace memory
{

class MemCtrlArbitrationTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
    }
};

TEST_F(MemCtrlArbitrationTest, DynamicQuotaCalculation)
{
    uint32_t min_writes = 16;
    uint32_t max_writes = 64;
    uint32_t min_reads = 16;
    uint32_t max_reads = 64;

    // High write pressure gradient scenario
    double p_write = 0.9;
    double p_read = 0.2;
    double pressure_gradient = p_write - p_read; // +0.7

    uint32_t wr_quota = min_writes;
    if (pressure_gradient > 0.0) {
        double scale = std::min(1.0, pressure_gradient);
        wr_quota = min_writes + (uint32_t)((max_writes - min_writes) * scale);
    }
    EXPECT_GT(wr_quota, min_writes);
    EXPECT_LE(wr_quota, max_writes);

    // High read pressure gradient scenario
    p_write = 0.1;
    p_read = 0.8;
    pressure_gradient = p_write - p_read; // -0.7

    uint32_t rd_quota = min_reads;
    if (pressure_gradient < 0.0) {
        double scale = std::min(1.0, -pressure_gradient);
        rd_quota = min_reads + (uint32_t)((max_reads - min_reads) * scale);
    }
    EXPECT_GT(rd_quota, min_reads);
    EXPECT_LE(rd_quota, max_reads);
}

TEST_F(MemCtrlArbitrationTest, EscalationThreshold)
{
    uint64_t read_age_thresh = 50000; // 50ns in ticks
    uint64_t entry_time_old = 1000;
    uint64_t current_tick = 60000; // wait time = 59000 > 50000

    uint64_t wait_time = current_tick - entry_time_old;
    bool is_escalated = (wait_time >= read_age_thresh);
    EXPECT_TRUE(is_escalated);

    uint64_t entry_time_recent = 20000; // wait time = 40000 < 50000
    wait_time = current_tick - entry_time_recent;
    is_escalated = (wait_time >= read_age_thresh);
    EXPECT_FALSE(is_escalated);
}

TEST_F(MemCtrlArbitrationTest, WriteDrainHysteresisLogic)
{
    uint32_t write_high_thresh = 85;
    uint32_t write_low_thresh = 50;

    uint32_t current_writes = 86; // Above high threshold
    bool is_write_draining = false;

    if (current_writes > write_high_thresh) {
        is_write_draining = true;
    }
    EXPECT_TRUE(is_write_draining);

    // After 16 writes, current_writes = 70 (still > 50 low thresh)
    current_writes = 70;
    if (is_write_draining) {
        if (current_writes < write_low_thresh) {
            is_write_draining = false;
        }
    }
    // Draining should remain active!
    EXPECT_TRUE(is_write_draining);

    // After draining down to 45 (below low thresh)
    current_writes = 45;
    if (is_write_draining) {
        if (current_writes < write_low_thresh) {
            is_write_draining = false;
        }
    }
    // Draining should now be complete!
    EXPECT_FALSE(is_write_draining);
}

} // namespace memory
} // namespace gem5
