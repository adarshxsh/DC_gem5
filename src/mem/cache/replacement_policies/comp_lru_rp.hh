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

/**
 * @file
 * Declaration of Compression-Density-Aware LRU Replacement Policy.
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_COMP_LRU_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_COMP_LRU_RP_HH__

#include <cstdint>
#include <memory>

#include "mem/cache/replacement_policies/lru_rp.hh"

namespace gem5
{

struct CompLRURPParams;
struct DensityAwareLRURPParams;

namespace replacement_policy
{

/**
 * Compression-density-aware replacement data entry.
 * Incorporates compression factor and valid sub-block count alongside LRU timestamp.
 */
struct CompLRUReplData : LRUReplData
{
    /** Compression factor of the superblock / entry. */
    uint8_t compressionFactor;

    /** Number of valid sub-blocks in the superblock / entry. */
    uint8_t validBlocks;

    CompLRUReplData()
        : LRUReplData(), compressionFactor(1), validBlocks(0)
    {}
};

/**
 * Compression-density-aware LRU replacement policy (CompLRU).
 * Calculates victim scores based on superblock compression factors and valid sub-block counts.
 */
class CompLRU : public LRU
{
  public:
    typedef CompLRURPParams Params;
    CompLRU(const Params &p);
    ~CompLRU() = default;

    /**
     * Find replacement victim using density-aware scoring.
     *
     * @param candidates Replacement candidates.
     * @return Entry chosen for replacement.
     */
    ReplaceableEntry* getVictim(
        const ReplacementCandidates& candidates) const override;

    /**
     * Instantiate replacement data entry.
     *
     * @return Shared pointer to new CompLRUReplData.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;
};

/**
 * DensityAwareLRU replacement policy.
 * Inherits density-aware replacement behavior from CompLRU.
 */
class DensityAwareLRU : public CompLRU
{
  public:
    typedef DensityAwareLRURPParams Params;
    DensityAwareLRU(const Params &p);
    ~DensityAwareLRU() = default;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_COMP_LRU_RP_HH__
