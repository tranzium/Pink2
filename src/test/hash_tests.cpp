// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "util.h"
#include "uint256.h"

#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(hash_tests)

// ============================================================================
// Hash() pinned regression vectors — SHA256d (double SHA-256)
// These MUST remain stable across any OpenSSL migration.
// If any of these change, consensus is broken.
// ============================================================================

BOOST_AUTO_TEST_CASE(hash_empty_input)
{
    // Hash of empty input — SHA256d("")
    // Pin the actual value to detect any behavioral change in the Hash() function
    std::vector<unsigned char> empty;
    uint256 hash = Hash(empty.begin(), empty.end());

    // Pinned regression value — if this changes, SHA256d implementation is broken
    uint256 expected("0x56944c5d3f98413ef45cf54545538103cc9f298e0575820ad3591376e2e0f65d");
    BOOST_CHECK(hash == expected);
}

BOOST_AUTO_TEST_CASE(hash_single_byte)
{
    // Hash of single byte 0x00
    unsigned char byte = 0x00;
    uint256 hash = Hash(&byte, &byte + 1);

    uint256 expected("0x9a538906e6466ebd2617d321f71bc94e56056ce213d366773699e28158e00614");
    BOOST_CHECK(hash == expected);
}

BOOST_AUTO_TEST_CASE(hash_known_string)
{
    // Hash("Pinkcoin") — deterministic regression value
    std::string input = "Pinkcoin";
    uint256 hash = Hash(input.begin(), input.end());

    uint256 expected("0x8db35df9702021642086614386acd99092dd5d8b5835eb280e200c90aeb4b22f");
    BOOST_CHECK(hash == expected);
}

BOOST_AUTO_TEST_CASE(hash_different_inputs_diverge)
{
    std::string a = "input_a";
    std::string b = "input_b";

    uint256 hashA = Hash(a.begin(), a.end());
    uint256 hashB = Hash(b.begin(), b.end());

    BOOST_CHECK(hashA != hashB);
}

BOOST_AUTO_TEST_CASE(hash_two_part)
{
    // Hash(part1, part2) should equal Hash(concat(part1, part2))
    std::string p1 = "hello";
    std::string p2 = "world";
    std::string combined = "helloworld";

    uint256 hashTwo = Hash(p1.begin(), p1.end(), p2.begin(), p2.end());
    uint256 hashOne = Hash(combined.begin(), combined.end());

    BOOST_CHECK(hashTwo == hashOne);
}

BOOST_AUTO_TEST_CASE(hash_three_part)
{
    std::string p1 = "aa";
    std::string p2 = "bb";
    std::string p3 = "cc";
    std::string combined = "aabbcc";

    uint256 hashThree = Hash(p1.begin(), p1.end(), p2.begin(), p2.end(), p3.begin(), p3.end());
    uint256 hashOne = Hash(combined.begin(), combined.end());

    BOOST_CHECK(hashThree == hashOne);
}

// ============================================================================
// Hash160() pinned regression vectors — SHA256 + RIPEMD160
// This is the address hash function.
// ============================================================================

BOOST_AUTO_TEST_CASE(hash160_known_value)
{
    // Hash160 of a known 33-byte compressed pubkey-sized input
    std::vector<unsigned char> input(33, 0x02);
    uint160 hash = Hash160(input);
    BOOST_TEST_MESSAGE("Hash160(33x0x02) = " << hash.ToString());

    // Pinned regression value — if this changes, SHA256+RIPEMD160 implementation is broken
    uint160 expected("0x31479dbc3466dd5d80c1772dedac7086104f8151");
    BOOST_CHECK(hash == expected);

    uint160 hash2 = Hash160(input);
    BOOST_CHECK(hash == hash2);
}

BOOST_AUTO_TEST_CASE(hash160_different_inputs)
{
    std::vector<unsigned char> a(33, 0x02);
    std::vector<unsigned char> b(33, 0x03);

    uint160 hashA = Hash160(a);
    uint160 hashB = Hash160(b);

    BOOST_CHECK(hashA != hashB);
}

BOOST_AUTO_TEST_CASE(hash160_real_pubkey)
{
    // Generate a real key and verify Hash160(pubkey) == GetID()
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();

    uint160 hash160 = Hash160(pubKey.Raw());
    CKeyID keyID = pubKey.GetID();

    // GetID() uses Hash160 internally — must match
    BOOST_CHECK(hash160 == keyID);
}

// ============================================================================
// SerializeHash regression
// ============================================================================

BOOST_AUTO_TEST_CASE(serialize_hash_deterministic)
{
    // SerializeHash of a CTransaction should be deterministic
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;

    uint256 hash1 = SerializeHash(tx);
    uint256 hash2 = SerializeHash(tx);

    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash1 != 0);
}

BOOST_AUTO_TEST_CASE(serialize_hash_field_sensitivity)
{
    CTransaction tx1;
    tx1.nVersion = 1;
    tx1.nTime = 1700000000;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = 50 * COIN;

    CTransaction tx2 = tx1;
    tx2.nTime = 1700000001;  // change one field

    uint256 hash1 = SerializeHash(tx1);
    uint256 hash2 = SerializeHash(tx2);

    BOOST_CHECK(hash1 != hash2);
}

// ============================================================================
// CHashWriter regression
// ============================================================================

BOOST_AUTO_TEST_CASE(hash_writer_matches_hash)
{
    // CHashWriter should produce the same result as Hash() for the same data
    std::string data = "test data for hash writer";

    uint256 directHash = Hash(data.begin(), data.end());

    CHashWriter writer(SER_GETHASH, 0);
    writer.write(data.c_str(), data.size());
    uint256 writerHash = writer.GetHash();

    BOOST_CHECK(directHash == writerHash);
}

BOOST_AUTO_TEST_SUITE_END()
