/*
 * Copyright (c) 2023-2024 ARM Limited
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
 * Copyright (c) 2018 Inria
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
 * Definitions of a base set associative compressed superblocks tag store.
 */

#include "mem/cache/tags/compressed_tags.hh"

#include <limits>

#include "base/trace.hh"
#include "debug/CacheComp.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/indexing_policies/base.hh"
#include "mem/cache/tags/partitioning_policies/partition_manager.hh"
#include "mem/packet.hh"
#include "params/CompressedTags.hh"

namespace gem5
{

CompressedTags::CompressedTags(const Params &p)
    : SectorTags(p)
{
}

void
CompressedTags::tagsInit()
{
    // Create blocks and superblocks
    blks = std::vector<CompressionBlk>(numBlocks);
    superBlks = std::vector<SuperBlk>(numSectors);

    // Initialize all blocks
    unsigned blk_index = 0;          // index into blks array
    for (unsigned superblock_index = 0; superblock_index < numSectors;
         superblock_index++)
    {
        // Locate next cache superblock
        SuperBlk* superblock = &superBlks[superblock_index];

        // Superblocks must be aware of the block size due to their co-
        // allocation conditions
        superblock->setBlkSize(blkSize);

        // Associate a replacement data entry to the block
        superblock->replacementData = replacementPolicy->instantiateEntry();

        // Initialize all blocks in this superblock
        superblock->blks.resize(numBlocksPerSector, nullptr);
        for (unsigned k = 0; k < numBlocksPerSector; ++k){
            // Select block within the set to be linked
            SectorSubBlk*& blk = superblock->blks[k];

            // Locate next cache block
            blk = &blks[blk_index];

            // Associate a data chunk to the block
            blk->data = &dataBlks[blkSize*blk_index];

            // Associate superblock to this block
            blk->setSectorBlock(superblock);

            // Associate the superblock replacement data to this block
            blk->replacementData = superblock->replacementData;

            // Set its index and sector offset
            blk->setSectorOffset(k);

            // Register TagExtractor for SubBlk
            blk->registerTagExtractor(genTagExtractor(indexingPolicy));

            // Update block index
            ++blk_index;
        }

        // Link block to indexing policy
        indexingPolicy->setEntry(superblock, superblock_index);

        // Register TagExtractor for SuperBlk
        superblock->registerTagExtractor(genTagExtractor(indexingPolicy));
    }
}

CacheBlk*
CompressedTags::findVictim(const CacheBlk::KeyType& key,
                           const std::size_t compressed_size,
                           std::vector<CacheBlk*>& evict_blks,
                           const uint64_t partition_id=0)
{
    // Get all possible locations of this superblock
    std::vector<ReplaceableEntry*> superblock_entries =
        indexingPolicy->getPossibleEntries(key);

    // Filter entries based on PartitionID
    if (partitionManager){
        partitionManager->filterByPartition(superblock_entries,
            partition_id);
    }

    if (superblock_entries.empty()) {
        return nullptr;
    }

    const uint64_t offset = extractSectorOffset(key.address);

    SuperBlk *best_superblock = nullptr;
    bool best_is_co_allocation = false;
    std::size_t min_evictions = std::numeric_limits<std::size_t>::max();
    ssize_t max_net_space = std::numeric_limits<ssize_t>::lowest();
    std::size_t min_valid_count = std::numeric_limits<std::size_t>::max();
    std::vector<ReplaceableEntry *> tied_candidates;

    for (const auto &entry : superblock_entries) {
        SuperBlk* superblock = static_cast<SuperBlk*>(entry);
        bool is_match = superblock->match(key);

        // Count valid sub-blocks and sum used bits, excluding the sub-block
        // at 'offset' if superblock matches key (since it is being
        // updated/relocated)
        std::size_t valid_count = 0;
        std::size_t used_bits = 0;
        uint8_t min_cf_other = superblock->blks.size();

        for (const auto &sub_blk : superblock->blks) {
            if (sub_blk->isValid()) {
                bool is_target_sub =
                    is_match && (sub_blk->getSectorOffset() == offset);
                if (!is_target_sub) {
                    valid_count++;
                    const CompressionBlk *cblk =
                        static_cast<const CompressionBlk *>(sub_blk);
                    used_bits += cblk->getSizeBits();
                    uint8_t cf = superblock->calculateCompressionFactor(
                        cblk->getSizeBits());
                    if (cf < min_cf_other) {
                        min_cf_other = cf;
                    }
                }
            }
        }

        std::size_t total_capacity = superblock->getBlkSizeBits();
        std::size_t free_bit_capacity =
            (total_capacity > used_bits) ? (total_capacity - used_bits) : 0;
        ssize_t net_available_space = static_cast<ssize_t>(free_bit_capacity) -
                                      static_cast<ssize_t>(compressed_size);

        // Determine if co-allocation is possible without secondary evictions
        bool can_coallocate = false;
        if (!superblock->isValid() || valid_count == 0) {
            // Empty superblock can always co-allocate if size fits
            can_coallocate = (compressed_size <= total_capacity);
        } else if (is_match && superblock->isCompressed()) {
            uint8_t new_blk_cf =
                superblock->calculateCompressionFactor(compressed_size);
            uint8_t target_cf = std::min(min_cf_other, new_blk_cf);
            if (target_cf > 1 && (valid_count + 1) <= target_cf &&
                compressed_size <= (total_capacity / target_cf)) {
                can_coallocate = true;
            }
        }

        std::size_t req_evictions = can_coallocate ? 0 : valid_count;
        bool is_co_alloc = can_coallocate && (valid_count > 0 || is_match);

        // Rank candidate superblocks:
        // 1. Minimize required secondary evictions
        // 2. Maximize net available bit space (free_bit_capacity -
        // compressed_size)
        // 3. Minimize valid sub-block count (destination sub-block density /
        // sparsity)
        if (!best_superblock || req_evictions < min_evictions ||
            (req_evictions == min_evictions &&
             net_available_space > max_net_space) ||
            (req_evictions == min_evictions &&
             net_available_space == max_net_space &&
             valid_count < min_valid_count)) {
            best_superblock = superblock;
            best_is_co_allocation = is_co_alloc;
            min_evictions = req_evictions;
            max_net_space = net_available_space;
            min_valid_count = valid_count;
            tied_candidates.clear();
            tied_candidates.push_back(entry);
        } else if (req_evictions == min_evictions &&
                   net_available_space == max_net_space &&
                   valid_count == min_valid_count) {
            tied_candidates.push_back(entry);
        }
    }

    SuperBlk *victim_superblock = best_superblock;
    if (tied_candidates.size() > 1) {
        victim_superblock = static_cast<SuperBlk *>(
            replacementPolicy->getVictim(tied_candidates));
    }

    // Populate evict_blks with valid sub-blocks to be evicted if replacement
    // is required
    if (min_evictions > 0) {
        for (const auto &blk : victim_superblock->blks) {
            if (blk->isValid()) {
                bool is_target_sub = victim_superblock->match(key) &&
                                     (blk->getSectorOffset() == offset);
                if (!is_target_sub) {
                    evict_blks.push_back(blk);
                }
            }
        }
    }

    // Get the location of the victim block within the superblock
    SectorSubBlk* victim = victim_superblock->blks[offset];

    if (best_is_co_allocation) {
        if (!victim->isValid()) {
            DPRINTF(CacheComp, "Co-Allocation: offset %d of %s\n", offset,
                    victim_superblock->print());
        }
    }

    // Update number of sub-blocks evicted due to a replacement
    sectorStats.evictionsReplacement[evict_blks.size()]++;

    return victim;
}

bool
CompressedTags::anyBlk(std::function<bool(CacheBlk &)> visitor)
{
    for (CompressionBlk& blk : blks) {
        if (visitor(blk)) {
            return true;
        }
    }
    return false;
}

bool
CompressedTags::checkInvariants() const
{
    SectorTags::checkInvariants();
    for (const auto &super_blk : superBlks) {
        if (super_blk.isValid()) {
            uint8_t num_valid = super_blk.getNumValid();
            uint8_t cf = super_blk.getCompressionFactor();
            assert(num_valid <= cf);
            if (num_valid > 1) {
                assert(super_blk.isCompressed());
            }
            for (const auto &blk : super_blk.blks) {
                if (blk->isValid()) {
                    const CompressionBlk *cblk =
                        static_cast<const CompressionBlk *>(blk);
                    uint8_t blk_cf = super_blk.calculateCompressionFactor(
                        cblk->getSizeBits());
                    assert(blk_cf >= cf);
                }
            }
        } else {
            assert(super_blk.getCompressionFactor() == 1);
        }
    }
    return true;
}

} // namespace gem5
