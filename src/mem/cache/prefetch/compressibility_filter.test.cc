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

#include <vector>

#include "mem/cache/prefetch/base.hh"
#include "params/BasePrefetcher.hh"

namespace gem5
{
namespace prefetch
{

class TestPrefetcher : public Base
{
  public:
    TestPrefetcher(const BasePrefetcherParams &p) : Base(p) {}

    void notify(const CacheAccessProbeArg &acc, const PrefetchInfo &pfi) override {}
    PacketPtr getPacket() override { return nullptr; }
    Tick nextPrefetchReadyTime() const override { return 0; }
};

TEST(CompressibilityFilterTest, LookupDefaultUnseen)
{
    BasePrefetcherParams p;
    p.block_size = 64;
    p.on_miss = true;
    p.on_read = true;
    p.on_write = true;
    p.on_data = true;
    p.on_inst = true;
    p.prefetch_on_access = false;
    p.prefetch_on_pf_hit = true;
    p.use_virtual_addresses = false;
    p.page_bytes = 4096;
    p.enable_compressibility_filter = true;
    p.compressibility_table_entries = 64;
    p.sys = nullptr;

    TestPrefetcher pf(p);

    // Unseen address should return 0 (unknown)
    EXPECT_EQ(pf.getPredictedCompressionFactor(0x1000), 0);
    EXPECT_EQ(pf.getPredictedCompressionFactor(0x2000), 0);
}

TEST(CompressibilityFilterTest, UpdateAndQueryHistory)
{
    BasePrefetcherParams p;
    p.block_size = 64;
    p.on_miss = true;
    p.on_read = true;
    p.on_write = true;
    p.on_data = true;
    p.on_inst = true;
    p.prefetch_on_access = false;
    p.prefetch_on_pf_hit = true;
    p.use_virtual_addresses = false;
    p.page_bytes = 4096;
    p.enable_compressibility_filter = true;
    p.compressibility_table_entries = 64;
    p.sys = nullptr;

    TestPrefetcher pf(p);

    Addr addr_uncomp = 0x1000;
    Addr addr_comp = 0x2000;

    // Record 1x (uncompressible) for addr_uncomp
    pf.updateCompressibilityHistory(addr_uncomp, 1);

    // Record 4x (compressible) for addr_comp
    pf.updateCompressibilityHistory(addr_comp, 4);

    EXPECT_EQ(pf.getPredictedCompressionFactor(addr_uncomp), 1);
    EXPECT_EQ(pf.getPredictedCompressionFactor(addr_comp), 4);
}

TEST(CompressibilityFilterTest, HistoryTableLRUEviction)
{
    BasePrefetcherParams p;
    p.block_size = 64;
    p.on_miss = true;
    p.on_read = true;
    p.on_write = true;
    p.on_data = true;
    p.on_inst = true;
    p.prefetch_on_access = false;
    p.prefetch_on_pf_hit = true;
    p.use_virtual_addresses = false;
    p.page_bytes = 4096;
    p.enable_compressibility_filter = true;
    p.compressibility_table_entries = 64;
    p.sys = nullptr;

    TestPrefetcher pf(p);

    // Insert 64 entries
    for (int i = 0; i < 64; ++i) {
        pf.updateCompressibilityHistory((i + 1) * 0x1000, 1);
    }

    // Verify entry 1 is present
    EXPECT_EQ(pf.getPredictedCompressionFactor(0x1000), 1);

    // Insert 65th entry, replacing LRU
    pf.updateCompressibilityHistory(65 * 0x1000, 2);

    // 65th entry should be present
    EXPECT_EQ(pf.getPredictedCompressionFactor(65 * 0x1000), 2);
}

TEST(CompressibilityFilterTest, CalculateDataCompressionFactor)
{
    BasePrefetcherParams p;
    p.block_size = 64;
    p.on_miss = true;
    p.on_read = true;
    p.on_write = true;
    p.on_data = true;
    p.on_inst = true;
    p.prefetch_on_access = false;
    p.prefetch_on_pf_hit = true;
    p.use_virtual_addresses = false;
    p.page_bytes = 4096;
    p.enable_compressibility_filter = true;
    p.compressibility_table_entries = 64;
    p.sys = nullptr;

    TestPrefetcher pf(p);

    struct DummyAccessor : public CacheAccessor
    {
        bool inCache(Addr addr, bool is_secure) const override { return false; }
        bool hasBeenPrefetched(Addr addr, bool is_secure) const override { return false; }
        bool hasBeenPrefetched(Addr addr, bool is_secure, RequestorID requestor) const override { return false; }
        bool inMissQueue(Addr addr, bool is_secure) const override { return false; }
        bool coalesce() const override { return false; }
    } acc;

    // Zero block
    uint8_t zero_data[64] = {0};
    EXPECT_GE(pf.calculateDataCompressionFactor(zero_data, acc), 2);

    // Random non-zero data (uncompressible)
    uint8_t random_data[64];
    for (int i = 0; i < 64; ++i) {
        random_data[i] = (uint8_t)(i * 37 + 11);
    }
    EXPECT_EQ(pf.calculateDataCompressionFactor(random_data, acc), 1);
}

} // namespace prefetch
} // namespace gem5
