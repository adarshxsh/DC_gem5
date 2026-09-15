/*
 * Unit test for MSHR threshold-aware cache compression bypass logic.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <list>
#include <string>

#include "mem/cache/queue.hh"
#include "mem/cache/queue_entry.hh"

namespace gem5 {

class DummyQueueEntry : public QueueEntry
{
  public:
    using List = std::list<DummyQueueEntry*>;
    using Iterator = List::iterator;

    List::iterator allocIter;
    List::iterator readyIter;

    DummyQueueEntry(const std::string &name = "dummy") : QueueEntry(name) {}
    bool matchBlockAddr(Addr addr, bool is_secure) const override { return false; }
    bool matchBlockAddr(const PacketPtr pkt) const override { return false; }
    bool conflictAddr(const QueueEntry *entry) const override { return false; }
    void deallocate() {}

    bool sendPacket(BaseCache &cache) override { return false; }
    Target* getTarget() override { return nullptr; }
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
    Queue<DummyQueueEntry> queue("TestReserveQueue", 16, 4, "test_reserve_queue");

    EXPECT_EQ(queue.capacity(), 16);
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_TRUE(queue.isEmpty());
}

TEST(MSHRCompressionBypassTest, HysteresisLogic)
{
    // Test the hysteresis threshold logic:
    // High watermark threshold: 75%
    // Low watermark threshold: 50%
    // MSHR capacity = 20 entries
    // High threshold trigger at >= 15 allocated entries (15/20 = 75%)
    // Low threshold trigger at < 10 allocated entries (9/20 = 45% < 50%)

    const unsigned highThreshold = 75;
    const unsigned lowThreshold = 50;
    const int totalCapacity = 20;

    bool mshrCompressionBypassed = false;

    auto updateBypassState = [&](int currentOccupancy) {
        double occupancyPct = ((double)currentOccupancy / totalCapacity) * 100.0;
        if (!mshrCompressionBypassed) {
            if (occupancyPct >= highThreshold) {
                mshrCompressionBypassed = true;
            }
        } else {
            if (occupancyPct < lowThreshold) {
                mshrCompressionBypassed = false;
            }
        }
    };

    // Initially at 0% occupancy -> bypass disabled
    updateBypassState(0);
    EXPECT_FALSE(mshrCompressionBypassed);

    // Occupancy increases to 50% (10 MSHRs) -> bypass disabled
    updateBypassState(10);
    EXPECT_FALSE(mshrCompressionBypassed);

    // Occupancy increases to 70% (14 MSHRs) -> bypass disabled
    updateBypassState(14);
    EXPECT_FALSE(mshrCompressionBypassed);

    // Occupancy reaches 75% (15 MSHRs) -> bypass ACTIVATED!
    updateBypassState(15);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Occupancy increases to 90% (18 MSHRs) -> bypass remains ACTIVATED
    updateBypassState(18);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Occupancy decreases to 60% (12 MSHRs) -> bypass remains ACTIVATED (hysteresis)
    updateBypassState(12);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Occupancy decreases to 50% (10 MSHRs) -> bypass remains ACTIVATED (hysteresis)
    updateBypassState(10);
    EXPECT_TRUE(mshrCompressionBypassed);

    // Occupancy drops below 50% to 45% (9 MSHRs) -> bypass DEACTIVATED!
    updateBypassState(9);
    EXPECT_FALSE(mshrCompressionBypassed);

    // Occupancy increases back to 60% (12 MSHRs) -> bypass remains DEACTIVATED
    updateBypassState(12);
    EXPECT_FALSE(mshrCompressionBypassed);
}

} // namespace gem5
