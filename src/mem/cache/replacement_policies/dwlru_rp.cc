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

#include "mem/cache/replacement_policies/dwlru_rp.hh"

#include <cassert>
#include <limits>
#include <memory>

#include "mem/cache/tags/sector_blk.hh"
#include "params/DWLRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

namespace replacement_policy
{

DWLRU::DWLRU(const Params &p)
    : LRU(p),
      queuePressure(0.0),
      queuePressureThreshold(p.queue_pressure_threshold),
      writebackPenaltyWeight(p.writeback_penalty_weight)
{}

void
DWLRU::invalidate(const std::shared_ptr<ReplacementData> &replacement_data)
{
    LRU::invalidate(replacement_data);
    auto dw_data = std::static_pointer_cast<DWLRUReplData>(replacement_data);
    dw_data->validSubBlkCount = 0;
    dw_data->dirtySubBlkCount = 0;
}

void
DWLRU::touch(const std::shared_ptr<ReplacementData> &replacement_data) const
{
    LRU::touch(replacement_data);
}

void
DWLRU::reset(const std::shared_ptr<ReplacementData> &replacement_data) const
{
    LRU::reset(replacement_data);
}

void
DWLRU::setQueuePressure(double pressure) const
{
    queuePressure = pressure;
}

void
DWLRU::setMemWriteQueuePressure(double pressure) const
{
    queuePressure = pressure;
}

double
DWLRU::getQueuePressure() const
{
    return queuePressure;
}

double
DWLRU::getMemWriteQueuePressure() const
{
    return queuePressure;
}

ReplaceableEntry *
DWLRU::getVictim(const ReplacementCandidates &candidates) const
{
    assert(candidates.size() > 0);

    ReplaceableEntry *victim = candidates[0];
    double max_score = -1.0;

    for (const auto &candidate : candidates) {
        auto candidate_data = std::static_pointer_cast<DWLRUReplData>(
            candidate->replacementData);

        // Synchronize validSubBlkCount and maxSubBlks from SectorBlk if
        // available
        SectorBlk *sec_blk = dynamic_cast<SectorBlk *>(candidate);
        if (sec_blk) {
            candidate_data->validSubBlkCount = sec_blk->getNumValid();
            if (!sec_blk->blks.empty()) {
                candidate_data->maxSubBlks = sec_blk->blks.size();
            }
            int actual_dirty = 0;
            bool has_real_blks = false;
            for (auto *sub_blk : sec_blk->blks) {
                if (sub_blk) {
                    has_real_blks = true;
                    if (sub_blk->isValid() &&
                        sub_blk->isSet(CacheBlk::DirtyBit)) {
                        actual_dirty++;
                    }
                }
            }
            if (has_real_blks) {
                candidate_data->dirtySubBlkCount = actual_dirty;
            }
        } else {
            CacheBlk *cache_blk = dynamic_cast<CacheBlk *>(candidate);
            if (cache_blk) {
                if (cache_blk->isValid() &&
                    cache_blk->isSet(CacheBlk::DirtyBit)) {
                    candidate_data->dirtySubBlkCount =
                        std::max(1, candidate_data->validSubBlkCount);
                } else {
                    candidate_data->dirtySubBlkCount = 0;
                }
            }
        }

        int valid_count = candidate_data->validSubBlkCount;
        int max_blks =
            candidate_data->maxSubBlks > 0 ? candidate_data->maxSubBlks : 1;
        int dirty_count = candidate_data->dirtySubBlkCount;

        double score;
        if (valid_count <= 0) {
            // Invalid entries are highest priority for replacement
            score = std::numeric_limits<double>::max();
        } else {
            double density = static_cast<double>(valid_count) / max_blks;
            Tick age = curTick() - candidate_data->lastTouchTick;
            score = static_cast<double>(age + 1) / density;

            if (queuePressure > queuePressureThreshold && dirty_count > 0) {
                double excess_pressure =
                    queuePressure - queuePressureThreshold;
                double penalty_factor = 1.0 + (writebackPenaltyWeight *
                                               excess_pressure * dirty_count);
                score /= penalty_factor;
            }
        }

        if (score > max_score) {
            max_score = score;
            victim = candidate;
        } else if (score == max_score) {
            // Tie breaker: evict candidate with smaller lastTouchTick (older)
            auto victim_data = std::static_pointer_cast<DWLRUReplData>(
                victim->replacementData);
            if (candidate_data->lastTouchTick < victim_data->lastTouchTick) {
                victim = candidate;
            }
        }
    }

    return victim;
}

std::shared_ptr<ReplacementData>
DWLRU::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new DWLRUReplData());
}

} // namespace replacement_policy
} // namespace gem5
