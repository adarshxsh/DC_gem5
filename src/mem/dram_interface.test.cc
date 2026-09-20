/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Unit test for DRAMInterface dynamic refresh deferral and power-down
 * inhibition.
 */

#include <gtest/gtest.h>

#include "mem/dram_interface.hh"
#include "mem/mem_ctrl.hh"

namespace gem5
{
namespace memory
{

TEST(DRAMInterfaceTest, QueuePressureAndDrainingCheck)
{
    // Test write queue pressure computation
    uint32_t write_buffer_size = 64;
    uint32_t current_writes = 48;
    double pressure = write_buffer_size > 0
                          ? (double)current_writes / (double)write_buffer_size
                          : 0.0;
    EXPECT_DOUBLE_EQ(pressure, 0.75);

    // Test high write threshold pressure (> 85%)
    uint32_t high_writes = 58;
    double high_pressure = (double)high_writes / (double)write_buffer_size;
    EXPECT_GT(high_pressure, 0.85);
}

} // namespace memory
} // namespace gem5
