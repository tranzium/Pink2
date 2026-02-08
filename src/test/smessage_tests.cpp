// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "smessage.h"

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
    // hash[4] + version[2] + flags[1] + timestamp[8] + iv[16] + cpkR[33] + mac[32] + nonse[4] = 100
    // + nPayload[4] + pPayload[ptr] — but we only care about the header portion
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

BOOST_AUTO_TEST_SUITE_END()
