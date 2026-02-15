#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>
#include <cstring>

#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/bn.h>

#include "key.h"
#include "base58.h"
#include "uint256.h"
#include "util.h"

using namespace std;


BOOST_AUTO_TEST_SUITE(key_tests)

BOOST_AUTO_TEST_CASE(key_test1)
{
    // Generate test keys dynamically instead of using hardcoded Bitcoin keys
    // Use deterministic secrets for reproducible tests
    CSecret secret1, secret2;
    secret1.resize(32);
    secret2.resize(32);

    // Use hash of known strings for deterministic test secrets
    uint256 hash1 = Hash(string("test secret 1").begin(), string("test secret 1").end());
    uint256 hash2 = Hash(string("test secret 2").begin(), string("test secret 2").end());
    memcpy(&secret1[0], &hash1, 32);
    memcpy(&secret2[0], &hash2, 32);

    // Create keys from secrets
    CKey key1, key2, key1C, key2C;
    key1.SetSecret(secret1, false);   // uncompressed
    key2.SetSecret(secret2, false);   // uncompressed
    key1C.SetSecret(secret1, true);   // compressed
    key2C.SetSecret(secret2, true);   // compressed

    // Test that keys are valid
    BOOST_CHECK(key1.IsValid());
    BOOST_CHECK(key2.IsValid());
    BOOST_CHECK(key1C.IsValid());
    BOOST_CHECK(key2C.IsValid());

    // Test CBitcoinSecret encoding/decoding roundtrip
    CBitcoinSecret bsecret1, bsecret2, bsecret1C, bsecret2C;
    bsecret1.SetSecret(secret1, false);
    bsecret2.SetSecret(secret2, false);
    bsecret1C.SetSecret(secret1, true);
    bsecret2C.SetSecret(secret2, true);

    // Verify roundtrip: encode to string and decode back
    CBitcoinSecret bsecret1_decoded, bsecret2_decoded, bsecret1C_decoded, bsecret2C_decoded;
    BOOST_CHECK(bsecret1_decoded.SetString(bsecret1.ToString()));
    BOOST_CHECK(bsecret2_decoded.SetString(bsecret2.ToString()));
    BOOST_CHECK(bsecret1C_decoded.SetString(bsecret1C.ToString()));
    BOOST_CHECK(bsecret2C_decoded.SetString(bsecret2C.ToString()));

    bool fCompressed;
    BOOST_CHECK(bsecret1_decoded.GetSecret(fCompressed) == secret1);
    BOOST_CHECK(fCompressed == false);
    BOOST_CHECK(bsecret2_decoded.GetSecret(fCompressed) == secret2);
    BOOST_CHECK(fCompressed == false);
    BOOST_CHECK(bsecret1C_decoded.GetSecret(fCompressed) == secret1);
    BOOST_CHECK(fCompressed == true);
    BOOST_CHECK(bsecret2C_decoded.GetSecret(fCompressed) == secret2);
    BOOST_CHECK(fCompressed == true);

    // Test address generation and roundtrip
    CBitcoinAddress addr1(key1.GetPubKey().GetID());
    CBitcoinAddress addr2(key2.GetPubKey().GetID());
    CBitcoinAddress addr1C(key1C.GetPubKey().GetID());
    CBitcoinAddress addr2C(key2C.GetPubKey().GetID());

    BOOST_CHECK(addr1.IsValid());
    BOOST_CHECK(addr2.IsValid());
    BOOST_CHECK(addr1C.IsValid());
    BOOST_CHECK(addr2C.IsValid());

    // Compressed and uncompressed from same secret should give different addresses
    BOOST_CHECK(addr1.ToString() != addr1C.ToString());
    BOOST_CHECK(addr2.ToString() != addr2C.ToString());

    // Verify address roundtrip
    CBitcoinAddress addr1_decoded(addr1.ToString());
    BOOST_CHECK(addr1_decoded.Get() == addr1.Get());

    for (int n=0; n<16; n++)
    {
        string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        vector<unsigned char> sign1, sign2, sign1C, sign2C;

        BOOST_CHECK(key1.Sign (hashMsg, sign1));
        BOOST_CHECK(key2.Sign (hashMsg, sign2));
        BOOST_CHECK(key1C.Sign(hashMsg, sign1C));
        BOOST_CHECK(key2C.Sign(hashMsg, sign2C));

        BOOST_CHECK( key1.Verify(hashMsg, sign1));
        BOOST_CHECK(!key1.Verify(hashMsg, sign2));
        BOOST_CHECK( key1.Verify(hashMsg, sign1C));
        BOOST_CHECK(!key1.Verify(hashMsg, sign2C));

        BOOST_CHECK(!key2.Verify(hashMsg, sign1));
        BOOST_CHECK( key2.Verify(hashMsg, sign2));
        BOOST_CHECK(!key2.Verify(hashMsg, sign1C));
        BOOST_CHECK( key2.Verify(hashMsg, sign2C));

        BOOST_CHECK( key1C.Verify(hashMsg, sign1));
        BOOST_CHECK(!key1C.Verify(hashMsg, sign2));
        BOOST_CHECK( key1C.Verify(hashMsg, sign1C));
        BOOST_CHECK(!key1C.Verify(hashMsg, sign2C));

        BOOST_CHECK(!key2C.Verify(hashMsg, sign1));
        BOOST_CHECK( key2C.Verify(hashMsg, sign2));
        BOOST_CHECK(!key2C.Verify(hashMsg, sign1C));
        BOOST_CHECK( key2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        vector<unsigned char> csign1, csign2, csign1C, csign2C;

        BOOST_CHECK(key1.SignCompact (hashMsg, csign1));
        BOOST_CHECK(key2.SignCompact (hashMsg, csign2));
        BOOST_CHECK(key1C.SignCompact(hashMsg, csign1C));
        BOOST_CHECK(key2C.SignCompact(hashMsg, csign2C));

        CKey rkey1, rkey2, rkey1C, rkey2C;

        BOOST_CHECK(rkey1.SetCompactSignature (hashMsg, csign1));
        BOOST_CHECK(rkey2.SetCompactSignature (hashMsg, csign2));
        BOOST_CHECK(rkey1C.SetCompactSignature(hashMsg, csign1C));
        BOOST_CHECK(rkey2C.SetCompactSignature(hashMsg, csign2C));

        BOOST_CHECK(rkey1.GetPubKey()  == key1.GetPubKey());
        BOOST_CHECK(rkey2.GetPubKey()  == key2.GetPubKey());
        BOOST_CHECK(rkey1C.GetPubKey() == key1C.GetPubKey());
        BOOST_CHECK(rkey2C.GetPubKey() == key2C.GetPubKey());
    }
}

// ============================================================================
// EC_KEY_regenerate_key() — determinism and roundtrip
// ============================================================================

BOOST_AUTO_TEST_CASE(key_regenerate_deterministic)
{
    // Same secret must always produce the same public key via EC_KEY_regenerate_key
    CSecret secret(32, 0);
    uint256 h = Hash(string("deterministic regen test").begin(),
                     string("deterministic regen test").end());
    memcpy(&secret[0], &h, 32);

    CKey k1, k2;
    k1.SetSecret(secret, true);
    k2.SetSecret(secret, true);

    BOOST_CHECK(k1.GetPubKey() == k2.GetPubKey());
    BOOST_CHECK(k1.GetPubKey().IsValid());
    BOOST_CHECK(k1.GetPubKey().IsCompressed());
    BOOST_CHECK_EQUAL(k1.GetPubKey().Raw().size(), 33u);
}

BOOST_AUTO_TEST_CASE(key_regenerate_secret_roundtrip)
{
    // SetSecret (calls EC_KEY_regenerate_key) → GetSecret must return same bytes
    CSecret secret(32, 0);
    uint256 h = Hash(string("roundtrip regen test").begin(),
                     string("roundtrip regen test").end());
    memcpy(&secret[0], &h, 32);

    // Uncompressed
    CKey key;
    key.SetSecret(secret, false);
    bool fCompr;
    CSecret recovered = key.GetSecret(fCompr);
    BOOST_CHECK(recovered == secret);
    BOOST_CHECK_EQUAL(fCompr, false);

    // Compressed
    CKey keyC;
    keyC.SetSecret(secret, true);
    CSecret recoveredC = keyC.GetSecret(fCompr);
    BOOST_CHECK(recoveredC == secret);
    BOOST_CHECK_EQUAL(fCompr, true);
}

BOOST_AUTO_TEST_CASE(key_regenerate_pubkey_format)
{
    CSecret secret(32, 0);
    uint256 h = Hash(string("pubkey format test").begin(),
                     string("pubkey format test").end());
    memcpy(&secret[0], &h, 32);

    CKey keyU, keyC;
    keyU.SetSecret(secret, false);
    keyC.SetSecret(secret, true);

    CPubKey pubU = keyU.GetPubKey();
    CPubKey pubC = keyC.GetPubKey();

    // Uncompressed: 0x04 prefix + 64 bytes (x,y)
    BOOST_CHECK_EQUAL(pubU.Raw().size(), 65u);
    BOOST_CHECK_EQUAL(pubU.Raw()[0], 0x04);

    // Compressed: 0x02 or 0x03 prefix + 32 bytes (x)
    BOOST_CHECK_EQUAL(pubC.Raw().size(), 33u);
    BOOST_CHECK(pubC.Raw()[0] == 0x02 || pubC.Raw()[0] == 0x03);

    // x-coordinates must match between compressed and uncompressed
    BOOST_CHECK(memcmp(&pubU.Raw()[1], &pubC.Raw()[1], 32) == 0);
}

// ============================================================================
// ECDSA_SIG_recover_key_GFp() — compact signature key recovery
// ============================================================================

BOOST_AUTO_TEST_CASE(key_compact_recovery_compressed)
{
    CSecret secret(32, 0);
    uint256 h = Hash(string("compact recovery compressed").begin(),
                     string("compact recovery compressed").end());
    memcpy(&secret[0], &h, 32);

    CKey key;
    key.SetSecret(secret, true);

    uint256 msgHash = Hash(string("test message for recovery").begin(),
                           string("test message for recovery").end());

    // SignCompact internally calls ECDSA_SIG_recover_key_GFp to find recid
    vector<unsigned char> csig;
    BOOST_CHECK(key.SignCompact(msgHash, csig));
    BOOST_CHECK_EQUAL(csig.size(), 65u);

    // Header byte for compressed key: 31..34 (27 + recid + 4)
    BOOST_CHECK(csig[0] >= 31 && csig[0] <= 34);

    // SetCompactSignature exercises ECDSA_SIG_recover_key_GFp for recovery
    CKey recovered;
    BOOST_CHECK(recovered.SetCompactSignature(msgHash, csig));
    BOOST_CHECK(recovered.GetPubKey() == key.GetPubKey());
}

BOOST_AUTO_TEST_CASE(key_compact_recovery_uncompressed)
{
    CSecret secret(32, 0);
    uint256 h = Hash(string("compact recovery uncompressed").begin(),
                     string("compact recovery uncompressed").end());
    memcpy(&secret[0], &h, 32);

    CKey key;
    key.SetSecret(secret, false);

    uint256 msgHash = Hash(string("uncompressed recovery msg").begin(),
                           string("uncompressed recovery msg").end());

    vector<unsigned char> csig;
    BOOST_CHECK(key.SignCompact(msgHash, csig));
    BOOST_CHECK_EQUAL(csig.size(), 65u);

    // Header byte for uncompressed key: 27..30
    BOOST_CHECK(csig[0] >= 27 && csig[0] <= 30);

    CKey recovered;
    BOOST_CHECK(recovered.SetCompactSignature(msgHash, csig));
    BOOST_CHECK(recovered.GetPubKey() == key.GetPubKey());
}

BOOST_AUTO_TEST_CASE(key_compact_recovery_wrong_message)
{
    CSecret secret(32, 0);
    uint256 h = Hash(string("wrong msg recovery").begin(),
                     string("wrong msg recovery").end());
    memcpy(&secret[0], &h, 32);

    CKey key;
    key.SetSecret(secret, true);

    uint256 correctHash = Hash(string("correct message").begin(),
                               string("correct message").end());
    uint256 wrongHash = Hash(string("wrong message").begin(),
                             string("wrong message").end());

    vector<unsigned char> csig;
    BOOST_CHECK(key.SignCompact(correctHash, csig));

    // VerifyCompact with wrong message must fail — recovered key won't match
    BOOST_CHECK(!key.VerifyCompact(wrongHash, csig));
}

BOOST_AUTO_TEST_CASE(key_verify_compact_roundtrip)
{
    CSecret secret(32, 0);
    uint256 h = Hash(string("verify compact roundtrip").begin(),
                     string("verify compact roundtrip").end());
    memcpy(&secret[0], &h, 32);

    CKey key;
    key.SetSecret(secret, true);

    // Sign and verify multiple messages via compact signature
    for (int i = 0; i < 8; i++)
    {
        string msg = strprintf("Compact verify message %d", i);
        uint256 msgHash = Hash(msg.begin(), msg.end());

        vector<unsigned char> csig;
        BOOST_CHECK(key.SignCompact(msgHash, csig));
        BOOST_CHECK(key.VerifyCompact(msgHash, csig));
    }
}

// ============================================================================
// secp256k1 constants pinning and CheckSignatureElement bounds
// ============================================================================

BOOST_AUTO_TEST_CASE(key_secp256k1_constants_pinned)
{
    // secp256k1 order n = FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
    // vchMaxModOrder = n - 1 (pinned for OpenSSL migration safety)
    const unsigned char orderMinus1[32] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
        0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
        0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x40
    };

    // vchMaxModHalfOrder = (n-1)/2
    const unsigned char halfOrder[32] = {
        0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,
        0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0
    };

    // n-1 valid for full range, invalid for half range
    BOOST_CHECK(CKey::CheckSignatureElement(orderMinus1, 32, false));
    BOOST_CHECK(!CKey::CheckSignatureElement(orderMinus1, 32, true));

    // (n-1)/2 valid for both ranges
    BOOST_CHECK(CKey::CheckSignatureElement(halfOrder, 32, false));
    BOOST_CHECK(CKey::CheckSignatureElement(halfOrder, 32, true));

    // n (= n-1 + 1) must be invalid for full range
    unsigned char order[32];
    memcpy(order, orderMinus1, 32);
    order[31] += 1;  // 0x40 + 1 = 0x41 → this is n itself
    BOOST_CHECK(!CKey::CheckSignatureElement(order, 32, false));

    // Zero: invalid (must be > 0)
    unsigned char zero[32] = {0};
    BOOST_CHECK(!CKey::CheckSignatureElement(zero, 32, false));
    BOOST_CHECK(!CKey::CheckSignatureElement(zero, 32, true));

    // One: valid for both
    unsigned char one[32] = {0};
    one[31] = 0x01;
    BOOST_CHECK(CKey::CheckSignatureElement(one, 32, false));
    BOOST_CHECK(CKey::CheckSignatureElement(one, 32, true));
}

// ============================================================================
// CKey utility operations
// ============================================================================

BOOST_AUTO_TEST_CASE(key_copy_constructor)
{
    CKey original;
    original.MakeNewKey(true);
    CPubKey origPub = original.GetPubKey();

    CKey copy(original);
    BOOST_CHECK(copy.GetPubKey() == origPub);
    BOOST_CHECK(copy.IsValid());
}

BOOST_AUTO_TEST_CASE(key_assignment_operator)
{
    CKey original;
    original.MakeNewKey(false);
    CPubKey origPub = original.GetPubKey();

    CKey assigned;
    assigned = original;
    BOOST_CHECK(assigned.GetPubKey() == origPub);
    BOOST_CHECK(assigned.IsValid());
}

BOOST_AUTO_TEST_CASE(key_make_new_key_valid)
{
    CKey keyC;
    keyC.MakeNewKey(true);
    BOOST_CHECK(keyC.IsValid());
    BOOST_CHECK(keyC.IsCompressed());
    BOOST_CHECK(keyC.GetPubKey().IsCompressed());

    CKey keyU;
    keyU.MakeNewKey(false);
    BOOST_CHECK(keyU.IsValid());
    BOOST_CHECK(!keyU.IsCompressed());
    BOOST_CHECK(!keyU.GetPubKey().IsCompressed());

    // Two new random keys should differ
    BOOST_CHECK(keyC.GetPubKey() != keyU.GetPubKey());
}

BOOST_AUTO_TEST_CASE(key_null_before_set)
{
    CKey key;
    BOOST_CHECK(key.IsNull());
    key.MakeNewKey(true);
    BOOST_CHECK(!key.IsNull());
}

BOOST_AUTO_TEST_CASE(key_ecc_sanity)
{
    BOOST_CHECK(ECC_InitSanityCheck());
}

// ============================================================================
// DER encoding/decoding and wallet.dat backward compatibility
// ============================================================================

// Helper: create an OpenSSL EC_KEY, serialize with i2d_ECPrivateKey, return DER + pubkey
static CPrivKey MakeOpenSSLDER(const unsigned char secret[32], bool fCompressed,
                               std::vector<unsigned char>& vchPubKey)
{
    EC_KEY* eckey = EC_KEY_new_by_curve_name(NID_secp256k1);
    BIGNUM* bn = BN_bin2bn(secret, 32, nullptr);
    EC_KEY_set_private_key(eckey, bn);

    // Compute public key
    const EC_GROUP* group = EC_KEY_get0_group(eckey);
    EC_POINT* pub = EC_POINT_new(group);
    BN_CTX* ctx = BN_CTX_new();
    EC_POINT_mul(group, pub, bn, nullptr, nullptr, ctx);
    EC_KEY_set_public_key(eckey, pub);
    EC_KEY_set_conv_form(eckey, fCompressed ? POINT_CONVERSION_COMPRESSED
                                            : POINT_CONVERSION_UNCOMPRESSED);

    // DER encode
    int n = i2d_ECPrivateKey(eckey, nullptr);
    CPrivKey der(n, 0);
    unsigned char* pp = &der[0];
    i2d_ECPrivateKey(eckey, &pp);

    // Serialize public key
    size_t publen = fCompressed ? 33 : 65;
    vchPubKey.resize(publen);
    unsigned char* pbuf = &vchPubKey[0];
    size_t outlen = publen;
    EC_POINT_point2oct(group, pub, fCompressed ? POINT_CONVERSION_COMPRESSED
                                               : POINT_CONVERSION_UNCOMPRESSED,
                       pbuf, outlen, ctx);

    BN_CTX_free(ctx);
    EC_POINT_free(pub);
    BN_free(bn);
    EC_KEY_free(eckey);
    return der;
}

// Helper: construct a SEC 1 DER blob manually with a given private key length
// (simulates older OpenSSL versions that don't zero-pad)
static CPrivKey MakeManualDER(const unsigned char* secretBytes, size_t secretLen,
                              const std::vector<unsigned char>& pubkey)
{
    // secp256k1 OID: 06 05 2B 81 04 00 0A
    const unsigned char oid[] = { 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x0A };

    size_t publen = pubkey.size(); // 33 or 65
    size_t bslen = 1 + publen;    // BIT STRING content: unused-bits + pubkey
    size_t a1len = 2 + bslen;     // BIT STRING TLV
    size_t inner = 3                          // version: 02 01 01
                 + 2 + secretLen              // OCTET STRING: 04 NN <secret>
                 + 2 + sizeof(oid)            // [0]: A0 07 <oid>
                 + 2 + a1len;                 // [1]: A1 NN ...

    CPrivKey der;
    der.reserve(2 + inner);

    // SEQUENCE
    der.push_back(0x30);
    der.push_back(static_cast<unsigned char>(inner));

    // version
    der.push_back(0x02); der.push_back(0x01); der.push_back(0x01);

    // OCTET STRING (variable length)
    der.push_back(0x04);
    der.push_back(static_cast<unsigned char>(secretLen));
    der.insert(der.end(), secretBytes, secretBytes + secretLen);

    // [0] parameters
    der.push_back(0xA0);
    der.push_back(static_cast<unsigned char>(sizeof(oid)));
    der.insert(der.end(), oid, oid + sizeof(oid));

    // [1] publicKey
    der.push_back(0xA1);
    der.push_back(static_cast<unsigned char>(a1len));
    der.push_back(0x03);
    der.push_back(static_cast<unsigned char>(bslen));
    der.push_back(0x00); // unused bits
    der.insert(der.end(), pubkey.begin(), pubkey.end());

    return der;
}

BOOST_AUTO_TEST_CASE(key_privkey_roundtrip_compressed)
{
    // EncodePrivKey → DecodePrivKey roundtrip with compressed key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pub = key.GetPubKey();
    CPrivKey der = key.GetPrivKey();

    CKey key2;
    BOOST_CHECK(key2.SetPrivKey(der));
    BOOST_CHECK(key2.IsCompressed());
    BOOST_CHECK(key2.GetPubKey() == pub);
    BOOST_CHECK(key2.IsValid());

    // Verify signing still works after round-trip
    uint256 h = Hash(string("roundtrip test").begin(), string("roundtrip test").end());
    vector<unsigned char> sig;
    BOOST_CHECK(key2.Sign(h, sig));
    BOOST_CHECK(key.Verify(h, sig));
}

BOOST_AUTO_TEST_CASE(key_privkey_roundtrip_uncompressed)
{
    // EncodePrivKey → DecodePrivKey roundtrip with uncompressed key
    CKey key;
    key.MakeNewKey(false);
    CPubKey pub = key.GetPubKey();
    CPrivKey der = key.GetPrivKey();

    CKey key2;
    BOOST_CHECK(key2.SetPrivKey(der));
    BOOST_CHECK(!key2.IsCompressed());
    BOOST_CHECK(key2.GetPubKey() == pub);
    BOOST_CHECK(key2.IsValid());
}

BOOST_AUTO_TEST_CASE(key_openssl_der_compat_compressed)
{
    // OpenSSL's i2d_ECPrivateKey → new SetPrivKey (compressed)
    // This is the exact code path that wallet.dat loading exercises.
    CSecret secret(32, 0);
    uint256 h = Hash(string("openssl compat compressed").begin(),
                     string("openssl compat compressed").end());
    memcpy(&secret[0], &h, 32);

    vector<unsigned char> vchPubKey;
    CPrivKey der = MakeOpenSSLDER(&secret[0], true, vchPubKey);

    // Simulate walletdb.cpp loading: SetPubKey first, then SetPrivKey
    CKey key;
    key.SetPubKey(CPubKey(vchPubKey));
    BOOST_CHECK(key.SetPrivKey(der));
    BOOST_CHECK(key.IsCompressed());
    BOOST_CHECK(key.GetPubKey() == CPubKey(vchPubKey));
    BOOST_CHECK(key.IsValid());
}

BOOST_AUTO_TEST_CASE(key_openssl_der_compat_uncompressed)
{
    // OpenSSL's i2d_ECPrivateKey → new SetPrivKey (uncompressed)
    CSecret secret(32, 0);
    uint256 h = Hash(string("openssl compat uncompressed").begin(),
                     string("openssl compat uncompressed").end());
    memcpy(&secret[0], &h, 32);

    vector<unsigned char> vchPubKey;
    CPrivKey der = MakeOpenSSLDER(&secret[0], false, vchPubKey);

    CKey key;
    key.SetPubKey(CPubKey(vchPubKey));
    BOOST_CHECK(key.SetPrivKey(der));
    BOOST_CHECK(!key.IsCompressed());
    BOOST_CHECK(key.GetPubKey() == CPubKey(vchPubKey));
    BOOST_CHECK(key.IsValid());
}

BOOST_AUTO_TEST_CASE(key_decode_short_privkey_31)
{
    // Simulate OpenSSL 1.0.x: private key with leading zero → 31-byte OCTET STRING
    unsigned char secret32[32];
    uint256 h = Hash(string("short key 31").begin(), string("short key 31").end());
    memcpy(secret32, &h, 32);
    secret32[0] = 0x00; // Force leading zero

    // Get the correct public key for this secret
    CKey refKey;
    CSecret secVec(secret32, secret32 + 32);
    refKey.SetSecret(secVec, true);
    CPubKey refPub = refKey.GetPubKey();

    // Build DER with 31-byte OCTET STRING (strip leading zero, like OpenSSL 1.0.x)
    CPrivKey der = MakeManualDER(secret32 + 1, 31, refPub.Raw());

    CKey loaded;
    BOOST_CHECK(loaded.SetPrivKey(der));
    BOOST_CHECK(loaded.IsCompressed());
    BOOST_CHECK(loaded.GetPubKey() == refPub);
    BOOST_CHECK(loaded.IsValid());
}

BOOST_AUTO_TEST_CASE(key_decode_short_privkey_30)
{
    // Two leading zero bytes → 30-byte OCTET STRING
    unsigned char secret32[32];
    uint256 h = Hash(string("short key 30").begin(), string("short key 30").end());
    memcpy(secret32, &h, 32);
    secret32[0] = 0x00;
    secret32[1] = 0x00;

    CKey refKey;
    CSecret secVec(secret32, secret32 + 32);
    refKey.SetSecret(secVec, true);
    CPubKey refPub = refKey.GetPubKey();

    CPrivKey der = MakeManualDER(secret32 + 2, 30, refPub.Raw());

    CKey loaded;
    BOOST_CHECK(loaded.SetPrivKey(der));
    BOOST_CHECK(loaded.GetPubKey() == refPub);
}

BOOST_AUTO_TEST_CASE(key_decode_no_pubkey_tag)
{
    // DER without [1] publicKey tag — fCompressed should default to true
    CKey refKey;
    refKey.MakeNewKey(true);
    bool fCompr;
    CSecret secret = refKey.GetSecret(fCompr);

    // Build DER manually: version + OCTET STRING + [0] params (no [1] pubkey)
    const unsigned char oid[] = { 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x0A };
    size_t inner = 3 + 34 + 2 + sizeof(oid); // version + key + [0]

    CPrivKey der;
    der.push_back(0x30);
    der.push_back(static_cast<unsigned char>(inner));
    der.push_back(0x02); der.push_back(0x01); der.push_back(0x01);
    der.push_back(0x04); der.push_back(0x20);
    der.insert(der.end(), &secret[0], &secret[0] + 32);
    der.push_back(0xA0);
    der.push_back(static_cast<unsigned char>(sizeof(oid)));
    der.insert(der.end(), oid, oid + sizeof(oid));

    CKey loaded;
    BOOST_CHECK(loaded.SetPrivKey(der));
    BOOST_CHECK(loaded.IsCompressed()); // default when [1] absent
    BOOST_CHECK(loaded.GetPubKey() == refKey.GetPubKey());
}

BOOST_AUTO_TEST_CASE(key_decode_minimal_der)
{
    // Minimal DER: version + OCTET STRING only (no [0] params, no [1] pubkey)
    CKey refKey;
    refKey.MakeNewKey(true);
    bool fCompr;
    CSecret secret = refKey.GetSecret(fCompr);

    size_t inner = 3 + 34; // version + key
    CPrivKey der;
    der.push_back(0x30);
    der.push_back(static_cast<unsigned char>(inner));
    der.push_back(0x02); der.push_back(0x01); der.push_back(0x01);
    der.push_back(0x04); der.push_back(0x20);
    der.insert(der.end(), &secret[0], &secret[0] + 32);

    CKey loaded;
    BOOST_CHECK(loaded.SetPrivKey(der));
    BOOST_CHECK(loaded.IsCompressed()); // default
    BOOST_CHECK(loaded.GetPubKey() == refKey.GetPubKey());
}

BOOST_AUTO_TEST_CASE(key_setprivkey_invalid_der)
{
    // Various invalid DER blobs must be rejected by both custom + OpenSSL decoders
    CKey key;

    // Empty
    CPrivKey empty;
    BOOST_CHECK(!key.SetPrivKey(empty));

    // Too short
    CPrivKey tiny = {0x30, 0x01};
    BOOST_CHECK(!key.SetPrivKey(tiny));

    // Wrong tag (not SEQUENCE — rejected by both decoders)
    CPrivKey wrongTag = {0x31, 0x25, 0x02, 0x01, 0x01, 0x04, 0x20};
    wrongTag.resize(39, 0xAA);
    BOOST_CHECK(!key.SetPrivKey(wrongTag));

    // Random garbage (not valid DER at all)
    CPrivKey garbage = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB};
    BOOST_CHECK(!key.SetPrivKey(garbage));

    // Valid DER structure but secret = 0 (fails secp256k1_ec_seckey_verify)
    CPrivKey zeroSecret;
    zeroSecret.push_back(0x30); zeroSecret.push_back(0x25);
    zeroSecret.push_back(0x02); zeroSecret.push_back(0x01); zeroSecret.push_back(0x01);
    zeroSecret.push_back(0x04); zeroSecret.push_back(0x20);
    zeroSecret.resize(39, 0x00); // 32 zero bytes = invalid key
    BOOST_CHECK(!key.SetPrivKey(zeroSecret));
}

BOOST_AUTO_TEST_CASE(key_openssl_der_multiple_keys)
{
    // Test 20 random keys through OpenSSL DER → SetPrivKey to catch edge cases
    for (int i = 0; i < 20; i++)
    {
        CKey origKey;
        origKey.MakeNewKey(i < 10); // 10 compressed, 10 uncompressed
        bool fCompr;
        CSecret secret = origKey.GetSecret(fCompr);
        CPubKey origPub = origKey.GetPubKey();

        vector<unsigned char> vchPubKey;
        CPrivKey der = MakeOpenSSLDER(&secret[0], fCompr, vchPubKey);

        // Wallet loading flow: SetPubKey → SetPrivKey → verify
        CKey loaded;
        loaded.SetPubKey(CPubKey(vchPubKey));
        BOOST_CHECK_MESSAGE(loaded.SetPrivKey(der),
            "SetPrivKey failed for key " + std::to_string(i));
        BOOST_CHECK(loaded.GetPubKey() == origPub);

        // Verify signing works
        uint256 hash = Hash(string("multi key test " + std::to_string(i)).begin(),
                           string("multi key test " + std::to_string(i)).end());
        vector<unsigned char> sig;
        BOOST_CHECK(loaded.Sign(hash, sig));
        BOOST_CHECK(origKey.Verify(hash, sig));
    }
}

BOOST_AUTO_TEST_SUITE_END()
