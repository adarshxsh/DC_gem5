/*
 * Copyright (c) 2026
 * All rights reserved.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "mem/cache/compressors/cpack.hh"
#include "mem/cache/compressors/dictionary_compressor_impl.hh"
#include "mem/cache/compressors/fpc.hh"
#include "params/CPack.hh"
#include "params/FPC.hh"
#include "sim/root.hh"

namespace gem5
{
Root *Root::_root = nullptr;
}

using namespace gem5;
using namespace compression;

class TestCPack : public CPack
{
  public:
    using Base::compress;
    using CPack::CPack;
    using CPack::decompress;
};

class TestFPC : public FPC
{
  public:
    using Base::compress;
    using FPC::decompress;
    using FPC::FPC;
};

class TestDictCompressor64 : public DictionaryCompressor<uint64_t>
{
  public:
    using DictionaryCompressor<uint64_t>::toDictionaryEntry;
    template <std::size_t DeltaSizeBits>
    using DeltaPattern = DictionaryCompressor<uint64_t>::DeltaPattern<DeltaSizeBits>;
};

TEST(DictionaryCompressorTest, ZeroBlockDecompressionShortcutCPack)
{
    CPackParams p{};
    p.name = "cpack";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 4;
    p.comp_chunks_per_cycle = 2;
    p.comp_extra_latency = Cycles(5);
    p.decomp_chunks_per_cycle = 2;
    p.decomp_extra_latency = Cycles(1);
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    TestCPack compressor(p);
    compressor.regStats();

    // 1. All-zero block
    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);
    auto comp_data = compressor.compress(zero_data, comp_lat, decomp_lat);

    // Decompression latency for all-zero block must be exactly 1 cycle
    EXPECT_EQ(decomp_lat, Cycles(1));

    // Verify decompression correctness
    uint64_t decomp_data[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    compressor.decompress(comp_data.get(), decomp_data);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(decomp_data[i], 0ULL);
    }

    // 2. Non-zero block
    uint64_t non_zero_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                                 0x2122232425262728ULL, 0x2930313233343536ULL,
                                 0x3738394041424344ULL, 0x4546474849505152ULL,
                                 0x5354555657585960ULL, 0x6162636465666768ULL};
    comp_data = compressor.compress(non_zero_data, comp_lat, decomp_lat);

    // Decompression latency for non-zero block retains standard calculated
    // latency (9 cycles for CPack)
    EXPECT_EQ(decomp_lat, Cycles(9));

    compressor.decompress(comp_data.get(), decomp_data);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(decomp_data[i], non_zero_data[i]);
    }

    // 3. Partial zero block (mix of zero and non-zero chunks)
    uint64_t partial_zero_data[8] = {0, 0, 0, 0,
                                     0, 0, 0, 0x1234567891011121ULL};
    comp_data = compressor.compress(partial_zero_data, comp_lat, decomp_lat);

    // Partial zero block must NOT receive the 1-cycle shortcut
    EXPECT_EQ(decomp_lat, Cycles(9));

    compressor.decompress(comp_data.get(), decomp_data);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(decomp_data[i], partial_zero_data[i]);
    }
}

TEST(DictionaryCompressorTest, ZeroBlockDecompressionShortcutFPC)
{
    FPCParams p{};
    p.name = "fpc";
    p.block_size = 64;
    p.chunk_size_bits = 32;
    p.dictionary_size = 0;
    p.comp_chunks_per_cycle = 8;
    p.comp_extra_latency = Cycles(1);
    p.decomp_chunks_per_cycle = 4;
    p.decomp_extra_latency = Cycles(1);
    p.zero_run_bits = 3;
    p.size_threshold_percentage = 100;
    p.enable_adaptive_bypass = false;
    p.latency_breakeven_threshold = 1.0;
    p.sampling_interval = 100;
    p.decay_shift = 4;

    TestFPC compressor(p);
    compressor.regStats();

    // 1. All-zero block
    uint64_t zero_data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    Cycles comp_lat(0), decomp_lat(0);
    auto comp_data = compressor.compress(zero_data, comp_lat, decomp_lat);

    // Decompression latency for all-zero block must be exactly 1 cycle
    EXPECT_EQ(decomp_lat, Cycles(1));

    uint64_t decomp_data[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    compressor.decompress(comp_data.get(), decomp_data);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(decomp_data[i], 0ULL);
    }

    // 2. Non-zero block
    uint64_t non_zero_data[8] = {0x1234567891011121ULL, 0x1314151617181920ULL,
                                 0x2122232425262728ULL, 0x2930313233343536ULL,
                                 0x3738394041424344ULL, 0x4546474849505152ULL,
                                 0x5354555657585960ULL, 0x6162636465666768ULL};
    comp_data = compressor.compress(non_zero_data, comp_lat, decomp_lat);

    // Standard decompression latency for FPC: 1 + (16 / 4) = 5 cycles
    EXPECT_EQ(decomp_lat, Cycles(5));

    compressor.decompress(comp_data.get(), decomp_data);
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(decomp_data[i], non_zero_data[i]);
    }
}

TEST(DictionaryCompressorTest, DeltaPatternAsymmetricNegativeBound)
{
    // Test that DeltaPattern::isValidDelta accepts full two's complement minimum
    // negative offset (-2^(N-1)), e.g. -128 for 8-bit delta.
    using Delta8Pattern = TestDictCompressor64::DeltaPattern<8>;

    uint64_t base_val = 0x1000;
    uint64_t min_neg_val = 0x1000 - 128; // delta = -128
    uint64_t max_pos_val = 0x1000 + 127; // delta = +127
    uint64_t out_of_bounds_neg = 0x1000 - 129; // delta = -129
    uint64_t out_of_bounds_pos = 0x1000 + 128; // delta = +128

    auto base_bytes = TestDictCompressor64::toDictionaryEntry(base_val);
    auto min_neg_bytes = TestDictCompressor64::toDictionaryEntry(min_neg_val);
    auto max_pos_bytes = TestDictCompressor64::toDictionaryEntry(max_pos_val);
    auto out_neg_bytes = TestDictCompressor64::toDictionaryEntry(out_of_bounds_neg);
    auto out_pos_bytes = TestDictCompressor64::toDictionaryEntry(out_of_bounds_pos);

    // Delta -128 must be valid under two's complement
    EXPECT_TRUE(Delta8Pattern::isValidDelta(min_neg_bytes, base_bytes));
    // Delta +127 must be valid
    EXPECT_TRUE(Delta8Pattern::isValidDelta(max_pos_bytes, base_bytes));
    // Delta -129 and +128 must be invalid
    EXPECT_FALSE(Delta8Pattern::isValidDelta(out_neg_bytes, base_bytes));
    EXPECT_FALSE(Delta8Pattern::isValidDelta(out_pos_bytes, base_bytes));
}

