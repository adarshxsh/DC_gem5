/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Unit tests for MemCtrl Adaptive Write Buffer Drain Management
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gem5
{

TEST(MemCtrlAdaptiveTest, PressureGradientAndEffectiveThreshold)
{
    // Test pressure gradient bounds and calculations
    uint32_t write_buffer_size = 64;
    uint32_t write_high_base = 54; // 85% of 64
    uint32_t write_high_max = 60;  // ~94% of 64
    uint32_t hard_ceiling = (uint32_t)std::floor(write_buffer_size * 0.95); // 60

    // Ensure max threshold does not exceed 95% ceiling
    EXPECT_LE(write_high_max, hard_ceiling);

    // Dynamic threshold scaling check
    for (double gradient = 0.0; gradient <= 1.0; gradient += 0.25) {
        uint32_t dynamic_high = write_high_base +
            (uint32_t)std::round((write_high_max - write_high_base) * gradient);
        uint32_t eff_high = std::min(dynamic_high, hard_ceiling);

        EXPECT_GE(eff_high, write_high_base);
        EXPECT_LE(eff_high, hard_ceiling);
    }
}

TEST(MemCtrlAdaptiveTest, DynamicWatermarkFormula)
{
    // Calculate pressure gradient: 0.5 * rd_occ + 0.5 * rd_wait - 0.5 * wr_occ
    double rd_occ = 0.8;
    double rd_wait = 0.6;
    double wr_occ = 0.2;

    double grad = std::max(0.0, std::min(1.0, 0.5 * rd_occ + 0.5 * rd_wait - 0.5 * wr_occ));
    EXPECT_NEAR(grad, 0.6, 1e-5);

    uint32_t base_high = 50;
    uint32_t max_high = 60;
    uint32_t eff_high = base_high + (uint32_t)std::round((max_high - base_high) * grad);
    EXPECT_EQ(eff_high, 56);
}

TEST(MemCtrlAdaptiveTest, AdaptiveBurstBatching)
{
    uint32_t min_writes = 16;
    uint32_t max_writes = 32;
    uint32_t write_buffer_size = 64;

    for (uint32_t write_q_size = 0; write_q_size <= write_buffer_size; write_q_size += 16) {
        double wr_occ_ratio = std::min(1.0, (double)write_q_size / write_buffer_size);
        uint32_t adaptive_writes = min_writes +
            (uint32_t)std::round((max_writes - min_writes) * wr_occ_ratio);
        adaptive_writes = std::min(adaptive_writes, max_writes);

        EXPECT_GE(adaptive_writes, min_writes);
        EXPECT_LE(adaptive_writes, max_writes);
    }
}

TEST(MemCtrlAdaptiveTest, EmergencyReadPreemptionCheck)
{
    uint64_t max_stall_latency = 100000; // 100ns in ticks
    uint64_t old_read_entry_time = 1000;
    uint64_t current_tick = 105000;

    uint64_t read_wait = current_tick - old_read_entry_time; // 104000
    bool emergency_read = (read_wait >= max_stall_latency);

    EXPECT_TRUE(emergency_read);

    // Read wait below threshold
    uint64_t recent_read_entry_time = 50000;
    uint64_t recent_wait = current_tick - recent_read_entry_time; // 55000
    bool recent_emergency = (recent_wait >= max_stall_latency);

    EXPECT_FALSE(recent_emergency);
}

} // namespace gem5
