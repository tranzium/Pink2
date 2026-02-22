#include <boost/test/unit_test.hpp>
#include "json/nlohmann/json.hpp"

#include "base58.h"
#include "util.h"
#include "stealth.h"

using json = nlohmann::json;
extern json read_json(const std::string& filename);

BOOST_AUTO_TEST_SUITE(base58_tests)

// Goal: test low-level base58 encoding functionality
BOOST_AUTO_TEST_CASE(base58_EncodeBase58)
{
    json tests = read_json("base58_encode_decode.json");

    for (json& tv : tests)
    {
        json test = tv;
        std::string strTest = tv.dump();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> sourcedata = ParseHex(test[0].get<std::string>());
        std::string base58string = test[1].get<std::string>();
        BOOST_CHECK_MESSAGE(
                    EncodeBase58(&sourcedata[0], &sourcedata[sourcedata.size()]) == base58string,
                    strTest);
    }
}

// Goal: test low-level base58 decoding functionality
BOOST_AUTO_TEST_CASE(base58_DecodeBase58)
{
    json tests = read_json("base58_encode_decode.json");
    std::vector<unsigned char> result;

    for (json& tv : tests)
    {
        json test = tv;
        std::string strTest = tv.dump();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> expected = ParseHex(test[0].get<std::string>());
        std::string base58string = test[1].get<std::string>();
        BOOST_CHECK_MESSAGE(DecodeBase58(base58string, result), strTest);
        BOOST_CHECK_MESSAGE(result.size() == expected.size() && std::equal(result.begin(), result.end(), expected.begin()), strTest);
    }

    BOOST_CHECK(!DecodeBase58("invalid", result));
}

// Visitor to check address type
class TestAddrTypeVisitor
{
private:
    std::string exp_addrType;
public:
    TestAddrTypeVisitor(const std::string &exp_addrType) : exp_addrType(exp_addrType) { }
    bool operator()(const CKeyID &id) const
    {
        return (exp_addrType == "pubkey");
    }
    bool operator()(const CScriptID &id) const
    {
        return (exp_addrType == "script");
    }
    bool operator()(const CNoDestination &no) const
    {
        return (exp_addrType == "none");
    }
    bool operator()(const CStealthAddress &stealth) const
    {
        return (exp_addrType == "stealth");
    }
};

// Visitor to check address payload
class TestPayloadVisitor
{
private:
    std::vector<unsigned char> exp_payload;
public:
    TestPayloadVisitor(std::vector<unsigned char> &exp_payload) : exp_payload(exp_payload) { }
    bool operator()(const CKeyID &id) const
    {
        uint160 exp_key(exp_payload);
        return exp_key == id;
    }
    bool operator()(const CScriptID &id) const
    {
        uint160 exp_key(exp_payload);
        return exp_key == id;
    }
    bool operator()(const CNoDestination &no) const
    {
        return exp_payload.size() == 0;
    }
    bool operator()(const CStealthAddress &stealth) const
    {
        return false; // Stealth address payload comparison not implemented
    }
};

// Goal: test private key and address encoding/decoding roundtrips
// Uses dynamically generated keys instead of Bitcoin-specific test vectors
BOOST_AUTO_TEST_CASE(base58_keys_valid_parse)
{
    // Save global state
    bool fTestNet_stored = fTestNet;

    // Test mainnet keys
    fTestNet = false;
    for (int i = 0; i < 5; i++)
    {
        // Generate deterministic test secret
        std::string seedStr = strprintf("test key seed %d", i);
        uint256 hash = Hash(seedStr.begin(), seedStr.end());
        CSecret secret;
        secret.resize(32);
        memcpy(&secret[0], &hash, 32);

        // Test uncompressed key
        {
            CBitcoinSecret bsecret;
            bsecret.SetSecret(secret, false);
            std::string encoded = bsecret.ToString();

            CBitcoinSecret decoded;
            BOOST_CHECK_MESSAGE(decoded.SetString(encoded), "SetString failed for uncompressed key");
            BOOST_CHECK_MESSAGE(decoded.IsValid(), "IsValid failed for uncompressed key");

            bool fCompressed;
            CSecret recoveredSecret = decoded.GetSecret(fCompressed);
            BOOST_CHECK_MESSAGE(!fCompressed, "Compression flag mismatch");
            BOOST_CHECK_MESSAGE(recoveredSecret == secret, "Secret mismatch");

            // Private key string should not be valid as address
            CBitcoinAddress addr;
            addr.SetString(encoded);
            BOOST_CHECK_MESSAGE(!addr.IsValid(), "Private key valid as address");
        }

        // Test compressed key
        {
            CBitcoinSecret bsecret;
            bsecret.SetSecret(secret, true);
            std::string encoded = bsecret.ToString();

            CBitcoinSecret decoded;
            BOOST_CHECK_MESSAGE(decoded.SetString(encoded), "SetString failed for compressed key");
            BOOST_CHECK_MESSAGE(decoded.IsValid(), "IsValid failed for compressed key");

            bool fCompressed;
            CSecret recoveredSecret = decoded.GetSecret(fCompressed);
            BOOST_CHECK_MESSAGE(fCompressed, "Compression flag mismatch");
            BOOST_CHECK_MESSAGE(recoveredSecret == secret, "Secret mismatch");
        }

        // Test address from pubkey
        {
            CKey key;
            key.SetSecret(secret, true);
            CBitcoinAddress addr(key.GetPubKey().GetID());
            BOOST_CHECK_MESSAGE(addr.IsValid(), "Address not valid");

            std::string encoded = addr.ToString();
            CBitcoinAddress decoded(encoded);
            BOOST_CHECK_MESSAGE(decoded.IsValid(), "Decoded address not valid");
            BOOST_CHECK_MESSAGE(decoded.Get() == addr.Get(), "Address roundtrip mismatch");
            BOOST_CHECK_MESSAGE(!decoded.IsScript(), "Pubkey address marked as script");

            CTxDestination dest = decoded.Get();
            BOOST_CHECK_MESSAGE(std::visit(TestAddrTypeVisitor("pubkey"), dest), "Address type mismatch");

            // Address should not be valid as private key
            CBitcoinSecret secret;
            secret.SetString(encoded);
            BOOST_CHECK_MESSAGE(!secret.IsValid(), "Address valid as private key");
        }
    }

    // Restore global state
    fTestNet = fTestNet_stored;
}

// Goal: test key and address generation
BOOST_AUTO_TEST_CASE(base58_keys_valid_gen)
{
    // Save global state
    bool fTestNet_stored = fTestNet;
    fTestNet = false;

    // Test that CKeyID can be encoded to address and back
    for (int i = 0; i < 5; i++)
    {
        std::string seedStr = strprintf("address test %d", i);
        uint256 hash = Hash(seedStr.begin(), seedStr.end());
        uint160 keyId;
        memcpy(&keyId, &hash, 20);

        CTxDestination dest = CKeyID(keyId);
        CBitcoinAddress addr;
        BOOST_CHECK(std::visit(CBitcoinAddressVisitor(&addr), dest));
        BOOST_CHECK(addr.IsValid());

        // Roundtrip
        CBitcoinAddress decoded(addr.ToString());
        BOOST_CHECK(decoded.IsValid());
        BOOST_CHECK(decoded.Get() == dest);
    }

    // Test that CScriptID can be encoded to address and back
    for (int i = 0; i < 5; i++)
    {
        std::string seedStr = strprintf("script address test %d", i);
        uint256 hash = Hash(seedStr.begin(), seedStr.end());
        uint160 scriptId;
        memcpy(&scriptId, &hash, 20);

        CTxDestination dest = CScriptID(scriptId);
        CBitcoinAddress addr;
        BOOST_CHECK(std::visit(CBitcoinAddressVisitor(&addr), dest));
        BOOST_CHECK(addr.IsValid());
        BOOST_CHECK(addr.IsScript());

        // Roundtrip
        CBitcoinAddress decoded(addr.ToString());
        BOOST_CHECK(decoded.IsValid());
        BOOST_CHECK(decoded.Get() == dest);
    }

    // Visiting a CNoDestination must fail
    CBitcoinAddress dummyAddr;
    CTxDestination nodest = CNoDestination();
    BOOST_CHECK(!std::visit(CBitcoinAddressVisitor(&dummyAddr), nodest));

    // Restore global state
    fTestNet = fTestNet_stored;
}

// Goal: check that base58 parsing code is robust against a variety of corrupted data
BOOST_AUTO_TEST_CASE(base58_keys_invalid)
{
    json tests = read_json("base58_keys_invalid.json"); // Negative testcases
    std::vector<unsigned char> result;
    CBitcoinSecret secret;
    CBitcoinAddress addr;

    for (json& tv : tests)
    {
        json test = tv;
        std::string strTest = tv.dump();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get<std::string>();

        // must be invalid as public and as private key
        addr.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!addr.IsValid(), "IsValid pubkey:" + strTest);
        secret.SetString(exp_base58string);
        BOOST_CHECK_MESSAGE(!secret.IsValid(), "IsValid privkey:" + strTest);
    }
}


BOOST_AUTO_TEST_SUITE_END()
