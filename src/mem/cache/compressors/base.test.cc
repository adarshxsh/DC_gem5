/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <list>

#include "mem/cache/compressors/base.hh"
#include "mem/cache/queue.hh"
#include "mem/cache/queue_entry.hh"
#include "sim/root.hh"

namespace gem5
{

Root *Root::_root = nullptr;

class DummyQueueEntry : public QueueEntry
{
  public:
    using List = std::list<DummyQueueEntry *>;
    using Iterator = List::iterator;

    Iterator allocIter;
    Iterator readyIter;

    DummyQueueEntry(const std::string &name = "") : QueueEntry(name) {}

    void
    deallocate()
    {}
    bool
    matchBlockAddr(const Addr addr, const bool is_secure) const override
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

TEST(QueueTest, OccupancyAndCapacityHelpers)
{
    // A queue with num_entries = 8, reserve = 2 -> capacity = 8
    Queue<DummyQueueEntry> queue("dummy_queue", 8, 2, "sys.cache.dummy");

    EXPECT_EQ(queue.capacity(), 8);
    EXPECT_EQ(queue.numAllocated(), 0);
    EXPECT_DOUBLE_EQ(queue.occupancy(), 0.0);
    EXPECT_FALSE(queue.isFull());
}

} // namespace gem5
