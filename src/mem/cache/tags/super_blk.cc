/**
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

/** @file
 * Implementation of a simple superblock class. Each superblock consists of a
 * number of compressed cache blocks limited by the maximum compression factor
 * that may or may not be present in the cache.
 */

#include "mem/cache/tags/super_blk.hh"

#include <climits>
#include <cmath>

#include "base/bitfield.hh"

namespace gem5
{

CompressionBlk::CompressionBlk()
    : SectorSubBlk(), _size(0), _decompressionLatency(0), _compressed(false)
{
}

CacheBlk&
CompressionBlk::operator=(CacheBlk&& other)
{
    operator=(std::move(static_cast<CompressionBlk&&>(other)));
    return *this;
}

CompressionBlk&
CompressionBlk::operator=(CompressionBlk&& other)
{
    _size = other._size;
    setDecompressionLatency(other.getDecompressionLatency());
    if (other.isCompressed()) {
        setCompressed();
    } else {
        setUncompressed();
    }

    SuperBlk *src_super = static_cast<SuperBlk *>(other.getSectorBlock());

    SectorSubBlk::operator=(std::move(other));

    SuperBlk *dest_super = static_cast<SuperBlk *>(getSectorBlock());
    if (src_super) {
        src_super->updateCompressionFactor();
    }
    if (dest_super && dest_super != src_super) {
        dest_super->updateCompressionFactor();
    }

    return *this;
}

bool
CompressionBlk::isCompressed() const
{
    return _compressed;
}

void
CompressionBlk::setCompressed()
{
    _compressed = true;
}

void
CompressionBlk::setUncompressed()
{
    _compressed = false;
}

std::size_t
CompressionBlk::getSizeBits() const
{
    return _size;
}

void
CompressionBlk::setSizeBits(const std::size_t size)
{
    _size = size;

    SuperBlk* superblock = static_cast<SuperBlk*>(getSectorBlock());
    if (superblock) {
        superblock->updateCompressionFactor();

        const double compression_factor =
            superblock->calculateCompressionFactor(size);

        // Set compression status of this sub-block based on whether it is
        // compressed
        if (compression_factor > 1.0) {
            setCompressed();
        } else {
            setUncompressed();
        }
    } else {
        if (size > 0 && size < 512) {
            setCompressed();
        } else {
            setUncompressed();
        }
    }
}

Cycles
CompressionBlk::getDecompressionLatency() const
{
    return _decompressionLatency;
}

void
CompressionBlk::setDecompressionLatency(const Cycles lat)
{
    _decompressionLatency = lat;
}

void
CompressionBlk::invalidate()
{
    SectorSubBlk::invalidate();
    setUncompressed();
    _size = 0;
    SuperBlk *superblock = static_cast<SuperBlk *>(getSectorBlock());
    if (superblock) {
        superblock->updateCompressionFactor();
    }
}

CompressionBlk::OverwriteType
CompressionBlk::checkExpansionContraction(const std::size_t size) const
{
    // An expansion happens when a block passes from a compressible state
    // to a less compressible state (e.g., blkSize/4 to (blkSize/2 or blkSize),
    // or blkSize/2 to blkSize). A contraction happens when a block passes
    // from a less compressible state to a more compressible state (i.e., the
    // opposite of expansion)
    const SuperBlk* superblock =
        static_cast<const SuperBlk*>(getSectorBlock());
    const double prev_cf = superblock->getCompressionFactor();
    const double new_cf = superblock->calculateCompressionFactor(size);
    return (new_cf < prev_cf) ? DATA_EXPANSION :
        ((new_cf > prev_cf) ? DATA_CONTRACTION : UNCHANGED);
}

std::string
CompressionBlk::print() const
{
    return csprintf("%s compressed: %d size: %llu decompression latency: %d",
                    SectorSubBlk::print(), isCompressed(), getSizeBits(),
                    getDecompressionLatency());
}

SuperBlk::SuperBlk() : SectorBlk(), blkSize(0), compressionFactor(1.0)
{
}

void
SuperBlk::invalidate()
{
    SectorBlk::invalidate();
    compressionFactor = 1.0;
}

bool
SuperBlk::isCompressed(const CompressionBlk* ignored_blk) const
{
    for (const auto& blk : blks) {
        if (blk->isValid() && (blk != ignored_blk)) {
            if (!static_cast<CompressionBlk *>(blk)->isCompressed()) {
                return false;
            }
        }
    }

    // An invalid block is seen as compressed
    return true;
}

bool
SuperBlk::canCoAllocate(const std::size_t compressed_size) const
{
    if (!isCompressed()) {
        return false;
    }

    const double new_blk_cf = calculateCompressionFactor(compressed_size);
    if (new_blk_cf <= 1.0) {
        return false;
    }

    const double target_cf =
        (getNumValid() == 0) ? new_blk_cf
                             : std::min(getCompressionFactor(), new_blk_cf);

    return (target_cf > 1.0) &&
           (static_cast<double>(getNumValid() + 1) <= target_cf) &&
           (static_cast<double>(compressed_size) <=
            static_cast<double>(blkSize * CHAR_BIT) / target_cf + 1e-9);
}

void
SuperBlk::setBlkSize(const std::size_t blk_size)
{
    assert(blkSize == 0);
    blkSize = blk_size;
}

double
SuperBlk::calculateCompressionFactor(const std::size_t size) const
{
    // The number of blocks per sector determines the maximum comp factor.
    // If the compressed size is worse than the uncompressed size, we assume
    // the size is the uncompressed size, and thus the compression factor is 1
    const std::size_t blk_size_bits = CHAR_BIT * blkSize;
    if (size > blk_size_bits) {
        return 1.0;
    }
    if (size == 0) {
        return static_cast<double>(blks.size());
    }
    const double raw_cf =
        static_cast<double>(blk_size_bits) / static_cast<double>(size);
    return std::min<double>(raw_cf, static_cast<double>(blks.size()));
}

double
SuperBlk::getCompressionFactor() const
{
    return compressionFactor;
}

void
SuperBlk::setCompressionFactor(const double compression_factor)
{
    compressionFactor = compression_factor;
}

void
SuperBlk::updateCompressionFactor()
{
    double min_cf = static_cast<double>(blks.size());
    bool has_valid = false;
    for (const auto &blk : blks) {
        if (blk->isValid()) {
            has_valid = true;
            CompressionBlk *cblk = static_cast<CompressionBlk *>(blk);
            double cf = calculateCompressionFactor(cblk->getSizeBits());
            if (cf < min_cf) {
                min_cf = cf;
            }
        }
    }
    setCompressionFactor(has_valid ? min_cf : 1.0);
}

std::string
SuperBlk::print() const
{
    return csprintf("CF: %g %s", getCompressionFactor(), SectorBlk::print());
}

} // namespace gem5
