/*
 * Copyright (c) 2019-2020 Inria
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
 * Implementation of the a multi compressor that choses the best compression
 * among multiple compressors.
 */

#include "mem/cache/compressors/multi.hh"

#include <cmath>
#include <queue>

#include "base/bitfield.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/CacheComp.hh"
#include "params/MultiCompressor.hh"

namespace gem5
{

namespace compression
{

Multi::MultiCompData::MultiCompData(unsigned index,
    std::unique_ptr<Base::CompressionData> comp_data)
    : CompressionData(), index(index), compData(std::move(comp_data))
{
    setSizeBits(compData->getSizeBits());
}

uint8_t
Multi::MultiCompData::getIndex() const
{
    return index;
}

Multi::Multi(const Params &p)
    : Base(p),
      compressors(p.compressors),
      numEncodingBits(p.encoding_in_tags
                          ? 0
                          : std::log2(alignToPowerOfTwo(compressors.size()))),
      unpromisingThreshold(p.unpromising_threshold),
      probeInterval(p.probe_interval),
      consecutiveFailures(compressors.size(), 0),
      isUnpromising(compressors.size(), false),
      totalCompressions(0),
      multiStats(stats, *this)
{
    fatal_if(compressors.size() == 0, "There must be at least one compressor");
}

Multi::~Multi()
{
    for (auto& compressor : compressors) {
        delete compressor;
    }
}

void
Multi::setCache(BaseCache *_cache)
{
    Base::setCache(_cache);
    for (auto& compressor : compressors) {
        compressor->setCache(_cache);
    }
}

bool
Multi::isCompressorUnpromising(unsigned index) const
{
    assert(index < isUnpromising.size());
    return isUnpromising[index];
}

unsigned
Multi::getConsecutiveFailures(unsigned index) const
{
    assert(index < consecutiveFailures.size());
    return consecutiveFailures[index];
}

std::unique_ptr<Base::CompressionData>
Multi::compress(const std::vector<Chunk>& chunks, Cycles& comp_lat,
    Cycles& decomp_lat)
{
    struct Results
    {
        unsigned index;
        std::unique_ptr<Base::CompressionData> compData;
        Cycles decompLat;
        uint8_t compressionFactor;
        bool successful;

        Results(unsigned index,
                std::unique_ptr<Base::CompressionData> comp_data,
                Cycles decomp_lat, std::size_t blk_size,
                std::size_t threshold_bytes)
            : index(index),
              compData(std::move(comp_data)),
              decompLat(decomp_lat)
        {
            const std::size_t size = compData->getSize();
            // A compression attempt is successful if it fits within the
            // size threshold and is smaller than the uncompressed block size.
            successful = (size <= threshold_bytes) && (size < blk_size);

            // If the compressed size is worse than the uncompressed size,
            // we assume the size is the uncompressed size, and thus the
            // compression factor is 1.
            //
            // Some compressors (notably the zero compressor) may rely on
            // extra information being stored in the tags, or added in
            // another compression layer. Their size can be 0, so it is
            // assigned the highest possible compression factor (the original
            // block's size).
            compressionFactor = (size > blk_size) ? 1 :
                ((size == 0) ? blk_size :
                alignToPowerOfTwo(std::floor(blk_size / (double) size)));
        }
    };
    struct ResultsComparator
    {
        bool
        operator()(const std::shared_ptr<Results>& lhs,
            const std::shared_ptr<Results>& rhs) const
        {
            const std::size_t lhs_cf = lhs->compressionFactor;
            const std::size_t rhs_cf = rhs->compressionFactor;

            if (lhs_cf == rhs_cf) {
                // When they have similar compressed sizes, give the one
                // with fastest decompression privilege
                return lhs->decompLat > rhs->decompLat;
            }
            return lhs_cf < rhs_cf;
        }
    };

    // Each sub-compressor can have its own chunk size; therefore, revert
    // the chunks to raw data, so that they handle the conversion internally
    auto data = std::make_unique<uint64_t[]>(blkSize/8);
    fromChunks(chunks, data.get());

    totalCompressions++;

    const bool periodic_probe =
        (probeInterval > 0) && (totalCompressions % probeInterval == 0);

    bool all_unpromising = true;
    for (bool unp : isUnpromising) {
        if (!unp) {
            all_unpromising = false;
            break;
        }
    }

    std::vector<bool> evaluated(compressors.size(), false);
    std::priority_queue<std::shared_ptr<Results>,
        std::vector<std::shared_ptr<Results>>, ResultsComparator> results;
    Cycles max_comp_lat(0);

    auto run_compressor = [&](unsigned i) {
        Cycles temp_comp_lat(0);
        Cycles temp_decomp_lat(0);
        auto temp_comp_data = compressors[i]->compress(
            data.get(), temp_comp_lat, temp_decomp_lat);
        temp_comp_data->setSizeBits(temp_comp_data->getSizeBits() +
            numEncodingBits);
        auto res =
            std::make_shared<Results>(i, std::move(temp_comp_data),
                                      temp_decomp_lat, blkSize, sizeThreshold);
        results.push(res);
        max_comp_lat = std::max(max_comp_lat, temp_comp_lat);
        evaluated[i] = true;
        multiStats.totalAttempts[i]++;
        return res;
    };

    // Primary Evaluation Pass
    bool has_successful_primary = false;
    for (unsigned i = 0; i < compressors.size(); i++) {
        if (!isUnpromising[i] || periodic_probe || all_unpromising) {
            auto res = run_compressor(i);
            if (res->successful) {
                has_successful_primary = true;
            }
        } else {
            multiStats.skippedCompressions[i]++;
        }
    }

    // Fallback Pass: If no active candidate produced a successful compression
    // and there are skipped candidates, evaluate the skipped candidates.
    if (!has_successful_primary) {
        for (unsigned i = 0; i < compressors.size(); i++) {
            if (!evaluated[i]) {
                run_compressor(i);
            }
        }
    }

    // Set decompression latency of the best compressor
    if (results.top()->compData->getSizeBits() >= blkSize * CHAR_BIT) {
        decomp_lat = Cycles(0);
    } else {
        decomp_lat = results.top()->decompLat + decompExtraLatency;
    }

    // Collect evaluated results in rank order (best to worst)
    std::vector<std::shared_ptr<Results>> evaluated_results;
    while (!results.empty()) {
        evaluated_results.push_back(results.top());
        results.pop();
    }

    // Assign best compressor to compression data
    const unsigned best_index = evaluated_results.front()->index;
    auto multi_comp_data = std::make_unique<MultiCompData>(
        best_index, std::move(evaluated_results.front()->compData));
    DPRINTF(CacheComp, "Best compressor: %d\n", best_index);

    // Update compressor ranking stats
    for (int rank = 0; rank < evaluated_results.size(); rank++) {
        multiStats.ranks[evaluated_results[rank]->index][rank]++;
    }

    // Update effectiveness tracking
    for (const auto &res : evaluated_results) {
        const unsigned idx = res->index;
        if (res->successful) {
            consecutiveFailures[idx] = 0;
            isUnpromising[idx] = false;
            multiStats.successfulCompressions[idx]++;
        } else {
            consecutiveFailures[idx]++;
            if (unpromisingThreshold > 0 &&
                consecutiveFailures[idx] >= unpromisingThreshold) {
                isUnpromising[idx] = true;
            }
        }
    }

    // Set compression latency (compression latency of the slowest evaluated
    // compressor plus extra latency)
    comp_lat = Cycles(max_comp_lat + compExtraLatency);

    return multi_comp_data;
}

void
Multi::decompress(const CompressionData* comp_data,
    uint64_t* cache_line)
{
    const MultiCompData* casted_comp_data =
        static_cast<const MultiCompData*>(comp_data);
    compressors[casted_comp_data->getIndex()]->decompress(
        casted_comp_data->compData.get(), cache_line);
}

Multi::MultiStats::MultiStats(BaseStats &base_group, Multi &_compressor)
    : statistics::Group(&base_group),
      compressor(_compressor),
      ADD_STAT(ranks, statistics::units::Count::get(),
               "Number of times each compressor had the nth best compression"),
      ADD_STAT(totalAttempts, statistics::units::Count::get(),
               "Total evaluation attempts per sub-compressor"),
      ADD_STAT(successfulCompressions, statistics::units::Count::get(),
               "Successful compressions per sub-compressor"),
      ADD_STAT(skippedCompressions, statistics::units::Count::get(),
               "Skipped evaluation attempts per sub-compressor")
{
}

void
Multi::MultiStats::regStats()
{
    statistics::Group::regStats();

    const std::size_t num_compressors = compressor.compressors.size();
    ranks.init(num_compressors, num_compressors);
    totalAttempts.init(num_compressors);
    successfulCompressions.init(num_compressors);
    skippedCompressions.init(num_compressors);

    for (unsigned compressor = 0; compressor < num_compressors; compressor++) {
        ranks.subname(compressor, std::to_string(compressor));
        ranks.subdesc(compressor, "Number of times compressor " +
            std::to_string(compressor) + " had the nth best compression.");
        totalAttempts.subname(compressor, std::to_string(compressor));
        totalAttempts.subdesc(compressor,
                              "Total evaluation attempts for compressor " +
                                  std::to_string(compressor));
        successfulCompressions.subname(compressor, std::to_string(compressor));
        successfulCompressions.subdesc(
            compressor, "Successful compressions for compressor " +
                            std::to_string(compressor));
        skippedCompressions.subname(compressor, std::to_string(compressor));
        skippedCompressions.subdesc(compressor,
                                    "Skipped compressions for compressor " +
                                        std::to_string(compressor));
        for (unsigned rank = 0; rank < num_compressors; rank++) {
            ranks.ysubname(rank, std::to_string(rank));
        }
    }
}

} // namespace compression
} // namespace gem5
