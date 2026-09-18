/*
 * Copyright (c) 2012-2013, 2015-2016 ARM Limited
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
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
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

/** @file
 * Definition of WriteQueue class functions.
 */

#include "mem/cache/write_queue.hh"

#include <cassert>

#include "mem/cache/tags/super_blk.hh"
#include "mem/cache/write_queue_entry.hh"

namespace gem5
{

WriteQueue::WriteQueue(const std::string &_label,
                       int num_entries, int reserve, const std::string &name)
    : Queue<WriteQueueEntry>(_label, num_entries, reserve,
            name + ".write_queue")
{}

WriteQueueEntry *
WriteQueue::findMatch(Addr blk_addr, bool is_secure, bool ignore_uncacheable,
                      Addr super_blk_addr, const SuperBlk *super_blk) const
{
    for (const auto &entry : allocatedList) {
        if (ignore_uncacheable && entry->isUncacheable()) {
            continue;
        }

        // 1. Exact block address match
        if (entry->matchBlockAddr(blk_addr, is_secure)) {
            return entry;
        }

        // 2. Superblock base address match for unissued entries
        if (!entry->inService && !entry->isUncacheable()) {
            if (super_blk && entry->getSuperBlock() == super_blk &&
                entry->isSecure == is_secure) {
                return entry;
            }
            if (super_blk_addr != 0 && entry->getSuperBlockAddr() != 0 &&
                entry->getSuperBlockAddr() == super_blk_addr &&
                entry->isSecure == is_secure) {
                return entry;
            }
            if (entry->getSuperBlockAddr() != 0 &&
                entry->isSecure == is_secure) {
                Addr entry_sb = entry->getSuperBlockAddr();
                unsigned sb_num_blks =
                    (entry->getSuperBlock() &&
                     !entry->getSuperBlock()->blks.empty())
                        ? entry->getSuperBlock()->blks.size()
                        : 4;
                Addr entry_sb_end = entry_sb + (sb_num_blks * entry->blkSize);
                if ((super_blk_addr != 0 && super_blk_addr == entry_sb) ||
                    (blk_addr >= entry_sb && blk_addr < entry_sb_end)) {
                    return entry;
                }
            }
        }
    }
    return nullptr;
}

WriteQueueEntry *
WriteQueue::allocate(Addr blk_addr, unsigned blk_size, PacketPtr pkt,
                     Tick when_ready, Counter order, const SuperBlk *super_blk,
                     Addr super_blk_addr, int sub_blk_idx,
                     std::size_t comp_size)
{
    assert(!freeList.empty());
    WriteQueueEntry *entry = freeList.front();
    assert(entry->getNumTargets() == 0);
    freeList.pop_front();

    entry->allocate(blk_addr, blk_size, pkt, when_ready, order, super_blk,
                    super_blk_addr, sub_blk_idx, comp_size);
    entry->allocIter = allocatedList.insert(allocatedList.end(), entry);
    entry->readyIter = addToReadyList(entry);

    allocated += 1;
    return entry;
}

void
WriteQueue::markInService(WriteQueueEntry *entry)
{
    // for a normal eviction, such as a writeback or a clean evict,
    // there is no more to do as we are done from the perspective of
    // this cache, and for uncacheable write we do not need the entry
    // as part of the response handling
    entry->popTarget();
    if (!entry->hasTargets()) {
        deallocate(entry);
    }
}

} // namespace gem5
