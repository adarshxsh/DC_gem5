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

    SuperBlk* victim_superblock = nullptr;
    bool is_co_allocation = false;
    const uint64_t offset = extractSectorOffset(key.address);

    // Check if the superblock this address belongs to has been allocated.
    SuperBlk *matching_superblock = nullptr;
    for (const auto &entry : superblock_entries) {
        SuperBlk* superblock = static_cast<SuperBlk*>(entry);
        if (superblock->match(key)) {
            matching_superblock = superblock;
            break;
        }
    }

    if (matching_superblock) {
        if (!matching_superblock->blks[offset]->isValid()) {
            // Matching superblock exists and sub-block at offset is invalid
            // (Co-allocation)
            if (matching_superblock->isCompressed() &&
                matching_superblock->canCoAllocate(compressed_size)) {
                victim_superblock = matching_superblock;
                is_co_allocation = true;
            }
        } else {
            // Sub-block at offset is already valid. This occurs during data
            // updates / expansions. Check if matching_superblock can keep this
            // block at compressed_size.
            uint8_t new_blk_cf =
                matching_superblock->calculateCompressionFactor(
                    compressed_size);
            uint8_t hypothetical_cf = new_blk_cf;
            uint8_t num_valid = matching_superblock->getNumValid();
            for (const auto &blk : matching_superblock->blks) {
                if (blk->isValid() && blk->getSectorOffset() != offset) {
                    const CompressionBlk *cblk =
                        static_cast<const CompressionBlk *>(blk);
                    uint8_t cf =
                        matching_superblock->calculateCompressionFactor(
                            cblk->getSizeBits());
                    if (cf < hypothetical_cf) {
                        hypothetical_cf = cf;
                    }
                }
            }

            bool can_fit_in_place = false;
            if (num_valid == 1) {
                can_fit_in_place = true;
            } else if (hypothetical_cf > 1 && num_valid <= hypothetical_cf &&
                       compressed_size <=
                           (blkSize * CHAR_BIT) / hypothetical_cf) {
                can_fit_in_place = true;
            }

            if (can_fit_in_place) {
                victim_superblock = matching_superblock;
            } else {
                // Cannot fit in place. Search for an available free superblock
                // in the set for relocation.
                for (const auto &entry : superblock_entries) {
                    SuperBlk *superblock = static_cast<SuperBlk *>(entry);
                    if (!superblock->isValid()) {
                        victim_superblock = superblock;
                        break;
                    }
                }
            }
        }
    }

    // If the superblock is not present or cannot be co-allocated / relocated
    // in-place, a superblock must be replaced using the replacement policy
    if (victim_superblock == nullptr) {
        // Choose replacement victim from replacement candidates
        victim_superblock = static_cast<SuperBlk*>(
            replacementPolicy->getVictim(superblock_entries));

        // The whole victim superblock must be evicted to make room for the new
        // one
        for (const auto &blk : victim_superblock->blks) {
            if (blk->isValid()) {
                evict_blks.push_back(blk);
            }
        }
    }

    // Get the location of the victim block within the superblock
    SectorSubBlk* victim = victim_superblock->blks[offset];

    // It would be a hit if victim was valid in a co-allocation, and upgrades
    // do not call findVictim, so it cannot happen
    if (is_co_allocation) {
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
