/*
 * Copyright (c) 2013-2014, 2022-2025 Arm Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2005 The Regents of The University of Michigan
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

/**
 * @file
 * Hardware Prefetcher Definition.
 */

#include "mem/cache/prefetch/base.hh"

#include <cassert>

#include "base/intmath.hh"
#include "debug/HWPrefetch.hh"
#include "mem/cache/compressors/base.hh"
#include "params/BasePrefetcher.hh"
#include "sim/system.hh"

namespace gem5
{

namespace prefetch
{

Base::PrefetchInfo::PrefetchInfo(PacketPtr pkt, Addr addr, bool miss)
  : address(addr), pc(pkt->req->hasPC() ? pkt->req->getPC() : 0),
    requestorId(pkt->req->requestorId()), validPC(pkt->req->hasPC()),
    secure(pkt->isSecure()), size(pkt->req->getSize()), write(pkt->isWrite()),
    paddress(pkt->req->getPaddr()), cacheMiss(miss)
{
    unsigned int req_size = pkt->req->getSize();
    if ((!write && miss) || !pkt->hasData()) {
        data = nullptr;
    } else {
        data = new uint8_t[req_size];
        Addr offset = pkt->req->getPaddr() - pkt->getAddr();
        std::memcpy(data, &(pkt->getConstPtr<uint8_t>()[offset]), req_size);
    }
}

Base::PrefetchInfo::PrefetchInfo(PrefetchInfo const &pfi, Addr addr)
  : address(addr), pc(pfi.pc), requestorId(pfi.requestorId),
    validPC(pfi.validPC), secure(pfi.secure), size(pfi.size),
    write(pfi.write), paddress(pfi.paddress), cacheMiss(pfi.cacheMiss),
    data(nullptr)
{
}

void
Base::PrefetchListener::notify(const CacheAccessProbeArg &arg)
{
    if (isFill) {
        parent.notifyFill(arg);
    } else {
        parent.probeNotify(arg, miss);
    }
}

void
Base::PrefetchEvictListener::notify(const EvictionInfo &info)
{
    if (info.newData.empty())
        parent.notifyEvict(info);
}

Base::Base(const BasePrefetcherParams &p)
    : ClockedObject(p),
      listeners(),
      system(nullptr),
      probeManager(nullptr),
      blkSize(p.block_size),
      lBlkSize(floorLog2(blkSize)),
      onMiss(p.on_miss),
      onRead(p.on_read),
      onWrite(p.on_write),
      onData(p.on_data),
      onInst(p.on_inst),
      requestorId(p.sys->getRequestorId(this)),
      pageBytes(p.page_bytes),
      prefetchOnAccess(p.prefetch_on_access),
      prefetchOnPfHit(p.prefetch_on_pf_hit),
      useVirtualAddresses(p.use_virtual_addresses),
      prefetchStats(this),
      enableCompressibilityFilter(p.enable_compressibility_filter),
      compressibilityTableEntries(p.compressibility_table_entries),
      compressibilityHistoryTable(p.compressibility_table_entries),
      issuedPrefetches(0),
      usefulPrefetches(0),
      mmu(nullptr)
{
}

void
Base::setParentInfo(System *sys, ProbeManager *pm, unsigned blk_size)
{
    assert(!system && !probeManager);
    system = sys;
    probeManager = pm;
    // If the cache has a different block size from the system's, save it
    blkSize = blk_size;
    lBlkSize = floorLog2(blkSize);
}

Base::StatGroup::StatGroup(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(demandMshrMisses, statistics::units::Count::get(),
               "demands not covered by prefetchs"),
      ADD_STAT(pfIssued, statistics::units::Count::get(),
               "number of hwpf issued"),
      ADD_STAT(pfUnused, statistics::units::Count::get(),
               "number of HardPF blocks evicted w/o reference"),
      ADD_STAT(pfUseful, statistics::units::Count::get(),
               "number of useful prefetch"),
      ADD_STAT(pfUsefulButMiss, statistics::units::Count::get(),
               "number of hit on prefetch but cache block is not in an usable "
               "state"),
      ADD_STAT(accuracy, statistics::units::Count::get(),
               "accuracy of the prefetcher"),
      ADD_STAT(coverage, statistics::units::Count::get(),
               "coverage brought by this prefetcher"),
      ADD_STAT(pfHitInCache, statistics::units::Count::get(),
               "number of prefetches hitting in cache"),
      ADD_STAT(pfHitInMSHR, statistics::units::Count::get(),
               "number of prefetches hitting in a MSHR"),
      ADD_STAT(pfHitInWB, statistics::units::Count::get(),
               "number of prefetches hit in the Write Buffer"),
      ADD_STAT(pfLate, statistics::units::Count::get(),
               "number of late prefetches (hitting in cache, MSHR or WB)"),
      ADD_STAT(pfFilteredUncompressible, statistics::units::Count::get(),
               "number of uncompressible prefetches suppressed by filter")
{
    using namespace statistics;

    pfUnused.flags(nozero);

    accuracy.flags(total);
    accuracy = pfUseful / pfIssued;

    coverage.flags(total);
    coverage = pfUseful / (pfUseful + demandMshrMisses);

    pfLate = pfHitInCache + pfHitInMSHR + pfHitInWB;
}

bool
Base::observeAccess(const PacketPtr &pkt, bool miss, bool prefetched) const
{
    bool fetch = pkt->req->isInstFetch();
    bool read = pkt->isRead();
    bool inv = pkt->isInvalidate();

    if (!miss) {
        if (prefetchOnPfHit)
            return prefetched;
        if (!prefetchOnAccess)
            return false;
    }
    if (pkt->req->isUncacheable()) return false;
    if (fetch && !onInst) return false;
    if (!fetch && !onData) return false;
    if (!fetch && read && !onRead) return false;
    if (!fetch && !read && !onWrite) return false;
    if (!fetch && !read && inv) return false;
    if (pkt->cmd == MemCmd::CleanEvict) return false;

    if (onMiss) {
        return miss;
    }

    return true;
}

bool
Base::samePage(Addr a, Addr b) const
{
    return roundDown(a, pageBytes) == roundDown(b, pageBytes);
}

Addr
Base::blockAddress(Addr a) const
{
    return a & ~((Addr)blkSize-1);
}

Addr
Base::blockIndex(Addr a) const
{
    return a >> lBlkSize;
}

Addr
Base::pageAddress(Addr a) const
{
    return roundDown(a, pageBytes);
}

Addr
Base::pageOffset(Addr a) const
{
    return a & (pageBytes - 1);
}

Addr
Base::pageIthBlockAddress(Addr page, uint32_t blockIndex) const
{
    return page + (blockIndex << lBlkSize);
}

uint8_t
Base::calculateDataCompressionFactor(const uint8_t *data,
                                     const CacheAccessor &cache) const
{
    if (!data) {
        return 1;
    }

    std::size_t uncomp_bits = blkSize * 8;
    std::size_t comp_bits = uncomp_bits;

    compression::Base *compressor = cache.getCompressor();
    if (compressor) {
        Cycles comp_lat(0), decomp_lat(0);
        auto comp_data = compressor->compress(
            reinterpret_cast<const uint64_t *>(data), comp_lat, decomp_lat);
        if (comp_data) {
            comp_bits = comp_data->getSizeBits();
        }
    } else {
        const uint64_t *qwords = reinterpret_cast<const uint64_t *>(data);
        size_t num_qwords = blkSize / sizeof(uint64_t);
        bool all_zero = true;
        bool all_equal = true;
        for (size_t i = 0; i < num_qwords; ++i) {
            if (qwords[i] != 0) {
                all_zero = false;
            }
            if (qwords[i] != qwords[0]) {
                all_equal = false;
            }
        }
        if (all_zero) {
            comp_bits = 64;
        } else if (all_equal) {
            comp_bits = 128;
        } else {
            int zero_count = 0;
            for (size_t i = 0; i < blkSize; ++i) {
                if (data[i] == 0) {
                    zero_count++;
                }
            }
            if (zero_count >= (int)(blkSize * 3 / 4)) {
                comp_bits = 256;
            }
        }
    }

    if (comp_bits > uncomp_bits / 2) {
        return 1;
    } else if (comp_bits <= uncomp_bits / 4) {
        return 4;
    } else {
        return 2;
    }
}

uint8_t
Base::getPredictedCompressionFactor(Addr addr) const
{
    if (!enableCompressibilityFilter || compressibilityTableEntries == 0) {
        return 0;
    }

    Addr target_tag = blockAddress(addr);
    for (const auto &entry : compressibilityHistoryTable) {
        if (entry.valid && entry.tag == target_tag) {
            return entry.predicted_cf;
        }
    }
    return 0;
}

void
Base::updateCompressibilityHistory(Addr addr, uint8_t cf)
{
    if (!enableCompressibilityFilter || compressibilityTableEntries == 0) {
        return;
    }

    Addr target_tag = blockAddress(addr);
    Tick now = curTick();

    for (auto &entry : compressibilityHistoryTable) {
        if (entry.valid && entry.tag == target_tag) {
            entry.predicted_cf = cf;
            entry.last_used = now;
            return;
        }
    }

    size_t lru_idx = 0;
    Tick oldest_tick = MaxTick;

    for (size_t i = 0; i < compressibilityHistoryTable.size(); ++i) {
        if (!compressibilityHistoryTable[i].valid) {
            lru_idx = i;
            break;
        }
        if (compressibilityHistoryTable[i].last_used < oldest_tick) {
            oldest_tick = compressibilityHistoryTable[i].last_used;
            lru_idx = i;
        }
    }

    if (lru_idx < compressibilityHistoryTable.size()) {
        compressibilityHistoryTable[lru_idx].valid = true;
        compressibilityHistoryTable[lru_idx].tag = target_tag;
        compressibilityHistoryTable[lru_idx].predicted_cf = cf;
        compressibilityHistoryTable[lru_idx].last_used = now;
    }
}

void
Base::probeNotify(const CacheAccessProbeArg &acc, bool miss)
{
    const PacketPtr pkt = acc.pkt;
    const CacheAccessor &cache = acc.cache;

    // Don't notify prefetcher on SWPrefetch, cache maintenance
    // operations or for writes that we are coaslescing.
    if (pkt->cmd.isSWPrefetch()) return;
    if (pkt->req->isCacheMaintenance()) return;
    if (pkt->isCleanEviction()) return;
    if (pkt->isWrite() && cache.coalesce()) return;
    if (!pkt->req->hasPaddr()) {
        panic("Request must have a physical address");
    }

    if (enableCompressibilityFilter && pkt->hasData() &&
        pkt->getConstPtr<uint8_t>()) {
        uint8_t obs_cf =
            calculateDataCompressionFactor(pkt->getConstPtr<uint8_t>(), cache);
        updateCompressibilityHistory(pkt->getAddr(), obs_cf);
    }

    bool has_been_prefetched =
        acc.cache.hasBeenPrefetched(pkt->getAddr(), pkt->isSecure(),
                                    requestorId);
    if (has_been_prefetched) {
        usefulPrefetches += 1;
        prefetchStats.pfUseful++;
        if (miss)
            // This case happens when a demand hits on a prefetched line
            // that's not in the requested coherency state.
            prefetchStats.pfUsefulButMiss++;
    }

    // Verify this access type is observed by prefetcher
    if (observeAccess(pkt, miss, has_been_prefetched)) {
        if (useVirtualAddresses && pkt->req->hasVaddr()) {
            PrefetchInfo pfi(pkt, pkt->req->getVaddr(), miss);
            notify(acc, pfi);
        } else if (!useVirtualAddresses) {
            PrefetchInfo pfi(pkt, pkt->req->getPaddr(), miss);
            notify(acc, pfi);
        }
    }
}

void
Base::regProbeListeners()
{
    /**
     * If no probes were added by the configuration scripts, connect to the
     * parent cache using the probe "Miss". Also connect to "Hit", if the
     * cache is configured to prefetch on accesses.
     */
    if (listeners.empty() && probeManager != nullptr) {
        listeners.push_back(probeManager->connect<PrefetchListener>(
            *this, "Miss", false, true));
        listeners.push_back(probeManager->connect<PrefetchListener>(
            *this, "Fill", true, false));
        listeners.push_back(probeManager->connect<PrefetchListener>(
            *this, "Hit", false, false));
        listeners.push_back(probeManager->connect<PrefetchEvictListener>(
            *this, "Data Update"));
    }
}

void
Base::addEventProbe(SimObject *obj, const char *name)
{
    ProbeManager *pm = obj->getProbeManager();
    listeners.push_back(pm->connect<PrefetchListener>(*this, name));
}

void
Base::addMMU(BaseMMU *m)
{
    fatal_if(mmu != nullptr, "Only one MMU can be registered");
    mmu = m;
}

} // namespace prefetch
} // namespace gem5
