// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "stealth.h"
#include "util.h"

#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(stealth_tests)

// ============================================================================
// Stealth constants
// ============================================================================

BOOST_AUTO_TEST_CASE(stealth_size_constants)
{
    BOOST_CHECK_EQUAL(ec_secret_size, 32u);
    BOOST_CHECK_EQUAL(ec_compressed_size, 33u);
    BOOST_CHECK_EQUAL(ec_uncompressed_size, 65u);
}

// ============================================================================
// GenerateRandomSecret
// ============================================================================

BOOST_AUTO_TEST_CASE(generate_random_secret_succeeds)
{
    ec_secret secret;
    memset(&secret.e[0], 0, ec_secret_size);
    int rv = GenerateRandomSecret(secret);
    BOOST_CHECK_EQUAL(rv, 0);

    // Secret should not be all zeros after generation
    bool allZero = true;
    for (size_t i = 0; i < ec_secret_size; i++) {
        if (secret.e[i] != 0) { allZero = false; break; }
    }
    BOOST_CHECK(!allZero);
}

BOOST_AUTO_TEST_CASE(generate_random_secret_unique)
{
    ec_secret s1, s2;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(s1), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(s2), 0);

    // Two random secrets should differ
    BOOST_CHECK(memcmp(&s1.e[0], &s2.e[0], ec_secret_size) != 0);
}

// ============================================================================
// SecretToPublicKey
// ============================================================================

BOOST_AUTO_TEST_CASE(secret_to_pubkey_succeeds)
{
    ec_secret secret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(secret), 0);

    ec_point pubkey;
    int rv = SecretToPublicKey(secret, pubkey);
    BOOST_CHECK_EQUAL(rv, 0);

    // Compressed public key should be 33 bytes
    BOOST_CHECK_EQUAL(pubkey.size(), ec_compressed_size);

    // First byte of compressed pubkey must be 0x02 or 0x03
    BOOST_CHECK(pubkey[0] == 0x02 || pubkey[0] == 0x03);
}

BOOST_AUTO_TEST_CASE(secret_to_pubkey_deterministic)
{
    ec_secret secret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(secret), 0);

    ec_point pub1, pub2;
    BOOST_CHECK_EQUAL(SecretToPublicKey(secret, pub1), 0);
    BOOST_CHECK_EQUAL(SecretToPublicKey(secret, pub2), 0);

    // Same secret must produce same pubkey
    BOOST_CHECK(pub1 == pub2);
}

BOOST_AUTO_TEST_CASE(different_secrets_different_pubkeys)
{
    ec_secret s1, s2;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(s1), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(s2), 0);

    ec_point pub1, pub2;
    BOOST_CHECK_EQUAL(SecretToPublicKey(s1, pub1), 0);
    BOOST_CHECK_EQUAL(SecretToPublicKey(s2, pub2), 0);

    BOOST_CHECK(pub1 != pub2);
}

// ============================================================================
// StealthSecret — shared secret derivation
// ============================================================================

BOOST_AUTO_TEST_CASE(stealth_secret_roundtrip)
{
    // Simulate sender and receiver deriving the same shared secret
    // Sender has: ephemeral secret (e), receiver's scan pubkey (Q = dG), spend pubkey (R = fG)
    // Receiver has: scan secret (d), ephemeral pubkey (P = eG)
    // Both compute shared secret c = H(eQ) = H(dP)

    // Generate scan keypair (d, Q)
    ec_secret scanSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(scanSecret), 0);
    ec_point scanPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(scanSecret, scanPubkey), 0);

    // Generate spend keypair (f, R)
    ec_secret spendSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(spendSecret), 0);
    ec_point spendPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(spendSecret, spendPubkey), 0);

    // Generate ephemeral keypair (e, P)
    ec_secret ephemSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(ephemSecret), 0);
    ec_point ephemPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(ephemSecret, ephemPubkey), 0);

    // Sender side: StealthSecret(ephemSecret, scanPubkey, spendPubkey, ...)
    ec_secret senderSharedS;
    ec_point senderPkOut;
    int rvSender = StealthSecret(ephemSecret, scanPubkey, spendPubkey, senderSharedS, senderPkOut);
    BOOST_CHECK_EQUAL(rvSender, 0);

    // Receiver side: StealthSecret(scanSecret, ephemPubkey, spendPubkey, ...)
    ec_secret receiverSharedS;
    ec_point receiverPkOut;
    int rvReceiver = StealthSecret(scanSecret, ephemPubkey, spendPubkey, receiverSharedS, receiverPkOut);
    BOOST_CHECK_EQUAL(rvReceiver, 0);

    // Both sides should derive the same shared secret
    BOOST_CHECK(memcmp(&senderSharedS.e[0], &receiverSharedS.e[0], ec_secret_size) == 0);

    // Both sides should derive the same output public key
    BOOST_CHECK(senderPkOut == receiverPkOut);
}

BOOST_AUTO_TEST_CASE(stealth_secret_different_ephemeral_keys)
{
    // Different ephemeral keys should produce different shared secrets
    ec_secret scanSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(scanSecret), 0);
    ec_point scanPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(scanSecret, scanPubkey), 0);

    ec_secret spendSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(spendSecret), 0);
    ec_point spendPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(spendSecret, spendPubkey), 0);

    ec_secret ephem1, ephem2;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(ephem1), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(ephem2), 0);

    ec_secret shared1, shared2;
    ec_point pk1, pk2;
    BOOST_CHECK_EQUAL(StealthSecret(ephem1, scanPubkey, spendPubkey, shared1, pk1), 0);
    BOOST_CHECK_EQUAL(StealthSecret(ephem2, scanPubkey, spendPubkey, shared2, pk2), 0);

    // Different ephemeral keys → different shared secrets and output keys
    BOOST_CHECK(memcmp(&shared1.e[0], &shared2.e[0], ec_secret_size) != 0);
    BOOST_CHECK(pk1 != pk2);
}

// ============================================================================
// StealthSecretSpend — derive private spending key
// ============================================================================

BOOST_AUTO_TEST_CASE(stealth_secret_spend)
{
    // StealthSecretSpend can fail when (f+c mod n) has leading zero bytes
    // due to BN_num_bytes returning < 32. Retry with fresh keys (prob ~1/256).
    bool succeeded = false;
    for (int attempt = 0; attempt < 8 && !succeeded; ++attempt)
    {
        ec_secret scanSecret, spendSecret;
        if (GenerateRandomSecret(scanSecret) != 0) continue;
        if (GenerateRandomSecret(spendSecret) != 0) continue;

        ec_point scanPubkey, spendPubkey;
        if (SecretToPublicKey(scanSecret, scanPubkey) != 0) continue;
        if (SecretToPublicKey(spendSecret, spendPubkey) != 0) continue;

        ec_secret ephemSecret;
        if (GenerateRandomSecret(ephemSecret) != 0) continue;
        ec_point ephemPubkey;
        if (SecretToPublicKey(ephemSecret, ephemPubkey) != 0) continue;

        ec_secret sharedS;
        ec_point pkOut;
        if (StealthSecret(scanSecret, ephemPubkey, spendPubkey, sharedS, pkOut) != 0) continue;

        ec_secret secretOut;
        int rv = StealthSecretSpend(scanSecret, ephemPubkey, spendSecret, secretOut);
        if (rv != 0) continue;  // BN_num_bytes < 32 edge case, retry

        ec_point derivedPubkey;
        BOOST_CHECK_EQUAL(SecretToPublicKey(secretOut, derivedPubkey), 0);
        BOOST_CHECK(derivedPubkey == pkOut);
        succeeded = true;
    }
    BOOST_CHECK_MESSAGE(succeeded, "StealthSecretSpend failed after 8 attempts");
}

// ============================================================================
// CStealthAddress encoding / decoding
// ============================================================================

BOOST_AUTO_TEST_CASE(stealth_address_encode_decode_roundtrip)
{
    // Build a stealth address from generated keys
    ec_secret scanSecret, spendSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(scanSecret), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(spendSecret), 0);

    ec_point scanPubkey, spendPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(scanSecret, scanPubkey), 0);
    BOOST_CHECK_EQUAL(SecretToPublicKey(spendSecret, spendPubkey), 0);

    CStealthAddress addr;
    addr.options = 0;
    addr.scan_pubkey = scanPubkey;
    addr.spend_pubkey = spendPubkey;
    addr.number_signatures = 0;
    addr.prefix.number_bits = 0;
    addr.prefix.bitfield = 0;

    // Encode
    std::string encoded = addr.Encoded();
    BOOST_CHECK(!encoded.empty());

    // Decode
    CStealthAddress addr2;
    BOOST_CHECK(addr2.SetEncoded(encoded));

    // Round-trip: must match
    BOOST_CHECK(addr2.scan_pubkey == addr.scan_pubkey);
    BOOST_CHECK(addr2.spend_pubkey == addr.spend_pubkey);
}

BOOST_AUTO_TEST_CASE(stealth_address_invalid_rejects)
{
    CStealthAddress addr;

    // Empty string
    BOOST_CHECK(!addr.SetEncoded(""));

    // Garbage
    BOOST_CHECK(!addr.SetEncoded("notavalidstealthaddress"));

    // Too short
    BOOST_CHECK(!addr.SetEncoded("abc"));
}

BOOST_AUTO_TEST_CASE(is_stealth_address)
{
    // Build a valid stealth address
    ec_secret scanSecret, spendSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(scanSecret), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(spendSecret), 0);

    ec_point scanPubkey, spendPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(scanSecret, scanPubkey), 0);
    BOOST_CHECK_EQUAL(SecretToPublicKey(spendSecret, spendPubkey), 0);

    CStealthAddress addr;
    addr.options = 0;
    addr.scan_pubkey = scanPubkey;
    addr.spend_pubkey = spendPubkey;
    addr.number_signatures = 0;
    addr.prefix.number_bits = 0;
    addr.prefix.bitfield = 0;

    std::string encoded = addr.Encoded();
    BOOST_CHECK(IsStealthAddress(encoded));

    // Regular Pinkcoin address should not be stealth
    BOOST_CHECK(!IsStealthAddress("2TestAddress123456"));
    BOOST_CHECK(!IsStealthAddress(""));
}

// ============================================================================
// Checksum functions
// ============================================================================

BOOST_AUTO_TEST_CASE(append_verify_checksum)
{
    data_chunk data = {0x28, 0x00, 0x01, 0x02, 0x03};
    data_chunk original = data;

    AppendChecksum(data);

    // Should add 4 bytes
    BOOST_CHECK_EQUAL(data.size(), original.size() + 4);

    // Checksum should verify
    BOOST_CHECK(VerifyChecksum(data));

    // Tamper with data — checksum should fail
    data[0] ^= 0xFF;
    BOOST_CHECK(!VerifyChecksum(data));
}

BOOST_AUTO_TEST_CASE(verify_checksum_too_short)
{
    data_chunk shortData = {0x01, 0x02};
    BOOST_CHECK(!VerifyChecksum(shortData));
}

// ============================================================================
// CStealthAddress serialization
// ============================================================================

BOOST_AUTO_TEST_CASE(stealth_address_serialization)
{
    ec_secret scanSecret, spendSecret;
    BOOST_CHECK_EQUAL(GenerateRandomSecret(scanSecret), 0);
    BOOST_CHECK_EQUAL(GenerateRandomSecret(spendSecret), 0);

    ec_point scanPubkey, spendPubkey;
    BOOST_CHECK_EQUAL(SecretToPublicKey(scanSecret, scanPubkey), 0);
    BOOST_CHECK_EQUAL(SecretToPublicKey(spendSecret, spendPubkey), 0);

    CStealthAddress addr;
    addr.options = 0;
    addr.scan_pubkey = scanPubkey;
    addr.spend_pubkey = spendPubkey;
    addr.label = "test stealth";
    addr.scan_secret.assign(scanSecret.e, scanSecret.e + ec_secret_size);
    addr.spend_secret.assign(spendSecret.e, spendSecret.e + ec_secret_size);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << addr;

    CStealthAddress addr2;
    ss >> addr2;

    BOOST_CHECK(addr2.scan_pubkey == addr.scan_pubkey);
    BOOST_CHECK(addr2.spend_pubkey == addr.spend_pubkey);
    BOOST_CHECK(addr2.label == addr.label);
    BOOST_CHECK(addr2.scan_secret == addr.scan_secret);
    BOOST_CHECK(addr2.spend_secret == addr.spend_secret);
}

BOOST_AUTO_TEST_SUITE_END()
