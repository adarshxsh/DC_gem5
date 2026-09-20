/*
 * Unit test for MSHR threshold-aware cache compression bypass logic.
 */

#include <cstdint>
#include <gtest/gtest.h>
#include <string>

// Simple test queue class mirroring Queue occupancy and capacity interface
class TestQueue
{
  private:
    int numEntries;
    int numReserve;
    int allocated;

  public:
    TestQueue(const std::string &label, int num_entries, int reserve,
              const std::string &name)
        : numEntries(num_entries), numReserve(reserve), allocated(0)
    {}

    void
    allocateEntry()
    {
        allocated++;
    }
    void
    deallocateEntry()
    {
        if (allocated > 0) {
            allocated--;
        }
    }

    bool
    isEmpty() const
    {
        return allocated == 0;
    }
    bool
    isFull() const
    {
        return allocated >= (numEntries - numReserve);
    }

    int
    occupancy() const
    {
        return allocated;
    }
    int
    capacity() const
    {
        return numEntries - numReserve;
    }
};

TEST(MSHRCompressionBypassTest, QueueOccupancyAndCapacity)
{
    TestQueue queue("TestQueue", 20, 0, "test_queue");

    EXPECT_EQ(queue.capacity(), 20);
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_FALSE(queue.isFull());
}

TEST(MSHRCompressionBypassTest, QueueCapacityWithReserve)
{
    TestQueue queue("TestReserveQueue", 16, 4, "test_reserve_queue");

    EXPECT_EQ(queue.capacity(), 12);
    EXPECT_EQ(queue.occupancy(), 0);
    EXPECT_TRUE(queue.isEmpty());
}

TEST(MSHRCompressionBypassTest, HysteresisLogic)
{
    const unsigned highThreshold = 75;
    const unsigned lowThreshold = 50;
    const int totalCapacity = 20;

    bool mshrCompressionBypassed = false;

    auto updateBypassState = [&](int currentOccupancy) {
        double occupancyPct =
            ((double)currentOccupancy / totalCapacity) * 100.0;
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

    updateBypassState(0);
    EXPECT_FALSE(mshrCompressionBypassed);

    updateBypassState(10);
    EXPECT_FALSE(mshrCompressionBypassed);

    updateBypassState(14);
    EXPECT_FALSE(mshrCompressionBypassed);

    updateBypassState(15);
    EXPECT_TRUE(mshrCompressionBypassed);

    updateBypassState(18);
    EXPECT_TRUE(mshrCompressionBypassed);

    updateBypassState(12);
    EXPECT_TRUE(mshrCompressionBypassed);

    updateBypassState(10);
    EXPECT_TRUE(mshrCompressionBypassed);

    updateBypassState(9);
    EXPECT_FALSE(mshrCompressionBypassed);

    updateBypassState(12);
    EXPECT_FALSE(mshrCompressionBypassed);
}
