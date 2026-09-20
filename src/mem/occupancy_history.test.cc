/*
 * Unit tests for OccupancyHistory write queue growth derivative tracking.
 */

#include <gtest/gtest.h>

#include "mem/mem_ctrl.hh"

namespace gem5
{

namespace memory
{

TEST(OccupancyHistoryTest, InitialState)
{
    OccupancyHistory history(10);
    EXPECT_EQ(history.getDerivative(100, 0), 0.0);
}

TEST(OccupancyHistoryTest, SingleSample)
{
    OccupancyHistory history(10);
    history.addSample(1000, 5);
    EXPECT_EQ(history.getDerivative(1000, 5), 0.0);
}

TEST(OccupancyHistoryTest, DerivativeCalculation)
{
    OccupancyHistory history(10);
    // Add sample at t=1000ns, occupancy=10
    history.addSample(1000, 10);
    // Add sample at t=2000ns, occupancy=20
    history.addSample(2000, 20);

    // dQ/dt = (20 - 10) / (2000 - 1000) = 10 / 1000 = 0.01 per tick
    double dQ_dt = history.getDerivative(2000, 20);
    EXPECT_DOUBLE_EQ(dQ_dt, 0.01);
}

TEST(OccupancyHistoryTest, SameTickUpdate)
{
    OccupancyHistory history(10);
    history.addSample(1000, 10);
    history.addSample(2000, 15);
    // Update sample at t=2000 to occupancy=25
    history.addSample(2000, 25);

    double dQ_dt = history.getDerivative(2000, 25);
    // dQ/dt = (25 - 10) / (2000 - 1000) = 15 / 1000 = 0.015
    EXPECT_DOUBLE_EQ(dQ_dt, 0.015);
}

TEST(OccupancyHistoryTest, CapacityWrapping)
{
    OccupancyHistory history(3);
    history.addSample(100, 10); // Overwritten later
    history.addSample(200, 20);
    history.addSample(300, 30);
    history.addSample(400, 40); // Overwrites t=100

    // Remaining samples in ring buffer: (200, 20), (300, 30), (400, 40)
    // Oldest is (200, 20), latest is (400, 40)
    // dQ/dt = (40 - 20) / (400 - 200) = 20 / 200 = 0.1
    double dQ_dt = history.getDerivative(400, 40);
    EXPECT_DOUBLE_EQ(dQ_dt, 0.1);
}

} // namespace memory
} // namespace gem5
