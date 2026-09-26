/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <list>

#include "base/gtest/cur_tick_fake.hh"
#include "mem/cache/queue.hh"
#include "mem/cache/queue_entry.hh"
#include "sim/root.hh"

namespace gem5
{

Root *Root::_root = nullptr;

class DummyQueueEntry : public QueueEntry
{
  public:
    typedef std::list<DummyQueueEntry*> List;
    typedef List::iterator Iterator;
    Iterator allocIter;
    Iterator readyIter;

    DummyQueueEntry(const std::string &name = "dummy_entry")
        : QueueEntry(name)
    {}

    bool matchBlockAddr(Addr addr, bool is_secure) const override { return false; }
    bool matchBlockAddr(const PacketPtr pkt) const override { return false; }
    bool conflictAddr(const QueueEntry *entry) const override { return false; }
    bool sendPacket(BaseCache &cache) override { return false; }
    Target* getTarget() override { return nullptr; }
    void deallocate() {}
};

class DummyQueue : public Queue<DummyQueueEntry>
{
  public:
    DummyQueue(int num_entries, int reserve, double ewma_alpha = 0.1, double gradient_alpha = 0.1)
        : Queue<DummyQueueEntry>("dummy_label", num_entries, reserve, "dummy_queue", ewma_alpha, gradient_alpha)
    {}

    void allocateEntry()
    {
        allocated++;
        updateEWMA();
    }

    void deallocateEntry()
    {
        if (allocated > 0) {
            allocated--;
            updateEWMA();
        }
    }
};

TEST(QueueEWMATest, EWMAOccupancyAndGradient)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(100);

    // Create a queue with capacity 10 (numEntries = 12, reserve = 2)
    DummyQueue q(10, 2, 0.5, 0.5);

    EXPECT_EQ(q.capacity(), 10);
    EXPECT_EQ(q.occupancy(), 0);
    EXPECT_EQ(q.getEWMAOccupancy(), 0.0);
    EXPECT_EQ(q.getArrivalRateGradient(), 0.0);

    // Tick 110: Allocate 2 entries
    tickHandler.setCurTick(110);
    q.allocateEntry();
    q.allocateEntry();

    // Occupancy = 2
    EXPECT_EQ(q.occupancy(), 2);
    // Initial allocation at tick 110 sets lastUpdateTick=110, ewmaOccupancy=1.0, arrivalRateGradient=0.0
    // Second allocation at tick 110 (deltaT=0): ewmaOccupancy = 1.5, arrivalRateGradient = 0.0
    EXPECT_DOUBLE_EQ(q.getEWMAOccupancy(), 1.5);
    EXPECT_DOUBLE_EQ(q.getArrivalRateGradient(), 0.0);

    // Predicted occupancy with leadTime = 20 ticks:
    // 1.5 + 0.0 * 20 = 1.5
    EXPECT_DOUBLE_EQ(q.getPredictedOccupancy(20.0), 1.5);
    // Predicted pressure ratio = 1.5 / 10.0 = 0.15
    EXPECT_DOUBLE_EQ(q.getPredictedPressureRatio(20.0), 0.15);

    // Tick 120: Deallocate 1 entry
    tickHandler.setCurTick(120);
    q.deallocateEntry();

    // Occupancy = 1
    // DeltaT = 10, DeltaQ = -1 -> instGradient = -0.1
    // EWMA Occupancy = 0.5 * 1 + 0.5 * 1.5 = 1.25
    // Rate gradient = 0.5 * (-0.1) + 0.5 * 0.0 = -0.05
    EXPECT_DOUBLE_EQ(q.getEWMAOccupancy(), 1.25);
    EXPECT_DOUBLE_EQ(q.getArrivalRateGradient(), -0.05);
}

} // namespace gem5
