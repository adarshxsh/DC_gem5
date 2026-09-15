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

#include "mem/cache/replacement_policies/superblock_density_rp.hh"

#include <cassert>
#include <memory>

#include "mem/cache/tags/super_blk.hh"
#include "params/SuperblockDensityRP.hh"

namespace gem5
{

namespace replacement_policy
{

SuperblockDensity::SuperblockDensity(const Params &p)
    : LRU(p)
{
}

ReplaceableEntry*
SuperblockDensity::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Visit all candidates to find victim with lowest sub-block density,
    // using last touch timestamp as a secondary tie-breaker.
    ReplaceableEntry* victim = candidates[0];
    for (const auto& candidate : candidates) {
        const SuperBlk* cand_sb = dynamic_cast<const SuperBlk*>(candidate);
        const SuperBlk* vict_sb = dynamic_cast<const SuperBlk*>(victim);

        if (cand_sb && vict_sb) {
            uint8_t cand_valid = cand_sb->getNumValid();
            uint8_t vict_valid = vict_sb->getNumValid();

            if (cand_valid < vict_valid) {
                victim = candidate;
            } else if (cand_valid == vict_valid) {
                if (std::static_pointer_cast<LRUReplData>(
                            candidate->replacementData)->lastTouchTick <
                    std::static_pointer_cast<LRUReplData>(
                            victim->replacementData)->lastTouchTick) {
                    victim = candidate;
                }
            }
        } else {
            // Fallback to standard recency evaluation if candidate entries
            // are not SuperBlk instances.
            if (std::static_pointer_cast<LRUReplData>(
                        candidate->replacementData)->lastTouchTick <
                std::static_pointer_cast<LRUReplData>(
                        victim->replacementData)->lastTouchTick) {
                victim = candidate;
            }
        }
    }

    return victim;
}

} // namespace replacement_policy
} // namespace gem5
