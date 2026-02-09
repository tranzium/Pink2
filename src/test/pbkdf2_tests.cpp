// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "pbkdf2.h"

#include <cstring>
#include <vector>

BOOST_AUTO_TEST_SUITE(pbkdf2_tests)

// ============================================================================
// HMAC-SHA256 — RFC 4231 pinned test vectors (OpenSSL migration safety)
// ============================================================================

BOOST_AUTO_TEST_CASE(hmac_sha256_rfc4231_case1)
{
    // Test Case 1: Key = 20 bytes of 0x0b, Data = "Hi There"
    unsigned char key[20];
    memset(key, 0x0b, sizeof(key));
    const char* data = "Hi There";

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key, sizeof(key));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    unsigned char digest[32];
    HMAC_SHA256_Final(digest, &ctx);

    const unsigned char expected[32] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
        0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    BOOST_CHECK(memcmp(digest, expected, 32) == 0);
}

BOOST_AUTO_TEST_CASE(hmac_sha256_rfc4231_case2)
{
    // Test Case 2: Key = "Jefe", Data = "what do ya want for nothing?"
    const char* key = "Jefe";
    const char* data = "what do ya want for nothing?";

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key, strlen(key));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    unsigned char digest[32];
    HMAC_SHA256_Final(digest, &ctx);

    const unsigned char expected[32] = {
        0x5b,0xdc,0xc1,0x46,0xbf,0x60,0x75,0x4e,
        0x6a,0x04,0x24,0x26,0x08,0x95,0x75,0xc7,
        0x5a,0x00,0x3f,0x08,0x9d,0x27,0x39,0x83,
        0x9d,0xec,0x58,0xb9,0x64,0xec,0x38,0x43
    };
    BOOST_CHECK(memcmp(digest, expected, 32) == 0);
}

BOOST_AUTO_TEST_CASE(hmac_sha256_rfc4231_case3)
{
    // Test Case 3: Key = 20 bytes of 0xaa, Data = 50 bytes of 0xdd
    unsigned char key[20];
    memset(key, 0xaa, sizeof(key));
    unsigned char data[50];
    memset(data, 0xdd, sizeof(data));

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key, sizeof(key));
    HMAC_SHA256_Update(&ctx, data, sizeof(data));
    unsigned char digest[32];
    HMAC_SHA256_Final(digest, &ctx);

    const unsigned char expected[32] = {
        0x77,0x3e,0xa9,0x1e,0x36,0x80,0x0e,0x46,
        0x85,0x4d,0xb8,0xeb,0xd0,0x91,0x81,0xa7,
        0x29,0x59,0x09,0x8b,0x3e,0xf8,0xc1,0x22,
        0xd9,0x63,0x55,0x14,0xce,0xd5,0x65,0xfe
    };
    BOOST_CHECK(memcmp(digest, expected, 32) == 0);
}

BOOST_AUTO_TEST_CASE(hmac_sha256_rfc4231_case6)
{
    // Test Case 6: Key = 131 bytes of 0xaa (exceeds 64-byte block, triggers SHA256(K) path)
    // Data = "Test Using Larger Than Block-Size Key - Hash Key First"
    unsigned char key[131];
    memset(key, 0xaa, sizeof(key));
    const char* data = "Test Using Larger Than Block-Size Key - Hash Key First";

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key, sizeof(key));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    unsigned char digest[32];
    HMAC_SHA256_Final(digest, &ctx);

    const unsigned char expected[32] = {
        0x60,0xe4,0x31,0x59,0x1e,0xe0,0xb6,0x7f,
        0x0d,0x8a,0x26,0xaa,0xcb,0xf5,0xb7,0x7f,
        0x8e,0x0b,0xc6,0x21,0x37,0x28,0xc5,0x14,
        0x05,0x46,0x04,0x0f,0x0e,0xe3,0x7f,0x54
    };
    BOOST_CHECK(memcmp(digest, expected, 32) == 0);
}

BOOST_AUTO_TEST_CASE(hmac_sha256_deterministic)
{
    const char* key = "test key";
    const char* data = "test data";

    unsigned char digest1[32], digest2[32];

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key, strlen(key));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    HMAC_SHA256_Final(digest1, &ctx);

    HMAC_SHA256_Init(&ctx, key, strlen(key));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    HMAC_SHA256_Final(digest2, &ctx);

    BOOST_CHECK(memcmp(digest1, digest2, 32) == 0);
}

BOOST_AUTO_TEST_CASE(hmac_sha256_different_keys)
{
    const char* data = "same data";
    const char* key1 = "key one";
    const char* key2 = "key two";

    unsigned char digest1[32], digest2[32];

    HMAC_SHA256_CTX ctx;
    HMAC_SHA256_Init(&ctx, key1, strlen(key1));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    HMAC_SHA256_Final(digest1, &ctx);

    HMAC_SHA256_Init(&ctx, key2, strlen(key2));
    HMAC_SHA256_Update(&ctx, data, strlen(data));
    HMAC_SHA256_Final(digest2, &ctx);

    BOOST_CHECK(memcmp(digest1, digest2, 32) != 0);
}

// ============================================================================
// PBKDF2-SHA256 — RFC 7914 pinned test vectors (OpenSSL migration safety)
// ============================================================================

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_rfc7914_case1)
{
    // PBKDF2-HMAC-SHA256("passwd", "salt", c=1, dkLen=64)
    const uint8_t* passwd = (const uint8_t*)"passwd";
    const uint8_t* salt = (const uint8_t*)"salt";
    uint8_t dk[64];

    PBKDF2_SHA256(passwd, 6, salt, 4, 1, dk, 64);

    // Verified against Python hashlib.pbkdf2_hmac('sha256', b'passwd', b'salt', 1, dklen=64)
    const unsigned char expected[64] = {
        0x55,0xac,0x04,0x6e,0x56,0xe3,0x08,0x9f,
        0xec,0x16,0x91,0xc2,0x25,0x44,0xb6,0x05,
        0xf9,0x41,0x85,0x21,0x6d,0xde,0x04,0x65,
        0xe6,0x8b,0x9d,0x57,0xc2,0x0d,0xac,0xbc,
        0x49,0xca,0x9c,0xcc,0xf1,0x79,0xb6,0x45,
        0x99,0x16,0x64,0xb3,0x9d,0x77,0xef,0x31,
        0x7c,0x71,0xb8,0x45,0xb1,0xe3,0x0b,0xd5,
        0x09,0x11,0x20,0x41,0xd3,0xa1,0x97,0x83
    };
    BOOST_CHECK(memcmp(dk, expected, 64) == 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_rfc7914_case2)
{
    // PBKDF2-HMAC-SHA256("Password", "NaCl", c=80000, dkLen=64)
    const uint8_t* passwd = (const uint8_t*)"Password";
    const uint8_t* salt = (const uint8_t*)"NaCl";
    uint8_t dk[64];

    PBKDF2_SHA256(passwd, 8, salt, 4, 80000, dk, 64);

    const unsigned char expected[64] = {
        0x4d,0xdc,0xd8,0xf6,0x0b,0x98,0xbe,0x21,
        0x83,0x0c,0xee,0x5e,0xf2,0x27,0x01,0xf9,
        0x64,0x1a,0x44,0x18,0xd0,0x4c,0x04,0x14,
        0xae,0xff,0x08,0x87,0x6b,0x34,0xab,0x56,
        0xa1,0xd4,0x25,0xa1,0x22,0x58,0x33,0x54,
        0x9a,0xdb,0x84,0x1b,0x51,0xc9,0xb3,0x17,
        0x6a,0x27,0x2b,0xde,0xbb,0xa1,0xd0,0x78,
        0x47,0x8f,0x62,0xb3,0x97,0xf3,0x3c,0x8d
    };
    BOOST_CHECK(memcmp(dk, expected, 64) == 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_deterministic)
{
    const uint8_t* passwd = (const uint8_t*)"password";
    const uint8_t* salt = (const uint8_t*)"salt";
    uint8_t dk1[32], dk2[32];

    PBKDF2_SHA256(passwd, 8, salt, 4, 1, dk1, 32);
    PBKDF2_SHA256(passwd, 8, salt, 4, 1, dk2, 32);

    BOOST_CHECK(memcmp(dk1, dk2, 32) == 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_different_salt)
{
    const uint8_t* passwd = (const uint8_t*)"password";
    const uint8_t* salt1 = (const uint8_t*)"salt1";
    const uint8_t* salt2 = (const uint8_t*)"salt2";
    uint8_t dk1[32], dk2[32];

    PBKDF2_SHA256(passwd, 8, salt1, 5, 1, dk1, 32);
    PBKDF2_SHA256(passwd, 8, salt2, 5, 1, dk2, 32);

    BOOST_CHECK(memcmp(dk1, dk2, 32) != 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_different_iterations)
{
    const uint8_t* passwd = (const uint8_t*)"password";
    const uint8_t* salt = (const uint8_t*)"salt";
    uint8_t dk1[32], dk2[32];

    PBKDF2_SHA256(passwd, 8, salt, 4, 1, dk1, 32);
    PBKDF2_SHA256(passwd, 8, salt, 4, 2, dk2, 32);

    BOOST_CHECK(memcmp(dk1, dk2, 32) != 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_partial_output)
{
    // dkLen=16 should match first 16 bytes of dkLen=32 (same T_1 block)
    const uint8_t* passwd = (const uint8_t*)"test";
    const uint8_t* salt = (const uint8_t*)"salt";
    uint8_t dk16[16], dk32[32];

    PBKDF2_SHA256(passwd, 4, salt, 4, 1, dk16, 16);
    PBKDF2_SHA256(passwd, 4, salt, 4, 1, dk32, 32);

    BOOST_CHECK(memcmp(dk16, dk32, 16) == 0);
}

BOOST_AUTO_TEST_CASE(pbkdf2_sha256_empty_password)
{
    // Empty password should still produce deterministic valid output
    const uint8_t* passwd = (const uint8_t*)"";
    const uint8_t* salt = (const uint8_t*)"salt";
    uint8_t dk1[32], dk2[32];

    PBKDF2_SHA256(passwd, 0, salt, 4, 1, dk1, 32);
    PBKDF2_SHA256(passwd, 0, salt, 4, 1, dk2, 32);

    // Deterministic
    BOOST_CHECK(memcmp(dk1, dk2, 32) == 0);

    // Different from non-empty password
    const uint8_t* passwd2 = (const uint8_t*)"x";
    uint8_t dk3[32];
    PBKDF2_SHA256(passwd2, 1, salt, 4, 1, dk3, 32);
    BOOST_CHECK(memcmp(dk1, dk3, 32) != 0);
}

BOOST_AUTO_TEST_SUITE_END()
