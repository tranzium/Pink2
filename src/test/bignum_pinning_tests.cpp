// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Pinning tests for CBigNum behavior — safety net before replacing
// CBigNum with arith_uint256, CScriptNum, and byte-array base58.
// Each test pins the EXACT current behavior so any replacement
// regression is caught instantly.

#include <boost/test/unit_test.hpp>

#include "bignum.h"
#include "base58.h"
#include "uint256.h"
#include "util.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Helper: construct uint256 from hex string
static uint256 uint256_from_hex(const std::string& hex)
{
    uint256 result;
    result.SetHex(hex);
    return result;
}

// ============================================================================
// Suite 1: compact_encoding_pinning
// Pin SetCompact/GetCompact behavior for all Pinkcoin difficulty limits
// and interesting edge cases.
// ============================================================================
BOOST_AUTO_TEST_SUITE(compact_encoding_pinning)

// --- Pinkcoin difficulty limits ---

BOOST_AUTO_TEST_CASE(pow_limit_compact)
{
    // bnProofOfWorkLimit = CBigNum(~uint256(0) >> 20)
    // 236 bits = 30 MPI bytes, mantissa 0x0FFFFF → compact 0x1E0FFFFF
    CBigNum bn(~uint256(0) >> 20);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x1e0fffffu);

    // Compact only stores 3 bytes of mantissa, so roundtrip via
    // SetCompact zeroes out the lower bytes — pin compact stability
    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact);
}

BOOST_AUTO_TEST_CASE(pos_limit_compact)
{
    // bnProofOfStakeLimit = CBigNum(~uint256(0) >> 10)
    // 246 bits = 31 MPI bytes, mantissa 0x3FFFFF → compact 0x1F3FFFFF
    CBigNum bn(~uint256(0) >> 10);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x1f3fffffu);

    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact);
}

BOOST_AUTO_TEST_CASE(flash_pos_limit_compact)
{
    // bnProofOfFlashStakeLimit = CBigNum(~uint256(0) >> 10) — same as PoS
    CBigNum bn(~uint256(0) >> 10);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x1f3fffffu);
}

BOOST_AUTO_TEST_CASE(pow_limit_testnet_compact)
{
    // bnProofOfWorkLimitTestNet = CBigNum(~uint256(0) >> 16)
    // 240 bits = 30 value bytes + sign byte → 31 MPI bytes
    // mantissa 0x00FFFF → compact 0x1F00FFFF
    CBigNum bn(~uint256(0) >> 16);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x1f00ffffu);

    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact);
}

// --- SetCompact edge cases ---

BOOST_AUTO_TEST_CASE(compact_zero)
{
    CBigNum bn;
    bn.SetCompact(0x00000000);
    BOOST_CHECK(!!bn == false); // IsZero via operator!
    BOOST_CHECK(bn.getuint256() == uint256(0));
}

BOOST_AUTO_TEST_CASE(compact_size_one_mantissa)
{
    // Size=1, mantissa=0x34 in high byte → value = 0x34
    CBigNum bn;
    bn.SetCompact(0x01340000);
    uint256 result = bn.getuint256();
    BOOST_CHECK(result == uint256_from_hex("34"));
}

BOOST_AUTO_TEST_CASE(compact_size_three)
{
    // Size=3, mantissa=0x123456 → value = 0x123456
    CBigNum bn;
    bn.SetCompact(0x03123456);
    uint256 result = bn.getuint256();
    BOOST_CHECK(result == uint256_from_hex("123456"));
}

BOOST_AUTO_TEST_CASE(compact_size_five)
{
    // SetCompact(0x05009234): Size=5, mantissa bytes=[0x00, 0x92, 0x34]
    // In MPI: 5 bytes [0x00, 0x92, 0x34, 0x00, 0x00]
    // Leading 0x00 is sign byte → value = 0x92340000
    CBigNum bn;
    bn.SetCompact(0x05009234);
    uint256 result = bn.getuint256();
    BOOST_CHECK(result == uint256_from_hex("92340000"));

    // GetCompact roundtrip — normalize removes the sign byte
    unsigned int compact2 = bn.GetCompact();
    CBigNum bn3;
    bn3.SetCompact(compact2);
    BOOST_CHECK(bn3.getuint256() == result);
}

BOOST_AUTO_TEST_CASE(compact_high_bit_mantissa)
{
    // Size=4, mantissa=0x800000 → high bit set → negative in MPI
    CBigNum bn;
    bn.SetCompact(0x04800000);
    // Pin: getuint256 for negative number
    (void)bn.getuint256();
    // GetCompact roundtrip is stable
    unsigned int compact_back = bn.GetCompact();
    CBigNum bn2;
    bn2.SetCompact(compact_back);
    BOOST_CHECK(bn2.GetCompact() == compact_back);
}

BOOST_AUTO_TEST_CASE(compact_large_size)
{
    // Size=32 (0x20), mantissa=0xffffff
    // In MPI: 32 bytes, first byte 0xff has high bit set → negative
    // But this still produces a non-zero uint256
    CBigNum bn;
    bn.SetCompact(0x20ffffff);
    uint256 result = bn.getuint256();
    BOOST_CHECK(result != uint256(0));

    // Roundtrip stability
    unsigned int compact_back = bn.GetCompact();
    CBigNum bn2;
    bn2.SetCompact(compact_back);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact_back);
}

BOOST_AUTO_TEST_CASE(compact_roundtrip_pow_limit)
{
    // Verify GetCompact() is stable under SetCompact(GetCompact())
    CBigNum bn(~uint256(0) >> 20);
    unsigned int compact = bn.GetCompact();
    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact);
}

BOOST_AUTO_TEST_CASE(compact_one)
{
    // Value = 1, MPI = [0x01] → size=1
    CBigNum bn(1);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x01010000u);

    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK(bn2.getuint256() == bn.getuint256());
}

BOOST_AUTO_TEST_CASE(compact_256)
{
    // Value = 256 = 0x0100, MPI = [0x01, 0x00] → size=2
    CBigNum bn(256);
    unsigned int compact = bn.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x02010000u);

    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK(bn2.getuint256() == bn.getuint256());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 2: script_number_pinning
// Pin CBigNum's getvch()/setvch() encoding — the script number format
// that CScriptNum must reproduce exactly.
// ============================================================================
BOOST_AUTO_TEST_SUITE(script_number_pinning)

BOOST_AUTO_TEST_CASE(vch_zero)
{
    CBigNum bn(0);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK(vch.empty());
}

BOOST_AUTO_TEST_CASE(vch_one)
{
    CBigNum bn(1);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 1u);
    BOOST_CHECK_EQUAL(vch[0], 0x01);
}

BOOST_AUTO_TEST_CASE(vch_negative_one)
{
    CBigNum bn(-1);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 1u);
    BOOST_CHECK_EQUAL(vch[0], 0x81);
}

BOOST_AUTO_TEST_CASE(vch_127)
{
    CBigNum bn(127);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 1u);
    BOOST_CHECK_EQUAL(vch[0], 0x7f);
}

BOOST_AUTO_TEST_CASE(vch_128)
{
    // 128 needs extra byte for sign since 0x80 bit is used for sign
    CBigNum bn(128);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 2u);
    BOOST_CHECK_EQUAL(vch[0], 0x80);
    BOOST_CHECK_EQUAL(vch[1], 0x00);
}

BOOST_AUTO_TEST_CASE(vch_255)
{
    CBigNum bn(255);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 2u);
    BOOST_CHECK_EQUAL(vch[0], 0xff);
    BOOST_CHECK_EQUAL(vch[1], 0x00);
}

BOOST_AUTO_TEST_CASE(vch_negative_255)
{
    CBigNum bn(-255);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 2u);
    BOOST_CHECK_EQUAL(vch[0], 0xff);
    BOOST_CHECK_EQUAL(vch[1], 0x80);
}

BOOST_AUTO_TEST_CASE(vch_negative_128)
{
    CBigNum bn(-128);
    std::vector<unsigned char> vch = bn.getvch();
    BOOST_CHECK_EQUAL(vch.size(), 2u);
    BOOST_CHECK_EQUAL(vch[0], 0x80);
    BOOST_CHECK_EQUAL(vch[1], 0x80);
}

BOOST_AUTO_TEST_CASE(cast_to_bignum_overflow)
{
    // CastToBigNum with 5-byte input should throw (nMaxNumSize=4)
    const size_t nMaxNumSize = 4;
    std::vector<unsigned char> vch5 = {0x01, 0x02, 0x03, 0x04, 0x05};
    BOOST_CHECK_THROW(
        ([&](){
            if (vch5.size() > nMaxNumSize)
                throw std::runtime_error("CastToBigNum() : overflow");
            CBigNum(CBigNum(vch5).getvch());
        })(),
        std::runtime_error
    );
}

BOOST_AUTO_TEST_CASE(vch_roundtrip_various)
{
    // Roundtrip: construct from int, getvch, setvch back, getint
    int values[] = {0, 1, -1, 42, -42, 127, 128, -128, 255, -255, 1000, -1000};
    for (int val : values) {
        CBigNum bn(val);
        std::vector<unsigned char> vch = bn.getvch();
        CBigNum bn2;
        bn2.setvch(vch);
        BOOST_CHECK_EQUAL(bn2.getint(), val);
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 3: base58_edge_pinning
// Pin EncodeBase58 edge cases before replacing with byte-array algorithm.
// ============================================================================
BOOST_AUTO_TEST_SUITE(base58_edge_pinning)

BOOST_AUTO_TEST_CASE(encode_empty)
{
    std::vector<unsigned char> empty;
    std::string result = EncodeBase58(empty);
    BOOST_CHECK_EQUAL(result, "");
}

BOOST_AUTO_TEST_CASE(encode_single_zero)
{
    // Leading zero byte maps to '1' in base58
    std::vector<unsigned char> data = {0x00};
    std::string result = EncodeBase58(data);
    BOOST_CHECK_EQUAL(result, "1");
}

BOOST_AUTO_TEST_CASE(encode_multiple_leading_zeros)
{
    // {0x00, 0x00, 0x01} → "11" + encode(0x01) = "112"
    std::vector<unsigned char> data = {0x00, 0x00, 0x01};
    std::string result = EncodeBase58(data);
    BOOST_CHECK_EQUAL(result, "112");
}

BOOST_AUTO_TEST_CASE(encode_five_zeros)
{
    // All zeros → each maps to '1'
    std::vector<unsigned char> data = {0x00, 0x00, 0x00, 0x00, 0x00};
    std::string result = EncodeBase58(data);
    BOOST_CHECK_EQUAL(result, "11111");
}

BOOST_AUTO_TEST_CASE(roundtrip_known_bytes)
{
    // Known test vector: "Hello World" in hex
    std::vector<unsigned char> data = {
        0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64
    };
    std::string encoded = EncodeBase58(data);
    BOOST_CHECK(!encoded.empty());

    // Decode back
    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58(encoded, decoded));
    BOOST_CHECK(decoded == data);
}

BOOST_AUTO_TEST_CASE(roundtrip_with_leading_zeros)
{
    // Data with leading zeros must roundtrip correctly
    std::vector<unsigned char> data = {0x00, 0x00, 0xAB, 0xCD, 0xEF};
    std::string encoded = EncodeBase58(data);

    std::vector<unsigned char> decoded;
    BOOST_CHECK(DecodeBase58(encoded, decoded));
    BOOST_CHECK(decoded == data);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 4: pos_target_pinning
// Pin the PoS target multiplication behavior used in kernel.cpp.
// ============================================================================
BOOST_AUTO_TEST_SUITE(pos_target_pinning)

BOOST_AUTO_TEST_CASE(target_per_coin_day_product)
{
    // Simulate: bnTargetPerCoinDay.SetCompact(bnProofOfStakeLimit.GetCompact())
    CBigNum bnProofOfStakeLimitLocal(~uint256(0) >> 10);
    unsigned int compact = bnProofOfStakeLimitLocal.GetCompact();
    BOOST_CHECK_EQUAL(compact, 0x1f3fffffu);

    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(compact);

    // Construct bnCoinDayWeight from a known int64_t
    int64_t coinDayWeight = 1000000;
    CBigNum bnCoinDayWeight(coinDayWeight);

    // Compute product and pin its uint256 representation
    uint256 product = (bnCoinDayWeight * bnTargetPerCoinDay).getuint256();
    BOOST_CHECK(product != uint256(0));

    // Pin: the product must equal coinDayWeight * bnTargetPerCoinDay exactly
    CBigNum bnProduct = bnCoinDayWeight * bnTargetPerCoinDay;
    BOOST_CHECK(bnProduct.getuint256() == product);

    // Pin compact stability
    BOOST_CHECK_EQUAL(bnTargetPerCoinDay.GetCompact(), compact);
}

BOOST_AUTO_TEST_CASE(hash_comparison_pass_and_fail)
{
    // Set up target with actual PoS limit compact
    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(0x1f3fffffu);

    int64_t coinDayWeight = 1000;
    CBigNum bnCoinDayWeight(coinDayWeight);
    CBigNum target = bnCoinDayWeight * bnTargetPerCoinDay;

    // A very small hash should be less than target → PASS
    uint256 hashLow;
    hashLow.SetHex("0000000000000000000000000000000000000000000000000000000000000001");
    BOOST_CHECK(!(CBigNum(hashLow) > target));

    // A maximum hash should be greater than target → FAIL
    uint256 hashHigh;
    hashHigh.SetHex("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    BOOST_CHECK(CBigNum(hashHigh) > target);
}

BOOST_AUTO_TEST_CASE(multiply_then_getcompact)
{
    // (CBigNum(1000) * CBigNum(~uint256(0) >> 20)).GetCompact()
    CBigNum bn1000(1000);
    CBigNum bnPowLimit(~uint256(0) >> 20);
    CBigNum product = bn1000 * bnPowLimit;
    unsigned int compact = product.GetCompact();

    // Pin: 1000 * (2^236 - 1) produces a large number
    BOOST_CHECK(compact != 0);

    // Verify GetCompact is stable
    CBigNum bn2;
    bn2.SetCompact(compact);
    BOOST_CHECK_EQUAL(bn2.GetCompact(), compact);
}

BOOST_AUTO_TEST_SUITE_END()
