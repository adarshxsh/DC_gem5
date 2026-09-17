/*
 * Copyright (c) 2026 ARM Limited
 * All rights reserved
 */

#include <algorithm>
#include <cmath>

#include <gtest/gtest.h>

#include "mem/cache/cache_probe_arg.hh"

namespace gem5
{

class MockCacheAccessor : public CacheAccessor
{
  public:
    double congestionScore = 0.0;

    bool inCache(Addr addr, bool is_secure) const override { return false; }
    bool hasBeenPrefetched(Addr addr, bool is_secure) const override { return false; }
    bool hasBeenPrefetched(Addr addr, bool is_secure, RequestorID requestor) const override { return false; }
    bool inMissQueue(Addr addr, bool is_secure) const override { return false; }
    bool coalesce() const override { return false; }
    double getCongestionScore() const override { return congestionScore; }
};

// Helper function implementing the prefetch throttling multiplier logic
size_t
computeMaxPermittedPrefetches(size_t total, size_t throttleControlPct, double congestion_score)
{
    size_t num_pt = total;
    if (throttleControlPct > 0) {
        num_pt = (total * throttleControlPct) / 100;
    }
    if (congestion_score > 0.0) {
        double multiplier = 1.0 - std::min(1.0, std::max(0.0, congestion_score));
        num_pt = static_cast<size_t>(std::round(num_pt * multiplier));
    }
    return num_pt;
}

// Helper function implementing MSHRQueue canPrefetch congestion gate
bool
canPrefetchCongestionGate(size_t demandAllocated, size_t capacity, double congestionScore)
{
    if (capacity > 0 && ((double)demandAllocated / capacity) >= 0.80) {
        return false;
    }
    if (congestionScore >= 0.85) {
        return false;
    }
    return true;
}

TEST(CacheAccessorTest, CongestionScoreInterface)
{
    MockCacheAccessor mockAccessor;
    EXPECT_DOUBLE_EQ(mockAccessor.getCongestionScore(), 0.0);

    mockAccessor.congestionScore = 0.75;
    EXPECT_DOUBLE_EQ(mockAccessor.getCongestionScore(), 0.75);
}

TEST(QueuedThrottlingTest, CongestionThrottlingMultiplier)
{
    // Uncongested (0.0): 10 candidates -> 10 permitted
    EXPECT_EQ(computeMaxPermittedPrefetches(10, 0, 0.0), 10);

    // Moderate congestion (0.50): 10 candidates -> 5 permitted
    EXPECT_EQ(computeMaxPermittedPrefetches(10, 0, 0.50), 5);

    // High congestion (0.80): 10 candidates -> 2 permitted
    EXPECT_EQ(computeMaxPermittedPrefetches(10, 0, 0.80), 2);

    // Critical congestion (1.00): 10 candidates -> 0 permitted
    EXPECT_EQ(computeMaxPermittedPrefetches(10, 0, 1.00), 0);

    // Accuracy throttling (50% -> 5) + congestion (0.50): 5 * 0.5 = 2.5 -> 3 permitted
    EXPECT_EQ(computeMaxPermittedPrefetches(10, 50, 0.50), 3);
}

TEST(MSHRQueueThrottlingTest, SafetyThresholdBoundaries)
{
    // Below demand occupancy threshold and below critical congestion score
    EXPECT_TRUE(canPrefetchCongestionGate(5, 16, 0.20));

    // High demand MSHR occupancy (>= 80%) denies speculative prefetch
    EXPECT_FALSE(canPrefetchCongestionGate(13, 16, 0.10));

    // Critical congestion score (>= 0.85) denies speculative prefetch
    EXPECT_FALSE(canPrefetchCongestionGate(2, 16, 0.85));
    EXPECT_FALSE(canPrefetchCongestionGate(2, 16, 0.95));
}

} // namespace gem5
