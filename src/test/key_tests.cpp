#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

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

BOOST_AUTO_TEST_SUITE_END()
