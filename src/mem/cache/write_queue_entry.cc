/*
 * Copyright (c) 2012-2013, 2015-2017 ARM Limited
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
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
 * Copyright (c) 2010 Advanced Micro Devices, Inc.
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
 * Miss Status and Handling Register (WriteQueueEntry) definitions.
 */

#include "mem/cache/write_queue_entry.hh"

#include <cassert>
#include <string>

#include "base/logging.hh"
#include "base/types.hh"
#include "mem/cache/base.hh"
#include "mem/request.hh"

namespace gem5
{

inline void
WriteQueueEntry::TargetList::add(PacketPtr pkt, Tick readyTime,
                                 Counter order)
{
    emplace_back(pkt, readyTime, order);
}

bool
WriteQueueEntry::TargetList::trySatisfyFunctional(PacketPtr pkt)
{
    for (auto& t : *this) {
        if (pkt->trySatisfyFunctional(t.pkt)) {
            return true;
        }
    }

    return false;
}

void
WriteQueueEntry::TargetList::print(std::ostream &os, int verbosity,
                                   const std::string &prefix) const
{
    for (auto& t : *this) {
        ccprintf(os, "%sFromCPU: ", prefix);
        t.pkt->print(os, verbosity, "");
    }
}

void
WriteQueueEntry::allocate(Addr blk_addr, unsigned blk_size, PacketPtr target,
                          Tick when_ready, Counter _order)
{
    blkAddr = blk_addr;
    blkSize = blk_size;
    isSecure = target->isSecure();
    readyTime = when_ready;
    order = _order;
    assert(target);
    _isUncacheable = target->req->isUncacheable();
    inService = false;

    // Initialize subBlockDirtyMask
    unsigned num_sub_blks = 8;
    unsigned sub_blk_size = blkSize / num_sub_blks;
    if (sub_blk_size == 0) {
        sub_blk_size = 8;
        num_sub_blks = blkSize / sub_blk_size;
        if (num_sub_blks == 0) {
            num_sub_blks = 1;
        }
    }
    subBlockDirtyMask.assign(num_sub_blks, false);

    const Request::FlagsType special_flags =
        Request::UNCACHEABLE | Request::STRICT_ORDER | Request::PRIVILEGED |
        Request::LLSC | Request::MEM_SWAP | Request::MEM_SWAP_COND |
        Request::SECURE | Request::LOCKED_RMW;
    bool is_special = target->req->getFlags().isSet(special_flags);

    if (_isUncacheable || is_special ||
        target->cmd == MemCmd::WritebackDirty ||
        target->getSize() >= blkSize) {
        std::fill(subBlockDirtyMask.begin(), subBlockDirtyMask.end(), true);
    } else if (target->cmd == MemCmd::CleanEvict) {
        std::fill(subBlockDirtyMask.begin(), subBlockDirtyMask.end(), false);
    } else {
        auto offset = target->getOffset(blkSize);
        auto size = target->getSize();
        unsigned start_sub = offset / sub_blk_size;
        unsigned end_sub = (offset + size - 1) / sub_blk_size;
        for (unsigned i = start_sub; i <= end_sub && i < num_sub_blks; ++i) {
            subBlockDirtyMask[i] = true;
        }
    }

    // we should never have more than a single target for cacheable
    // writes (writebacks and clean evictions)
    panic_if(!_isUncacheable && !targets.empty(),
             "Write queue entry %#llx should never have more than one "
             "cacheable target", blkAddr);
    panic_if(!((target->isWrite() && _isUncacheable) ||
               (target->isEviction() && !_isUncacheable) ||
               target->cmd == MemCmd::WriteClean),
             "Write queue entry %#llx should be an uncacheable write or "
             "a cacheable eviction or a writeclean");

    targets.add(target, when_ready, _order);

    // All targets must refer to the same block
    assert(target->matchBlockAddr(targets.front().pkt, blkSize));
}

void
WriteQueueEntry::deallocate()
{
    assert(targets.empty());
    inService = false;
    subBlockDirtyMask.clear();
}

bool
WriteQueueEntry::trySatisfyFunctional(PacketPtr pkt)
{
    // For printing, we treat the WriteQueueEntry as a whole as single
    // entity. For other requests, we iterate over the individual
    // targets since that's where the actual data lies.
    if (pkt->isPrint()) {
        pkt->trySatisfyFunctional(this, blkAddr, isSecure, blkSize, nullptr);
        return false;
    } else {
        if (!subBlockDirtyMask.empty() &&
            pkt->matchBlockAddr(blkAddr, isSecure, blkSize)) {
            unsigned num_sub_blks = subBlockDirtyMask.size();
            unsigned sub_blk_size = blkSize / num_sub_blks;
            if (sub_blk_size > 0) {
                auto offset = pkt->getOffset(blkSize);
                auto size = pkt->getSize();
                unsigned start_sub = offset / sub_blk_size;
                unsigned end_sub = (offset + size - 1) / sub_blk_size;

                bool overlaps_dirty = false;
                for (unsigned i = start_sub; i <= end_sub && i < num_sub_blks;
                     ++i) {
                    if (subBlockDirtyMask[i]) {
                        overlaps_dirty = true;
                        break;
                    }
                }
                if (!overlaps_dirty) {
                    return false;
                }
            }
        }
        return targets.trySatisfyFunctional(pkt);
    }
}

bool
WriteQueueEntry::sendPacket(BaseCache &cache)
{
    return cache.sendWriteQueuePacket(this);
}

bool
WriteQueueEntry::matchBlockAddr(const Addr addr, const bool is_secure) const
{
    assert(hasTargets());
    return (blkAddr == addr) && (isSecure == is_secure);
}

bool
WriteQueueEntry::matchBlockAddr(const PacketPtr pkt) const
{
    assert(hasTargets());
    if (!pkt->matchBlockAddr(blkAddr, isSecure, blkSize)) {
        return false;
    }

    if (_isUncacheable || subBlockDirtyMask.empty()) {
        return true;
    }

    unsigned num_sub_blks = subBlockDirtyMask.size();
    unsigned sub_blk_size = blkSize / num_sub_blks;
    if (sub_blk_size > 0) {
        auto offset = pkt->getOffset(blkSize);
        auto size = pkt->getSize();
        unsigned start_sub = offset / sub_blk_size;
        unsigned end_sub = (offset + size - 1) / sub_blk_size;

        for (unsigned i = start_sub; i <= end_sub && i < num_sub_blks; ++i) {
            if (subBlockDirtyMask[i]) {
                return true;
            }
        }
        return false;
    }

    return true;
}

bool
WriteQueueEntry::conflictAddr(const QueueEntry* entry) const
{
    assert(hasTargets());
    return entry->matchBlockAddr(blkAddr, isSecure);
}

void
WriteQueueEntry::print(std::ostream &os, int verbosity,
                       const std::string &prefix) const
{
    ccprintf(os, "%s[%#llx:%#llx](%s) %s %s %s state: %s %s %s %s %s\n",
             prefix, blkAddr, blkAddr + blkSize - 1,
             isSecure ? "s" : "ns",
             _isUncacheable ? "Unc" : "",
             inService ? "InSvc" : "");

    ccprintf(os, "%s  Targets:\n", prefix);
    targets.print(os, verbosity, prefix + "    ");
}

std::string
WriteQueueEntry::print() const
{
    std::ostringstream str;
    print(str);
    return str.str();
}

} // namespace gem5
