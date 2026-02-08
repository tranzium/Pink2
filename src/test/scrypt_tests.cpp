// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "scrypt.h"
#include "uint256.h"
#include "util.h"

#include <string.h>

BOOST_AUTO_TEST_SUITE(scrypt_tests)

// ============================================================================
// scrypt_hash() determinism tests
// ============================================================================

BOOST_AUTO_TEST_CASE(scrypt_hash_deterministic)
{
    // The same input must always produce the same hash
    const char* input = "Pinkcoin scrypt test vector";
    size_t len = strlen(input);

    uint256 hash1 = scrypt_hash(input, len);
    uint256 hash2 = scrypt_hash(input, len);

    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash1 != 0);
}

BOOST_AUTO_TEST_CASE(scrypt_hash_different_inputs)
{
    // Different inputs must produce different hashes
    const char* input1 = "input one";
    const char* input2 = "input two";

    uint256 hash1 = scrypt_hash(input1, strlen(input1));
    uint256 hash2 = scrypt_hash(input2, strlen(input2));

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(scrypt_hash_empty_vs_nonempty)
{
    const char* empty = "";
    const char* nonempty = "x";

    uint256 hashEmpty = scrypt_hash(empty, 0);
    uint256 hashNonEmpty = scrypt_hash(nonempty, 1);

    BOOST_CHECK(hashEmpty != hashNonEmpty);
    // Even empty input should produce a valid nonzero hash
    BOOST_CHECK(hashEmpty != 0);
}

BOOST_AUTO_TEST_CASE(scrypt_hash_single_bit_difference)
{
    // Changing a single byte should produce a completely different hash
    char input1[32];
    char input2[32];
    memset(input1, 0, 32);
    memset(input2, 0, 32);
    input2[0] = 1;  // flip one bit

    uint256 hash1 = scrypt_hash(input1, 32);
    uint256 hash2 = scrypt_hash(input2, 32);

    BOOST_CHECK(hash1 != hash2);
}

// ============================================================================
// scrypt_blockhash() tests — this is used for PoW block hashing
// ============================================================================

BOOST_AUTO_TEST_CASE(scrypt_blockhash_deterministic)
{
    // scrypt_blockhash takes exactly 80 bytes (block header size)
    unsigned char header[80];
    memset(header, 0, 80);

    uint256 hash1 = scrypt_blockhash(header);
    uint256 hash2 = scrypt_blockhash(header);

    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash1 != 0);
}

BOOST_AUTO_TEST_CASE(scrypt_blockhash_different_headers)
{
    unsigned char header1[80];
    unsigned char header2[80];
    memset(header1, 0, 80);
    memset(header2, 0, 80);
    header2[0] = 1;

    uint256 hash1 = scrypt_blockhash(header1);
    uint256 hash2 = scrypt_blockhash(header2);

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(scrypt_blockhash_known_zero_header)
{
    // Regression test: record the hash of an all-zero 80-byte header
    // This value must remain stable across builds and platforms
    unsigned char header[80];
    memset(header, 0, 80);

    uint256 hash = scrypt_blockhash(header);

    // Known-good regression value — if this ever changes, PoW consensus is broken
    uint256 expected("0x694b3a55a61339b43c01421b13f710e22e33a7eabda1cd48103bb9f376081d16");
    BOOST_CHECK(hash == expected);

    // Re-hash to confirm stability within same run
    uint256 hash2 = scrypt_blockhash(header);
    BOOST_CHECK(hash == hash2);
}

BOOST_AUTO_TEST_CASE(scrypt_blockhash_nonce_sensitivity)
{
    // In a real block header, bytes 76-79 are the nonce.
    // Changing only the nonce should produce a different hash.
    unsigned char header[80];
    memset(header, 0x42, 80);

    uint256 hash_nonce0 = scrypt_blockhash(header);

    header[76] = 0x01;  // change nonce
    uint256 hash_nonce1 = scrypt_blockhash(header);

    BOOST_CHECK(hash_nonce0 != hash_nonce1);
}

// ============================================================================
// scrypt_salted_hash() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(salted_hash_deterministic)
{
    const char* input = "test data";
    const char* salt = "test salt";

    uint256 hash1 = scrypt_salted_hash(input, strlen(input), salt, strlen(salt));
    uint256 hash2 = scrypt_salted_hash(input, strlen(input), salt, strlen(salt));

    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash1 != 0);
}

BOOST_AUTO_TEST_CASE(salted_hash_different_salts)
{
    const char* input = "same data";
    const char* salt1 = "salt one";
    const char* salt2 = "salt two";

    uint256 hash1 = scrypt_salted_hash(input, strlen(input), salt1, strlen(salt1));
    uint256 hash2 = scrypt_salted_hash(input, strlen(input), salt2, strlen(salt2));

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_CASE(salted_hash_vs_unsalted)
{
    // scrypt_hash uses input as both data and salt
    // scrypt_salted_hash uses separate salt
    // When salt == input, they should produce the same result
    const char* input = "identical input and salt";
    size_t len = strlen(input);

    uint256 hashUnsalted = scrypt_hash(input, len);
    uint256 hashSalted = scrypt_salted_hash(input, len, input, len);

    BOOST_CHECK(hashUnsalted == hashSalted);
}

// ============================================================================
// scrypt_salted_multiround_hash() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(multiround_single_round_equals_salted)
{
    // With nRounds=1, multiround should equal single salted hash
    const char* input = "round test";
    const char* salt = "round salt";

    uint256 hashSingle = scrypt_salted_hash(input, strlen(input), salt, strlen(salt));
    uint256 hashMulti = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 1);

    BOOST_CHECK(hashSingle == hashMulti);
}

BOOST_AUTO_TEST_CASE(multiround_more_rounds_different)
{
    // More rounds should produce a different hash
    const char* input = "multi test";
    const char* salt = "multi salt";

    uint256 hash1 = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 1);
    uint256 hash2 = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 2);
    uint256 hash3 = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 3);

    BOOST_CHECK(hash1 != hash2);
    BOOST_CHECK(hash2 != hash3);
    BOOST_CHECK(hash1 != hash3);
}

BOOST_AUTO_TEST_CASE(multiround_deterministic)
{
    const char* input = "deterministic";
    const char* salt = "salt";

    uint256 hash1 = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 5);
    uint256 hash2 = scrypt_salted_multiround_hash(input, strlen(input), salt, strlen(salt), 5);

    BOOST_CHECK(hash1 == hash2);
}

// ============================================================================
// Block header hash integration test
// ============================================================================

BOOST_AUTO_TEST_CASE(block_getpowhash_uses_scrypt)
{
    // CBlock::GetPoWHash() should use scrypt_blockhash on the header
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = 0;
    block.nTime = 0;
    block.nBits = 0;
    block.nNonce = 0;

    uint256 powHash = block.GetPoWHash();

    // Manually compute scrypt_blockhash on the same data
    uint256 manualHash = scrypt_blockhash(CVOIDBEGIN(block.nVersion));

    BOOST_CHECK(powHash == manualHash);
}

BOOST_AUTO_TEST_CASE(block_hash_changes_with_nonce)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = 0;
    block.nTime = 1700000000;
    block.nBits = 0x1e0fffff;

    block.nNonce = 0;
    uint256 hash0 = block.GetPoWHash();

    block.nNonce = 1;
    uint256 hash1 = block.GetPoWHash();

    block.nNonce = 2;
    uint256 hash2 = block.GetPoWHash();

    BOOST_CHECK(hash0 != hash1);
    BOOST_CHECK(hash1 != hash2);
    BOOST_CHECK(hash0 != hash2);
}

BOOST_AUTO_TEST_CASE(block_hash_changes_with_time)
{
    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = 0;
    block.hashMerkleRoot = 0;
    block.nBits = 0x1e0fffff;
    block.nNonce = 12345;

    block.nTime = 1700000000;
    uint256 hash1 = block.GetPoWHash();

    block.nTime = 1700000001;
    uint256 hash2 = block.GetPoWHash();

    BOOST_CHECK(hash1 != hash2);
}

BOOST_AUTO_TEST_SUITE_END()
