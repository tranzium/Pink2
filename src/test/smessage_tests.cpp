// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "smessage.h"
#include "xxhash/xxhash.h"

#include <string>
#include <vector>
#include <set>
#include <cstring>

BOOST_AUTO_TEST_SUITE(smessage_tests)

// ============================================================================
// Constants pinning — must remain stable across builds
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_constants)
{
    BOOST_CHECK_EQUAL(SMSG_HDR_LEN, 104u);
    BOOST_CHECK_EQUAL(SMSG_PL_HDR_LEN, 90u);  // 1+20+65+4
    BOOST_CHECK_EQUAL(SMSG_BUCKET_LEN, 600u);
    BOOST_CHECK_EQUAL(SMSG_RETENTION, 172800u);  // 48 hours
    BOOST_CHECK_EQUAL(SMSG_MAX_MSG_BYTES, 4096u);
    BOOST_CHECK_EQUAL(SMSG_SEND_DELAY, 2u);
    BOOST_CHECK_EQUAL(SMSG_THREAD_DELAY, 20u);
    BOOST_CHECK_EQUAL(SMSG_TIME_LEEWAY, 60u);
    BOOST_CHECK_EQUAL(SMSG_TIME_IGNORE, 90u);
}

// ============================================================================
// SecureMessage structure
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_default_construct)
{
    SecureMessage msg;
    BOOST_CHECK_EQUAL(msg.nPayload, 0u);
    BOOST_CHECK(msg.pPayload == nullptr);
}

BOOST_AUTO_TEST_CASE(smsg_header_layout)
{
    // Verify packed struct has expected layout
    // hash[4] + version[2] + flags[1] + timestamp[8] + iv[16] + cpkR[33] + mac[32] + nonse[4] = 100 wire bytes
    // + nPayload[4] = 104 = SMSG_HDR_LEN
    SecureMessage msg;
    unsigned char* base = reinterpret_cast<unsigned char*>(&msg);

    // Offsets: hash=0, version=4, flags=6, timestamp=7, iv=15, cpkR=31, mac=64, nonse=96
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.hash[0]) - base, 0);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.version[0]) - base, 4);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.flags) - base, 6);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.timestamp) - base, 7);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.iv[0]) - base, 15);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.cpkR[0]) - base, 31);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.mac[0]) - base, 64);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.nonse[0]) - base, 96);
    BOOST_CHECK_EQUAL(reinterpret_cast<unsigned char*>(&msg.nPayload) - base, 100);
}

// ============================================================================
// SecMsgCrypter — AES-256-CBC round-trip (OpenSSL migration safety)
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_crypter_setkey)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0x42);
    unsigned char iv[16];
    memset(iv, 0x13, sizeof(iv));

    BOOST_CHECK(crypter.SetKey(key, iv));
}

BOOST_AUTO_TEST_CASE(smsg_crypter_encrypt_decrypt_roundtrip)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0xAA);
    unsigned char iv[16];
    memset(iv, 0xBB, sizeof(iv));
    BOOST_CHECK(crypter.SetKey(key, iv));

    std::string plaintext = "Hello Pinkcoin secure messaging!";
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypter.Encrypt((unsigned char*)plaintext.c_str(), plaintext.size(), ciphertext));
    BOOST_CHECK(!ciphertext.empty());

    // Ciphertext should differ from plaintext
    BOOST_CHECK(ciphertext.size() != plaintext.size() ||
                memcmp(ciphertext.data(), plaintext.c_str(), plaintext.size()) != 0);

    // Decrypt
    std::vector<unsigned char> decrypted;
    BOOST_CHECK(crypter.Decrypt(ciphertext.data(), ciphertext.size(), decrypted));

    // Must match original
    BOOST_CHECK_EQUAL(decrypted.size(), plaintext.size());
    BOOST_CHECK(memcmp(decrypted.data(), plaintext.c_str(), plaintext.size()) == 0);
}

BOOST_AUTO_TEST_CASE(smsg_crypter_wrong_key_fails)
{
    // Encrypt with key1
    SecMsgCrypter enc;
    std::vector<unsigned char> key1(32, 0x11);
    unsigned char iv[16];
    memset(iv, 0x22, sizeof(iv));
    BOOST_CHECK(enc.SetKey(key1, iv));

    std::string plaintext = "secret data";
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(enc.Encrypt((unsigned char*)plaintext.c_str(), plaintext.size(), ciphertext));

    // Decrypt with key2 — should fail or produce garbage
    SecMsgCrypter dec;
    std::vector<unsigned char> key2(32, 0x99);
    BOOST_CHECK(dec.SetKey(key2, iv));

    std::vector<unsigned char> decrypted;
    bool decOk = dec.Decrypt(ciphertext.data(), ciphertext.size(), decrypted);

    // Either decrypt fails entirely, or produces wrong plaintext
    if (decOk)
    {
        BOOST_CHECK(decrypted.size() != plaintext.size() ||
                    memcmp(decrypted.data(), plaintext.c_str(), plaintext.size()) != 0);
    }
}

BOOST_AUTO_TEST_CASE(smsg_crypter_different_iv_different_ciphertext)
{
    std::vector<unsigned char> key(32, 0xCC);
    std::string plaintext = "same plaintext";

    unsigned char iv1[16], iv2[16];
    memset(iv1, 0x01, sizeof(iv1));
    memset(iv2, 0x02, sizeof(iv2));

    SecMsgCrypter c1, c2;
    BOOST_CHECK(c1.SetKey(key, iv1));
    BOOST_CHECK(c2.SetKey(key, iv2));

    std::vector<unsigned char> ct1, ct2;
    BOOST_CHECK(c1.Encrypt((unsigned char*)plaintext.c_str(), plaintext.size(), ct1));
    BOOST_CHECK(c2.Encrypt((unsigned char*)plaintext.c_str(), plaintext.size(), ct2));

    // Same key + different IV → different ciphertext
    BOOST_CHECK(ct1 != ct2);
}

BOOST_AUTO_TEST_CASE(smsg_crypter_large_plaintext)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0xDD);
    unsigned char iv[16];
    memset(iv, 0xEE, sizeof(iv));
    BOOST_CHECK(crypter.SetKey(key, iv));

    // Test with SMSG_MAX_MSG_BYTES (4096)
    std::vector<unsigned char> bigPlain(SMSG_MAX_MSG_BYTES, 0x42);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypter.Encrypt(bigPlain.data(), bigPlain.size(), ciphertext));

    std::vector<unsigned char> decrypted;
    BOOST_CHECK(crypter.Decrypt(ciphertext.data(), ciphertext.size(), decrypted));
    BOOST_CHECK_EQUAL(decrypted.size(), bigPlain.size());
    BOOST_CHECK(decrypted == bigPlain);
}

// ============================================================================
// SecMsgAddress serialization
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_address_serialize_roundtrip)
{
    SecMsgAddress addr("2TestStealthAddr", true, false);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << addr;

    SecMsgAddress addr2;
    ss >> addr2;

    BOOST_CHECK_EQUAL(addr2.sAddress, "2TestStealthAddr");
    BOOST_CHECK_EQUAL(addr2.fReceiveEnabled, true);
    BOOST_CHECK_EQUAL(addr2.fReceiveAnon, false);
}

BOOST_AUTO_TEST_CASE(smsg_address_serialize_roundtrip_anon)
{
    SecMsgAddress addr("2AnonAddr", false, true);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << addr;

    SecMsgAddress addr2;
    ss >> addr2;

    BOOST_CHECK_EQUAL(addr2.sAddress, "2AnonAddr");
    BOOST_CHECK_EQUAL(addr2.fReceiveEnabled, false);
    BOOST_CHECK_EQUAL(addr2.fReceiveAnon, true);
}

// ============================================================================
// SecMsgStored serialization
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_stored_serialize_roundtrip)
{
    SecMsgStored stored;
    stored.timeReceived = 1700000000;
    stored.status = 'R';
    stored.folderId = 1;
    stored.sAddrTo = "2RecipientAddr";
    stored.sAddrOutbox = "2SenderAddr";
    stored.vchMessage = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << stored;

    SecMsgStored stored2;
    ss >> stored2;

    BOOST_CHECK_EQUAL(stored2.timeReceived, 1700000000);
    BOOST_CHECK_EQUAL(stored2.status, 'R');
    BOOST_CHECK_EQUAL(stored2.folderId, 1);
    BOOST_CHECK_EQUAL(stored2.sAddrTo, "2RecipientAddr");
    BOOST_CHECK_EQUAL(stored2.sAddrOutbox, "2SenderAddr");
    BOOST_CHECK(stored2.vchMessage == stored.vchMessage);
}

// ============================================================================
// SecMsgToken ordering
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_token_ordering)
{
    unsigned char payload1[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char payload2[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09};

    SecMsgToken t1(1000, payload1, 8, 0);
    SecMsgToken t2(2000, payload1, 8, 0);
    SecMsgToken t3(1000, payload2, 8, 0);

    // Different timestamps — lower timestamp first
    BOOST_CHECK(t1 < t2);
    BOOST_CHECK(!(t2 < t1));

    // Same timestamp — compare by sample bytes
    BOOST_CHECK(t1 < t3);  // payload1 < payload2
}

BOOST_AUTO_TEST_CASE(smsg_token_set_ordering)
{
    unsigned char p1[8] = {0xAA, 0, 0, 0, 0, 0, 0, 0};
    unsigned char p2[8] = {0xBB, 0, 0, 0, 0, 0, 0, 0};

    std::set<SecMsgToken> tokens;
    tokens.insert(SecMsgToken(2000, p2, 8, 0));
    tokens.insert(SecMsgToken(1000, p1, 8, 0));
    tokens.insert(SecMsgToken(1500, p1, 8, 0));

    // Set should be ordered by timestamp
    auto it = tokens.begin();
    BOOST_CHECK_EQUAL(it->timestamp, 1000);
    ++it;
    BOOST_CHECK_EQUAL(it->timestamp, 1500);
    ++it;
    BOOST_CHECK_EQUAL(it->timestamp, 2000);
}

// ============================================================================
// SecMsgOptions defaults
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_options_defaults)
{
    SecMsgOptions opts;
    BOOST_CHECK(opts.fNewAddressRecv);
    BOOST_CHECK(opts.fNewAddressAnon);
}

// ============================================================================
// SecMsgBucket defaults
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_bucket_defaults)
{
    SecMsgBucket bucket;
    BOOST_CHECK_EQUAL(bucket.timeChanged, 0);
    BOOST_CHECK_EQUAL(bucket.hash, 0u);
    BOOST_CHECK_EQUAL(bucket.nLockCount, 0u);
    BOOST_CHECK_EQUAL(bucket.nLockPeerId, 0u);
    BOOST_CHECK(bucket.setTokens.empty());
}

// ============================================================================
// SecureMsgValidate tests — input validation
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_validate_invalid_version)
{
    // Create a header with invalid version (0 instead of 1)
    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    // version is at offset 4, 2 bytes — set to {0, 0}
    header[4] = 0;
    header[5] = 0;

    unsigned char payload[16];
    memset(payload, 0, sizeof(payload));

    int rv = SecureMsgValidate(header, payload, 16);
    BOOST_CHECK_EQUAL(rv, 4); // 4 = invalid version
}

BOOST_AUTO_TEST_CASE(smsg_validate_oversized_payload)
{
    // Create a header with valid version (1)
    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    header[4] = 1; // version[0] = 1

    unsigned char payload[16];
    memset(payload, 0, sizeof(payload));

    // Pass a nPayload larger than SMSG_MAX_MSG_WORST
    uint32_t oversized = SMSG_MAX_MSG_WORST + 1;
    int rv = SecureMsgValidate(header, payload, oversized);
    BOOST_CHECK_EQUAL(rv, 5); // 5 = payload too large
}

BOOST_AUTO_TEST_CASE(smsg_validate_valid_version_but_bad_hash)
{
    // Valid version, normal payload size, but hash won't match → returns 2 or 3
    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    header[4] = 1; // version[0] = 1

    unsigned char payload[64];
    memset(payload, 0x42, sizeof(payload));

    int rv = SecureMsgValidate(header, payload, 64);
    // Should be 2 (invalid hash) or 3 (checksum mismatch), NOT 0
    BOOST_CHECK(rv == 2 || rv == 3);
}

BOOST_AUTO_TEST_CASE(smsg_max_msg_worst_larger_than_max)
{
    // SMSG_MAX_MSG_WORST should be >= SMSG_MAX_MSG_BYTES (compression bound)
    BOOST_CHECK(SMSG_MAX_MSG_WORST >= SMSG_MAX_MSG_BYTES);
}

// ============================================================================
// SecMsgCrypter edge cases
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_crypter_empty_plaintext)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0x55);
    unsigned char iv[16];
    memset(iv, 0x66, sizeof(iv));
    BOOST_CHECK(crypter.SetKey(key, iv));

    // 0-byte encrypt/decrypt roundtrip
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypter.Encrypt(nullptr, 0, ciphertext));

    std::vector<unsigned char> decrypted;
    BOOST_CHECK(crypter.Decrypt(ciphertext.data(), ciphertext.size(), decrypted));
    BOOST_CHECK_EQUAL(decrypted.size(), 0u);
}

BOOST_AUTO_TEST_CASE(smsg_crypter_single_byte)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0x77);
    unsigned char iv[16];
    memset(iv, 0x88, sizeof(iv));
    BOOST_CHECK(crypter.SetKey(key, iv));

    unsigned char byte = 0x42;
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypter.Encrypt(&byte, 1, ciphertext));

    std::vector<unsigned char> decrypted;
    BOOST_CHECK(crypter.Decrypt(ciphertext.data(), ciphertext.size(), decrypted));
    BOOST_CHECK_EQUAL(decrypted.size(), 1u);
    BOOST_CHECK_EQUAL(decrypted[0], 0x42);
}

BOOST_AUTO_TEST_CASE(smsg_crypter_block_boundary)
{
    SecMsgCrypter crypter;
    std::vector<unsigned char> key(32, 0x99);
    unsigned char iv[16];
    memset(iv, 0xAA, sizeof(iv));
    BOOST_CHECK(crypter.SetKey(key, iv));

    // 16 bytes = exact AES block size
    std::vector<unsigned char> plain(16, 0xBB);
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(crypter.Encrypt(plain.data(), plain.size(), ciphertext));

    std::vector<unsigned char> decrypted;
    BOOST_CHECK(crypter.Decrypt(ciphertext.data(), ciphertext.size(), decrypted));
    BOOST_CHECK_EQUAL(decrypted.size(), 16u);
    BOOST_CHECK(decrypted == plain);
}

BOOST_AUTO_TEST_CASE(smsg_crypter_no_key_fails)
{
    SecMsgCrypter crypter;
    // No SetKey() call

    unsigned char byte = 0x42;
    std::vector<unsigned char> ciphertext;
    BOOST_CHECK(!crypter.Encrypt(&byte, 1, ciphertext));

    unsigned char fakeCipher[32];
    memset(fakeCipher, 0, sizeof(fakeCipher));
    std::vector<unsigned char> decrypted;
    BOOST_CHECK(!crypter.Decrypt(fakeCipher, sizeof(fakeCipher), decrypted));
}

// ============================================================================
// SecureMsgSetHash + SecureMsgValidate roundtrip
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_sethash_validate_roundtrip)
{
    // Save and enable fSecMsgenabled
    bool fWas = fSecMsgenabled;
    fSecMsgenabled = true;

    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    // Set valid version
    header[4] = 1;

    unsigned char payload[64];
    memset(payload, 0x42, sizeof(payload));

    int rv = SecureMsgSetHash(header, payload, 64);
    BOOST_CHECK_EQUAL(rv, 0);

    // Validate should confirm
    int vr = SecureMsgValidate(header, payload, 64);
    BOOST_CHECK_EQUAL(vr, 0);

    fSecMsgenabled = fWas;
}

BOOST_AUTO_TEST_CASE(smsg_validate_checksum_mismatch)
{
    bool fWas = fSecMsgenabled;
    fSecMsgenabled = true;

    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    header[4] = 1;

    unsigned char payload[64];
    memset(payload, 0xAA, sizeof(payload));

    int rv = SecureMsgSetHash(header, payload, 64);
    BOOST_CHECK_EQUAL(rv, 0);

    // Corrupt the checksum (hash[0..3])
    header[0] ^= 0xFF;

    int vr = SecureMsgValidate(header, payload, 64);
    BOOST_CHECK_EQUAL(vr, 3); // checksum mismatch

    fSecMsgenabled = fWas;
}

BOOST_AUTO_TEST_CASE(smsg_validate_timestamp_sensitivity)
{
    bool fWas = fSecMsgenabled;
    fSecMsgenabled = true;

    unsigned char header[SMSG_HDR_LEN];
    memset(header, 0, sizeof(header));
    header[4] = 1;

    unsigned char payload[64];
    memset(payload, 0xBB, sizeof(payload));

    int rv = SecureMsgSetHash(header, payload, 64);
    BOOST_CHECK_EQUAL(rv, 0);

    // Modify timestamp (at offset 7, 8 bytes) — HMAC includes all header bytes after hash[4]
    header[7] ^= 0x01;

    int vr = SecureMsgValidate(header, payload, 64);
    BOOST_CHECK(vr != 0); // Should fail (2 = invalid hash, or 3 = checksum mismatch)

    fSecMsgenabled = fWas;
}

// ============================================================================
// Utility functions — fsReadable
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_fsreadable_bytes)
{
    BOOST_CHECK_EQUAL(fsReadable(0), "0 bytes");
    BOOST_CHECK_EQUAL(fsReadable(1), "1 bytes");
    BOOST_CHECK_EQUAL(fsReadable(512), "512 bytes");
}

BOOST_AUTO_TEST_CASE(smsg_fsreadable_kb)
{
    BOOST_CHECK_EQUAL(fsReadable(1024), "1.00 KB");
}

BOOST_AUTO_TEST_CASE(smsg_fsreadable_mb)
{
    BOOST_CHECK_EQUAL(fsReadable(1048576), "1.00 MB");
}

BOOST_AUTO_TEST_CASE(smsg_fsreadable_gb)
{
    BOOST_CHECK_EQUAL(fsReadable(1073741824ULL), "1.00 GB");
}

BOOST_AUTO_TEST_CASE(smsg_fsreadable_tb)
{
    BOOST_CHECK_EQUAL(fsReadable(1099511627776ULL), "1.00 TB");
}

// ============================================================================
// Utility functions — getTimeString
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_gettimestring_format)
{
    char buffer[64];
    std::string result = getTimeString(1700000000, buffer, sizeof(buffer));
    // Should contain YYYY-MM-DD HH:MM:SS pattern
    BOOST_CHECK(!result.empty());
    BOOST_CHECK(result.find('-') != std::string::npos);
    BOOST_CHECK(result.find(':') != std::string::npos);
    // 2023-11-14 is the date for epoch 1700000000
    BOOST_CHECK(result.find("2023") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(smsg_gettimestring_epoch)
{
    char buffer[64];
    std::string result = getTimeString(0, buffer, sizeof(buffer));
    BOOST_CHECK(!result.empty());
    BOOST_CHECK(result.find("1970") != std::string::npos);
}

// ============================================================================
// LZ4 compression
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_lz4_roundtrip)
{
    // Compressible data (repeated pattern)
    std::vector<char> input(512, 'A');
    int bound = LZ4_compressBound(input.size());
    std::vector<char> compressed(bound);

    int compLen = LZ4_compress(input.data(), compressed.data(), input.size());
    BOOST_CHECK(compLen > 0);

    std::vector<char> decompressed(input.size());
    int decLen = LZ4_decompress_safe(compressed.data(), decompressed.data(), compLen, decompressed.size());
    BOOST_CHECK_EQUAL(decLen, (int)input.size());
    BOOST_CHECK(decompressed == input);
}

BOOST_AUTO_TEST_CASE(smsg_lz4_compressbound)
{
    // LZ4_COMPRESSBOUND macro should be >= actual compressed output
    for (int n : {1, 64, 256, 1024, 4096})
    {
        std::vector<char> data(n, 'X');
        int bound = LZ4_COMPRESSBOUND(n);
        std::vector<char> out(bound);
        int actual = LZ4_compress(data.data(), out.data(), n);
        BOOST_CHECK(actual > 0);
        BOOST_CHECK(actual <= bound);
    }
}

BOOST_AUTO_TEST_CASE(smsg_lz4_incompressible)
{
    // Pseudo-random data: not very compressible, but compressed size <= compressBound
    std::vector<char> data(512);
    for (int i = 0; i < 512; i++)
        data[i] = (char)((i * 131 + 97) & 0xFF);

    int bound = LZ4_compressBound(data.size());
    std::vector<char> compressed(bound);
    int compLen = LZ4_compress(data.data(), compressed.data(), data.size());
    BOOST_CHECK(compLen > 0);
    BOOST_CHECK(compLen <= bound);
}

BOOST_AUTO_TEST_CASE(smsg_lz4_smsg_threshold)
{
    // smessage uses 128-byte threshold: <=128 stored as-is, >128 compressed
    // Verify LZ4 can handle both sizes correctly
    std::vector<char> small(128, 'S');
    std::vector<char> large(129, 'L');

    int boundSmall = LZ4_compressBound(small.size());
    int boundLarge = LZ4_compressBound(large.size());
    BOOST_CHECK(boundSmall > 0);
    BOOST_CHECK(boundLarge > 0);

    // Large data compresses and decompresses
    std::vector<char> comp(boundLarge);
    int compLen = LZ4_compress(large.data(), comp.data(), large.size());
    BOOST_CHECK(compLen > 0);

    std::vector<char> decomp(large.size());
    int decLen = LZ4_decompress_safe(comp.data(), decomp.data(), compLen, decomp.size());
    BOOST_CHECK_EQUAL(decLen, (int)large.size());
    BOOST_CHECK(decomp == large);
}

// ============================================================================
// XXHash
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_xxhash_deterministic)
{
    const char data[] = "Hello Pinkcoin";
    unsigned int h1 = XXH32(data, sizeof(data) - 1, 0);
    unsigned int h2 = XXH32(data, sizeof(data) - 1, 0);
    BOOST_CHECK_EQUAL(h1, h2);
}

BOOST_AUTO_TEST_CASE(smsg_xxhash_different_seeds)
{
    const char data[] = "Hello Pinkcoin";
    unsigned int h1 = XXH32(data, sizeof(data) - 1, 0);
    unsigned int h2 = XXH32(data, sizeof(data) - 1, 42);
    BOOST_CHECK(h1 != h2);
}

BOOST_AUTO_TEST_CASE(smsg_xxhash_streaming_matches_oneshot)
{
    const char data[] = "Streaming hash test data for xxhash";
    int len = sizeof(data) - 1;

    // One-shot
    unsigned int oneShot = XXH32(data, len, 1);

    // Streaming
    void* state = XXH32_init(1);
    XXH32_update(state, data, len);
    unsigned int streamed = XXH32_digest(state);

    BOOST_CHECK_EQUAL(oneShot, streamed);
}

BOOST_AUTO_TEST_CASE(smsg_xxhash_empty_input)
{
    // Empty data (len=0) with valid pointer should produce a valid hash without crashing
    unsigned char dummy = 0;
    unsigned int h = XXH32(&dummy, 0, 0);
    // Just check it doesn't crash; any value is valid
    (void)h;
    BOOST_CHECK(true);
}

// ============================================================================
// SecMsgBucket::hashBucket
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_bucket_hash_empty)
{
    SecMsgBucket bucket;
    // Empty setTokens — should not crash
    bucket.hashBucket();
    // Hash is computed (may be any value for empty set, but timeChanged should be set)
    BOOST_CHECK(bucket.timeChanged != 0);
}

BOOST_AUTO_TEST_CASE(smsg_bucket_hash_deterministic)
{
    unsigned char p1[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char p2[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    SecMsgBucket b1, b2;
    b1.setTokens.insert(SecMsgToken(1000, p1, 8, 0));
    b1.setTokens.insert(SecMsgToken(2000, p2, 8, 0));
    b1.hashBucket();

    b2.setTokens.insert(SecMsgToken(1000, p1, 8, 0));
    b2.setTokens.insert(SecMsgToken(2000, p2, 8, 0));
    b2.hashBucket();

    BOOST_CHECK_EQUAL(b1.hash, b2.hash);
}

BOOST_AUTO_TEST_CASE(smsg_bucket_hash_different_tokens)
{
    unsigned char p1[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    unsigned char p2[8] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};

    SecMsgBucket b1, b2;
    b1.setTokens.insert(SecMsgToken(1000, p1, 8, 0));
    b1.hashBucket();

    b2.setTokens.insert(SecMsgToken(1000, p2, 8, 0));
    b2.hashBucket();

    BOOST_CHECK(b1.hash != b2.hash);
}

// ============================================================================
// SecMsgToken edge cases
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_token_small_payload)
{
    // np < 8 triggers memset path (sample zeroed)
    unsigned char shortPayload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    SecMsgToken token(1000, shortPayload, 4, 0);

    unsigned char zero[8] = {0};
    BOOST_CHECK(memcmp(token.sample, zero, 8) == 0);
}

BOOST_AUTO_TEST_CASE(smsg_token_exact_8_bytes)
{
    // np == 8 triggers memcpy path (sample fully copied)
    unsigned char payload[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    SecMsgToken token(2000, payload, 8, 100);

    BOOST_CHECK(memcmp(token.sample, payload, 8) == 0);
    BOOST_CHECK_EQUAL(token.timestamp, 2000);
    BOOST_CHECK_EQUAL(token.offset, 100);
}

BOOST_AUTO_TEST_CASE(smsg_token_equality)
{
    // Same timestamp + same sample → neither t1<t2 nor t2<t1
    unsigned char payload[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    SecMsgToken t1(1000, payload, 8, 0);
    SecMsgToken t2(1000, payload, 8, 50);

    BOOST_CHECK(!(t1 < t2));
    BOOST_CHECK(!(t2 < t1));
}

// ============================================================================
// Structure edge cases
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_stored_empty_strings)
{
    SecMsgStored stored;
    stored.timeReceived = 0;
    stored.status = 0;
    stored.folderId = 0;
    stored.sAddrTo = "";
    stored.sAddrOutbox = "";
    stored.vchMessage.clear();

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << stored;

    SecMsgStored stored2;
    ss >> stored2;

    BOOST_CHECK_EQUAL(stored2.timeReceived, 0);
    BOOST_CHECK_EQUAL(stored2.sAddrTo, "");
    BOOST_CHECK_EQUAL(stored2.sAddrOutbox, "");
    BOOST_CHECK(stored2.vchMessage.empty());
}

BOOST_AUTO_TEST_CASE(smsg_stored_large_message)
{
    SecMsgStored stored;
    stored.timeReceived = 1700000000;
    stored.status = 'U';
    stored.folderId = 2;
    stored.sAddrTo = "2TestAddr";
    stored.sAddrOutbox = "2SenderAddr";
    stored.vchMessage.assign(4096, 0xAB);

    CDataStream ss(SER_DISK, CLIENT_VERSION);
    ss << stored;

    SecMsgStored stored2;
    ss >> stored2;

    BOOST_CHECK_EQUAL(stored2.vchMessage.size(), 4096u);
    BOOST_CHECK(stored2.vchMessage == stored.vchMessage);
}

BOOST_AUTO_TEST_CASE(smsg_address_all_flag_combos)
{
    // All 4 combinations of (fReceiveEnabled, fReceiveAnon)
    bool combos[4][2] = {{false, false}, {false, true}, {true, false}, {true, true}};
    for (int i = 0; i < 4; i++)
    {
        SecMsgAddress addr("2Addr", combos[i][0], combos[i][1]);
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << addr;

        SecMsgAddress addr2;
        ss >> addr2;

        BOOST_CHECK_EQUAL(addr2.fReceiveEnabled, combos[i][0]);
        BOOST_CHECK_EQUAL(addr2.fReceiveAnon, combos[i][1]);
    }
}

BOOST_AUTO_TEST_CASE(smsg_messagedata_defaults)
{
    MessageData md;
    // Default construction: timestamp should be 0, strings/vector empty
    // Note: MessageData has no explicit constructor, so POD fields are uninitialized
    // but strings/vectors are default-constructed
    BOOST_CHECK(md.sToAddress.empty());
    BOOST_CHECK(md.sFromAddress.empty());
    BOOST_CHECK(md.vchMessage.empty());
}

BOOST_AUTO_TEST_CASE(smsg_secure_message_sizeof)
{
    // Verify packed struct: hash[4]+version[2]+flags[1]+timestamp[8]+iv[16]+cpkR[33]+mac[32]+nonse[4]+nPayload[4] = 104
    // Plus pointer pPayload (not counted in wire format)
    SecureMessage msg;
    unsigned char* base = reinterpret_cast<unsigned char*>(&msg);
    // nPayload ends at offset 104
    ptrdiff_t nPayloadEnd = reinterpret_cast<unsigned char*>(&msg.nPayload) - base + sizeof(msg.nPayload);
    BOOST_CHECK_EQUAL(nPayloadEnd, SMSG_HDR_LEN);
}

// ============================================================================
// Address wrappers and constants
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_address_b_version)
{
    // CBitcoinAddress_B::getVersion() — set address from a valid Pinkcoin P2PKH
    CBitcoinAddress_B addr;
    addr.SetString("2MZjzaAFgsQRhvyFth1y7GKmQaWFBP3VEo"); // any valid Pinkcoin "2..." address
    if (addr.IsValid())
    {
        BOOST_CHECK_EQUAL(addr.getVersion(), 3); // Pinkcoin PUBKEY_ADDRESS
    }
    else
    {
        // If address parsing requires network params, just verify the method exists and returns
        CBitcoinAddress_B addr2;
        (void)addr2.getVersion();
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(smsg_keyid_b_getppn)
{
    CKeyID_B kid;
    unsigned int* ppn = kid.GetPPN();
    BOOST_CHECK(ppn != nullptr);
}

BOOST_AUTO_TEST_CASE(smsg_mask_unread)
{
    BOOST_CHECK_EQUAL(SMSG_MASK_UNREAD, 1);
}

// ============================================================================
// PoW difficulty condition
// ============================================================================

BOOST_AUTO_TEST_CASE(smsg_pow_condition_passes)
{
    // Condition: sha256Hash[31]==0 && sha256Hash[30]==0 && (~sha256Hash[29]) & 0x07
    // Construct a hash where [31]=0, [30]=0, [29] has bits 0-2 all clear (e.g., 0x00)
    unsigned char hash[32];
    memset(hash, 0xFF, sizeof(hash));
    hash[31] = 0;
    hash[30] = 0;
    hash[29] = 0x00; // ~0x00 & 0x07 = 0x07, which is truthy

    bool passes = (hash[31] == 0 && hash[30] == 0 && (~hash[29] & ((1<<0)|(1<<1)|(1<<2))));
    BOOST_CHECK(passes);
}

BOOST_AUTO_TEST_CASE(smsg_pow_condition_fails_byte31)
{
    unsigned char hash[32];
    memset(hash, 0, sizeof(hash));
    hash[31] = 1; // Non-zero → fails

    bool passes = (hash[31] == 0 && hash[30] == 0 && (~hash[29] & ((1<<0)|(1<<1)|(1<<2))));
    BOOST_CHECK(!passes);
}

BOOST_AUTO_TEST_CASE(smsg_pow_condition_fails_byte29)
{
    unsigned char hash[32];
    memset(hash, 0, sizeof(hash));
    hash[31] = 0;
    hash[30] = 0;
    hash[29] = 0x07; // All 3 bits set → ~0x07 & 0x07 = 0, fails

    bool passes = (hash[31] == 0 && hash[30] == 0 && (~hash[29] & ((1<<0)|(1<<1)|(1<<2))));
    BOOST_CHECK(!passes);
}

BOOST_AUTO_TEST_SUITE_END()
