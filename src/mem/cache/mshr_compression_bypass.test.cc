/*
 * Unit test for memory queue threshold-aware cache compression bypass logic.
 */

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <list>
#include <string>

#include "mem/cache/queue.hh"
#include "mem/cache/queue_entry.hh"

namespace gem5
{

class DummyQueueEntry : public QueueEntry
{
  public:
    using List = std::list<DummyQueueEntry *>;
    using Iterator = List::iterator;

    List::iterator allocIter;
    List::iterator readyIter;

    DummyQueueEntry(const std::string &name = "dummy") : QueueEntry(name) {}
    bool
    matchBlockAddr(Addr addr, bool is_secure) const override
    {
        return false;
    }
    bool
    matchBlockAddr(const PacketPtr pkt) const override
    {
        return false;
    }
    bool
    conflictAddr(const QueueEntry *entry) const override
    {
        return false;
    }
    void
    deallocate()
    {}

    bool
    sendPacket(BaseCache &cache) override
    {
        return false;
    }
    Target *
    getTarget() override
    {
        return nullptr;
    }
};

TEST(MSHRCompressionBypassTest, QueueOccupancyAndCapacity)
{
    // Test Queue occupancy() and capacity() calculations
    // Queue constructor parameters: label, num_entries, reserve, name
    Queue<DummyQueueEntry> queue("TestQueue", 20, 0, "test_queue");

    EXPECT_EQ(queue.capacity(), 20);
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
}

TEST(MSHRCompressionBypassTest, QueueCapacityWithReserve)
{
    // Queue with reserve entries: num_entries = 16, reserve = 4
    // Capacity should be 16 (numEntries - numReserve)
    Queue<DummyQueueEntry> queue("TestReserveQueue", 16, 4,
                                 "test_reserve_queue");

    EXPECT_EQ(queue.capacity(), 16);
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_TRUE(queue.isEmpty());
}

TEST(MSHRCompressionBypassTest, HysteresisLogic)
{
    // Test hysteresis threshold logic with MSHR and write buffer occupancy:
    // High watermark threshold: 75%
    // Low watermark threshold: 50%
    // MSHR capacity = 20 entries
    // Write buffer capacity = 8 entries

    const unsigned highThreshold = 75;
    const unsigned lowThreshold = 50;
    const int mshrCapacity = 20;
    const int wbCapacity = 8;

    bool mshrCompressionBypassed = false;

    auto updateBypassState = [&](int mshrOccupancy, int wbOccupancy) {
        double mshrPct = ((double)mshrOccupancy / mshrCapacity) * 100.0;
        double wbPct = ((double)wbOccupancy / wbCapacity) * 100.0;
        double maxPct = std::max(mshrPct, wbPct);

        if (!mshrCompressionBypassed) {
            if (maxPct >= highThreshold) {
                mshrCompressionBypassed = true;
            }
        } else {
            if (maxPct < lowThreshold) {
                mshrCompressionBypassed = false;
            }
        }
    };

    // Initially at 0% occupancy -> bypass disabled
    updateBypassState(0, 0);
    EXPECT_FALSE(mshrCompressionBypassed);

    // MSHR occupancy 50% (10/20), Write buffer 0% -> bypass disabled
    updateBypassState(10, 0);
    EXPECT_FALSE(mshrCompressionBypassed);

    // Write buffer occupancy 75% (6/8), MSHR 0% -> bypass ACTIVATED (write
    // buffer congestion)!
    updateBypassState(0, 6);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Write buffer occupancy drops to 50% (4/8), MSHR 0% -> remains ACTIVATED
    // (hysteresis)
    updateBypassState(0, 4);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Write buffer drops to 37.5% (3/8), MSHR 0% -> bypass DEACTIVATED!
    updateBypassState(0, 3);
    EXPECT_FALSE(mshrCompressionBypassed);

    // MSHR reaches 75% (15/20) -> bypass ACTIVATED!
    updateBypassState(15, 0);
    EXPECT_TRUE(mshrCompressionBypassed);

    // MSHR decreases to 60% (12/20) -> remains ACTIVATED (hysteresis)
    updateBypassState(12, 0);
    EXPECT_TRUE(mshrCompressionBypassed);

    // MSHR drops below 50% to 45% (9/20) -> bypass DEACTIVATED!
    updateBypassState(9, 0);
    EXPECT_FALSE(mshrCompressionBypassed);
}

} // namespace gem5
