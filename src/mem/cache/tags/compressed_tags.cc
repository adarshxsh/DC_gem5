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

#include <algorithm>
#include <map>

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

    // Check if the superblock this address belongs to has been allocated. If
    // so, try co-allocating
    SuperBlk* victim_superblock = nullptr;
    bool is_co_allocation = false;
    const uint64_t offset = extractSectorOffset(key.address);
    for (const auto& entry : superblock_entries){
        SuperBlk* superblock = static_cast<SuperBlk*>(entry);
        if (superblock->match(key) &&
            !superblock->blks[offset]->isValid() &&
            superblock->isCompressed() &&
            superblock->canCoAllocate(compressed_size))
        {
            victim_superblock = superblock;
            is_co_allocation = true;
            break;
        }
    }

    // If the superblock is not present or cannot be co-allocated a
    // superblock must be replaced
    if (victim_superblock == nullptr){
        // check if partitioning policy limited allocation and if true - return
        // this assumes that superblock_entries would not be empty if
        // partitioning policy is not in place
        if (superblock_entries.size() == 0){
            return nullptr;
        }

        // Choose replacement victim from replacement candidates scaled by valid sub-block count
        std::vector<ReplaceableEntry*> invalid_entries;
        for (const auto& entry : superblock_entries) {
            SuperBlk* superblock = static_cast<SuperBlk*>(entry);
            if (!superblock->isValid() || superblock->getNumValid() == 0) {
                invalid_entries.push_back(entry);
            }
        }

        if (!invalid_entries.empty()) {
            victim_superblock = static_cast<SuperBlk*>(
                replacementPolicy->getVictim(invalid_entries));
        } else {
            std::vector<ReplaceableEntry*> remaining_candidates = superblock_entries;
            std::map<ReplaceableEntry*, uint64_t> recency_ranks;
            uint64_t current_rank = remaining_candidates.size();

            while (!remaining_candidates.empty()) {
                ReplaceableEntry* worst = replacementPolicy->getVictim(remaining_candidates);
                recency_ranks[worst] = current_rank;
                if (current_rank > 0) {
                    current_rank--;
                }

                auto it = std::find(remaining_candidates.begin(), remaining_candidates.end(), worst);
                if (it != remaining_candidates.end()) {
                    remaining_candidates.erase(it);
                }
            }

            SuperBlk* best_victim = nullptr;
            uint64_t max_weight = 0;
            uint64_t max_recency_rank = 0;

            for (const auto& entry : superblock_entries) {
                SuperBlk* superblock = static_cast<SuperBlk*>(entry);
                uint8_t valid_count = superblock->getNumValid();
                uint8_t comp_factor = superblock->getCompressionFactor();
                (void)comp_factor;
                uint64_t recency_rank = recency_ranks[entry] * 1000;

                // Calculate effective weight = recency_rank / (1 + valid_count)
                uint64_t effective_weight = recency_rank / (1 + valid_count);

                if (best_victim == nullptr || effective_weight > max_weight ||
                    (effective_weight == max_weight && recency_ranks[entry] > max_recency_rank)) {
                    best_victim = superblock;
                    max_weight = effective_weight;
                    max_recency_rank = recency_ranks[entry];
                }
            }

            victim_superblock = best_victim;
        }

        // The whole superblock must be evicted to make room for the new one
        for (const auto& blk : victim_superblock->blks){
            if (blk->isValid()) {
                evict_blks.push_back(blk);
            }
        }
    }

    // Get the location of the victim block within the superblock
    SectorSubBlk* victim = victim_superblock->blks[offset];

    // It would be a hit if victim was valid in a co-allocation, and upgrades
    // do not call findVictim, so it cannot happen
    if (is_co_allocation){
        assert(!victim->isValid());

        // Print all co-allocated blocks
        DPRINTF(CacheComp, "Co-Allocation: offset %d of %s\n", offset,
                victim_superblock->print());
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
