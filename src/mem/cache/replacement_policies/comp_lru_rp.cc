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

#include "mem/cache/replacement_policies/comp_lru_rp.hh"

#include <cassert>
#include <cstdint>
#include <memory>

#include "mem/cache/tags/super_blk.hh"
#include "params/CompLRURP.hh"
#include "params/DensityAwareLRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

CompLRU::CompLRU(const Params &p) : LRU(p)
{}

DensityAwareLRU::DensityAwareLRU(const Params &p) : CompLRU(p)
{}

std::shared_ptr<ReplacementData>
CompLRU::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new CompLRUReplData());
}

ReplaceableEntry *
CompLRU::getVictim(const ReplacementCandidates &candidates) const
{
    assert(candidates.size() > 0);

    ReplaceableEntry *victim = candidates[0];
    uint64_t min_score = UINT64_MAX;

    for (const auto &candidate : candidates) {
        std::shared_ptr<CompLRUReplData> data =
            std::dynamic_pointer_cast<CompLRUReplData>(
                candidate->replacementData);

        // Sync attributes from SuperBlk if candidate is a SuperBlk
        SuperBlk *sb = dynamic_cast<SuperBlk *>(candidate);
        if (sb && data) {
            data->compressionFactor = sb->getCompressionFactor();
            data->validBlocks = sb->getNumValid();
        }

        uint8_t cf = data ? data->compressionFactor : 1;
        uint8_t valid = data ? data->validBlocks : 1;

        // An invalid entry with 0 valid sub-blocks is prioritized for
        // replacement
        if (valid == 0) {
            return candidate;
        }

        Tick touch_tick = data ? data->lastTouchTick : Tick(0);
        uint64_t score = static_cast<uint64_t>(touch_tick) * cf * valid;

        if (score < min_score) {
            min_score = score;
            victim = candidate;
        }
    }

    return victim;
}

} // namespace replacement_policy
} // namespace gem5
