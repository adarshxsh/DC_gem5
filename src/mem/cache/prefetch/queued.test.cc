/*
 * Copyright (c) 2026
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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "base/gtest/cur_tick_fake.hh"
#include "mem/cache/cache_probe_arg.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/cache/replacement_policies/lru_rp.hh"
#include "mem/cache/tags/indexing_policies/set_associative.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "params/ClockDomain.hh"
#include "params/LRURP.hh"
#include "params/QueuedPrefetcher.hh"
#include "params/TaggedSetAssociative.hh"
#include "sim/clock_domain.hh"
#include "sim/cur_tick.hh"
#include "sim/power/power_model.hh"
#include "sim/root.hh"
#include "sim/system.hh"

using namespace gem5;
using namespace gem5::prefetch;

namespace gem5
{
Root *Root::_root = nullptr;

RequestorID
System::getRequestorId(const SimObject *requestor, std::string subrequestor)
{
    return 0;
}

void
exitSimLoop(const std::string &message, int exit_code, Tick when, Tick repeat,
            bool serialize)
{}

void
PowerModel::setClockedObject(ClockedObject *clk_obj)
{}
} // namespace gem5

namespace
{

class MockCacheAccessor : public CacheAccessor
{
  public:
    uint8_t compressionFactor = 1;
    std::size_t compressedSizeBits = 512;

    bool
    inCache(Addr addr, bool is_secure) const override
    {
        return false;
    }
    bool
    hasBeenPrefetched(Addr addr, bool is_secure) const override
    {
        return false;
    }
    bool
    hasBeenPrefetched(Addr addr, bool is_secure,
                      RequestorID requestor) const override
    {
        return false;
    }
    bool
    inMissQueue(Addr addr, bool is_secure) const override
    {
        return false;
    }
    bool
    coalesce() const override
    {
        return false;
    }

    std::size_t
    getCompressedSizeBits(Addr addr, bool is_secure) const override
    {
        return compressedSizeBits;
    }

    uint8_t
    getCompressionFactor(Addr addr, bool is_secure) const override
    {
        return compressionFactor;
    }
};

class MockClockDomain : public ClockDomain
{
  public:
    MockClockDomain(const ClockDomainParams &p) : ClockDomain(p, nullptr)
    {
        _clockPeriod = 1;
    }
};

class TestQueuedPrefetcher : public Queued
{
  public:
    TestQueuedPrefetcher(const QueuedPrefetcherParams &p) : Queued(p) {}

    void
    calculatePrefetch(const PrefetchInfo &pfi,
                      std::vector<AddrPriority> &addresses,
                      const CacheAccessor &cache) override
    {
        // Simple stride prefetch candidate generator
        addresses.push_back(std::make_pair(pfi.getAddr() + 64, 1));
    }

    std::list<DeferredPacket> &
    getPFQ()
    {
        return pfq;
    }
    QueuedStats &
    getStats()
    {
        return statsQueued;
    }
};

} // namespace

TEST(QueuedCHTTest, CHTFilteringAndSaturationCounters)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(1000);

    ClockDomainParams clkParams{};
    clkParams.name = "mock_clk_domain";
    clkParams.eventq_index = 0;
    MockClockDomain mockClkDomain(clkParams);

    QueuedPrefetcherParams params{};
    params.eventq_index = 0;
    params.clk_domain = &mockClkDomain;
    params.name = "test_queued_prefetcher";
    params.block_size = 64;
    params.latency = 1;
    params.queue_size = 32;
    params.max_prefetch_requests_with_pending_translation = 32;
    params.queue_squash = true;
    params.queue_filter = true;
    params.cache_snoop = false;
    params.tag_prefetch = true;
    params.throttle_control_percentage = 0;
    params.on_miss = false;
    params.on_read = true;
    params.on_write = true;
    params.on_data = true;
    params.on_inst = true;
    params.prefetch_on_access = true;
    params.prefetch_on_pf_hit = true;
    params.use_virtual_addresses = false;
    params.page_bytes = 4096;

    params.enable_cht = true;
    params.cht_entries = 64;
    params.cht_assoc = 2;
    params.cht_min_cf_threshold = 2;
    params.cht_missing_is_low = true;

    TaggedSetAssociativeParams idxParams{};
    idxParams.eventq_index = 0;
    idxParams.entry_size = 1;
    idxParams.assoc = 2;
    idxParams.size = 64;
    params.cht_indexing_policy = new TaggedSetAssociative(idxParams);

    LRURPParams replParams{};
    replParams.eventq_index = 0;
    params.cht_replacement_policy = new replacement_policy::LRU(replParams);

    TestQueuedPrefetcher prefetcher(params);
    MockCacheAccessor mockCache;

    Addr testPC1 = 0x400100;
    Addr testAddr1 = 0x8000;

    // Create a request with PC testPC1
    RequestPtr req = std::make_shared<Request>(testAddr1, 64, 0, 0);
    req->setPC(testPC1);
    Packet pkt(req, MemCmd::ReadReq);
    pkt.allocate();

    Base::PrefetchInfo pfi(&pkt, testAddr1, true);

    // Initial state: untracked PC defaults to low compression when
    // cht_missing_is_low is true
    EXPECT_TRUE(prefetcher.isLowCompression(testPC1, false));

    // Try inserting prefetch candidate with testPC1; should be dropped
    prefetcher.insert(&pkt, pfi, 1, mockCache);
    EXPECT_EQ(prefetcher.getPFQ().size(), 0);
    EXPECT_EQ(prefetcher.getStats().pfDroppedLowCompression.value(), 1);

    // Observe a compressible fill for testPC1 (CF = 2)
    mockCache.compressionFactor = 2;
    CacheAccessProbeArg fillArg(&pkt, mockCache);
    prefetcher.notifyFill(fillArg);

    // After demand fill, CHT entry allocated/updated with counter = 3 (>=
    // threshold 2)
    EXPECT_FALSE(prefetcher.isLowCompression(testPC1, false));

    // Inserting again for testPC1 should now pass CHT filter
    prefetcher.insert(&pkt, pfi, 1, mockCache);
    EXPECT_EQ(prefetcher.getPFQ().size(), 1);

    // Observe an uncompressible fill for testPC1 (CF = 1)
    mockCache.compressionFactor = 1;
    prefetcher.notifyFill(fillArg);
    prefetcher.notifyFill(fillArg);

    // Counter drops below threshold
    EXPECT_TRUE(prefetcher.isLowCompression(testPC1, false));

    // Candidate should be dropped again
    prefetcher.getPFQ().clear();
    prefetcher.insert(&pkt, pfi, 1, mockCache);
    EXPECT_EQ(prefetcher.getPFQ().size(), 0);
    EXPECT_EQ(prefetcher.getStats().pfDroppedLowCompression.value(), 2);

    delete params.cht_indexing_policy;
    delete params.cht_replacement_policy;
}

TEST(QueuedCHTTest, CHTMissingIsLowDisabled)
{
    GTestTickHandler tickHandler;
    tickHandler.setCurTick(1000);

    ClockDomainParams clkParams{};
    clkParams.name = "mock_clk_domain_2";
    clkParams.eventq_index = 0;
    MockClockDomain mockClkDomain(clkParams);

    QueuedPrefetcherParams params{};
    params.eventq_index = 0;
    params.clk_domain = &mockClkDomain;
    params.name = "test_queued_prefetcher_optimistic";
    params.block_size = 64;
    params.latency = 1;
    params.queue_size = 32;
    params.max_prefetch_requests_with_pending_translation = 32;
    params.queue_squash = true;
    params.queue_filter = true;
    params.cache_snoop = false;
    params.tag_prefetch = true;
    params.throttle_control_percentage = 0;
    params.on_miss = false;
    params.on_read = true;
    params.on_write = true;
    params.on_data = true;
    params.on_inst = true;
    params.prefetch_on_access = true;
    params.prefetch_on_pf_hit = true;
    params.use_virtual_addresses = false;
    params.page_bytes = 4096;

    params.enable_cht = true;
    params.cht_entries = 64;
    params.cht_assoc = 2;
    params.cht_min_cf_threshold = 2;
    params.cht_missing_is_low = false;

    TaggedSetAssociativeParams idxParams{};
    idxParams.eventq_index = 0;
    idxParams.entry_size = 1;
    idxParams.assoc = 2;
    idxParams.size = 64;
    params.cht_indexing_policy = new TaggedSetAssociative(idxParams);

    LRURPParams replParams{};
    replParams.eventq_index = 0;
    params.cht_replacement_policy = new replacement_policy::LRU(replParams);

    TestQueuedPrefetcher prefetcher(params);
    MockCacheAccessor mockCache;

    Addr testPC1 = 0x400100;
    Addr testAddr1 = 0x8000;

    RequestPtr req = std::make_shared<Request>(testAddr1, 64, 0, 0);
    req->setPC(testPC1);
    Packet pkt(req, MemCmd::ReadReq);
    pkt.allocate();

    Base::PrefetchInfo pfi(&pkt, testAddr1, true);

    // Initial state: when cht_missing_is_low is false, untracked PC is not
    // treated as low compression
    EXPECT_FALSE(prefetcher.isLowCompression(testPC1, false));

    // Candidate should be enqueued
    prefetcher.insert(&pkt, pfi, 1, mockCache);
    EXPECT_EQ(prefetcher.getPFQ().size(), 1);
    EXPECT_EQ(prefetcher.getStats().pfDroppedLowCompression.value(), 0);

    delete params.cht_indexing_policy;
    delete params.cht_replacement_policy;
}
