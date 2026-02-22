// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "base58.h"
#include "key.h"
#include "main.h"
#include "script.h"
#include "util.h"

BOOST_AUTO_TEST_SUITE(rpc_tests)

// ============================================================================
// ValueFromAmount / AmountFromValue tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_ValueFromAmount)
{
    // Test conversion from satoshis to JSON value
    BOOST_CHECK_EQUAL(ValueFromAmount(0).get<double>(), 0.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN).get<double>(), 1.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 2).get<double>(), 0.5);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10).get<double>(), 0.1);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 100).get<double>(), 0.01);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 1000).get<double>(), 0.001);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10000).get<double>(), 0.0001);

    // Large amounts
    BOOST_CHECK_EQUAL(ValueFromAmount(100 * COIN).get<double>(), 100.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(1000000 * COIN).get<double>(), 1000000.0);

    // Negative amounts (internal representation)
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN).get<double>(), -1.0);
}

BOOST_AUTO_TEST_CASE(rpc_AmountFromValue)
{
    // Test conversion from JSON value to satoshis
    BOOST_CHECK_EQUAL(AmountFromValue(json(1.0)), COIN);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.5)), COIN / 2);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.1)), COIN / 10);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.01)), COIN / 100);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.001)), COIN / 1000);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.0001)), COIN / 10000);
    BOOST_CHECK_EQUAL(AmountFromValue(json(0.00000001)), 1); // 1 satoshi

    // Larger amounts
    BOOST_CHECK_EQUAL(AmountFromValue(json(100.0)), 100 * COIN);
    BOOST_CHECK_EQUAL(AmountFromValue(json(1000.0)), 1000 * COIN);

    // Invalid amounts should throw (throws json object)
    BOOST_CHECK_THROW(AmountFromValue(json(0.0)), json);
    BOOST_CHECK_THROW(AmountFromValue(json(-1.0)), json);
}

// ============================================================================
// ParseHashV / ParseHexV tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_ParseHashV)
{
    // Valid 64-character hex string (256-bit hash)
    std::string validHash = "0000000000000000000000000000000000000000000000000000000000000001";
    json v(validHash);
    uint256 result = ParseHashV(v, "testhash");
    BOOST_CHECK_EQUAL(result.GetHex(), validHash);

    // All zeros
    std::string zeroHash = "0000000000000000000000000000000000000000000000000000000000000000";
    result = ParseHashV(json(zeroHash), "zerohash");
    BOOST_CHECK(result == 0);

    // Invalid: non-hex characters (throws json object)
    BOOST_CHECK_THROW(ParseHashV(json("not_a_hex_string"), "badhash"), json);

    // Invalid: empty string
    BOOST_CHECK_THROW(ParseHashV(json(""), "emptyhash"), json);

    // Invalid: wrong type (not a string)
    BOOST_CHECK_THROW(ParseHashV(json(12345), "wrongtype"), json);
}

BOOST_AUTO_TEST_CASE(rpc_ParseHexV)
{
    // Valid hex string
    json v("deadbeef");
    std::vector<unsigned char> result = ParseHexV(v, "testhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);
    BOOST_CHECK_EQUAL(result[0], 0xde);
    BOOST_CHECK_EQUAL(result[1], 0xad);
    BOOST_CHECK_EQUAL(result[2], 0xbe);
    BOOST_CHECK_EQUAL(result[3], 0xef);

    // Valid: uppercase hex
    result = ParseHexV(json("DEADBEEF"), "upperhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);

    // Valid: mixed case
    result = ParseHexV(json("DeAdBeEf"), "mixedhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);

    // Invalid: non-hex (throws json object)
    BOOST_CHECK_THROW(ParseHexV(json("xyz"), "badhex"), json);

    // Invalid: empty
    BOOST_CHECK_THROW(ParseHexV(json(""), "emptyhex"), json);

    // Odd length hex is invalid (IsHex rejects odd-length strings)
    BOOST_CHECK_THROW(ParseHexV(json("abc"), "oddhex"), json);
}

// ============================================================================
// RPCTypeCheck tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_array)
{
    // Test array type checking
    json params = json::array();
    params.push_back("string_value");
    params.push_back(123);
    params.push_back(45.67);
    params.push_back(true);

    // Valid types - should not throw
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::string, json::value_t::number_integer, json::value_t::number_float, json::value_t::boolean}));

    // Valid with fewer expected types
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::string, json::value_t::number_integer}));

    // Invalid: wrong type at position 0 (throws json object)
    BOOST_CHECK_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::number_integer}), json);

    // Invalid: wrong type at position 1
    BOOST_CHECK_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::string, json::value_t::string}), json);
}

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_array_with_null)
{
    json params = json::array();
    params.push_back(nullptr);
    params.push_back("test");

    // Without fAllowNull, null doesn't match str_type (throws json object)
    BOOST_CHECK_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::string, json::value_t::string}, false), json);

    // With fAllowNull, null is acceptable
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, std::list<json::value_t>{json::value_t::string, json::value_t::string}, true));
}

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_object)
{
    json obj;
    obj["name"] = "test";
    obj["count"] = 42;
    obj["enabled"] = true;

    // Valid types
    std::map<std::string, json::value_t> expected;
    expected["name"] = json::value_t::string;
    expected["count"] = json::value_t::number_integer;
    expected["enabled"] = json::value_t::boolean;
    BOOST_CHECK_NO_THROW(RPCTypeCheck(obj, expected));

    // Check subset of fields
    std::map<std::string, json::value_t> subset;
    subset["name"] = json::value_t::string;
    BOOST_CHECK_NO_THROW(RPCTypeCheck(obj, subset));

    // Wrong type for existing field (throws json object)
    std::map<std::string, json::value_t> wrongType;
    wrongType["name"] = json::value_t::number_integer;
    BOOST_CHECK_THROW(RPCTypeCheck(obj, wrongType), json);

    // Missing required field (without fAllowNull)
    std::map<std::string, json::value_t> missing;
    missing["nonexistent"] = json::value_t::string;
    BOOST_CHECK_THROW(RPCTypeCheck(obj, missing, false), json);

    // Missing field allowed with fAllowNull
    BOOST_CHECK_NO_THROW(RPCTypeCheck(obj, missing, true));
}

// ============================================================================
// HexBits tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_HexBits)
{
    // Test conversion of nBits to hex string
    // nBits is the compact representation of difficulty target
    std::string result = HexBits(0x1d00ffff); // Genesis block difficulty
    BOOST_CHECK_EQUAL(result.size(), 8u); // 4 bytes = 8 hex chars

    // Verify it's valid hex
    BOOST_CHECK(IsHex(result));
}

// ============================================================================
// JSONRPCError tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_JSONRPCError)
{
    json error = JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Test error message");

    // Check structure
    BOOST_CHECK_EQUAL(error["code"].get<int>(), RPC_INVALID_ADDRESS_OR_KEY);
    BOOST_CHECK_EQUAL(error["message"].get<std::string>(), "Test error message");
}

// ============================================================================
// Address validation tests (using key generation)
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_address_validation)
{
    // Generate a key pair
    CKey key;
    key.MakeNewKey(true); // compressed
    CPubKey pubkey = key.GetPubKey();

    // Create address from public key
    CKeyID keyID = pubkey.GetID();
    CBitcoinAddress addr(keyID);

    // Verify the address is valid
    BOOST_CHECK(addr.IsValid());

    // Verify we can get the key ID back
    CKeyID recoveredKeyID;
    BOOST_CHECK(addr.GetKeyID(recoveredKeyID));
    BOOST_CHECK(keyID == recoveredKeyID);

    // Test address string format
    std::string addrStr = addr.ToString();
    BOOST_CHECK(!addrStr.empty());
    // Pinkcoin mainnet addresses start with '2'
    BOOST_CHECK_EQUAL(addrStr[0], '2');

    // Parse address back
    CBitcoinAddress parsedAddr(addrStr);
    BOOST_CHECK(parsedAddr.IsValid());
    BOOST_CHECK_EQUAL(parsedAddr.ToString(), addrStr);
}

BOOST_AUTO_TEST_CASE(rpc_invalid_addresses)
{
    // Empty address
    CBitcoinAddress emptyAddr("");
    BOOST_CHECK(!emptyAddr.IsValid());

    // Invalid characters
    CBitcoinAddress badChars("2INVALID0OIl");
    BOOST_CHECK(!badChars.IsValid());

    // Too short
    CBitcoinAddress tooShort("2abc");
    BOOST_CHECK(!tooShort.IsValid());

    // Wrong prefix (Bitcoin address starting with '1')
    CBitcoinAddress bitcoinAddr("1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa");
    BOOST_CHECK(!bitcoinAddr.IsValid());
}

// ============================================================================
// Script address (P2SH) tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_script_address)
{
    // Create a simple script
    CScript script;
    script << OP_1 << OP_1 << OP_ADD << OP_2 << OP_EQUAL;

    // Get script ID using CScript::GetID()
    CScriptID scriptID = script.GetID();

    // Create P2SH address
    CBitcoinAddress addr(scriptID);
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsScript());

    // Pinkcoin P2SH addresses start with 'C'
    std::string addrStr = addr.ToString();
    BOOST_CHECK_EQUAL(addrStr[0], 'C');

    // Parse back
    CBitcoinAddress parsedAddr(addrStr);
    BOOST_CHECK(parsedAddr.IsValid());
    BOOST_CHECK(parsedAddr.IsScript());
}

// ============================================================================
// Message signing tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_sign_verify_message)
{
    // Generate a key
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    CKeyID keyID = pubkey.GetID();
    CBitcoinAddress addr(keyID);

    // Message to sign
    std::string message = "Hello, Pinkcoin!";

    // Create the message hash (Bitcoin signed message format)
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message;
    uint256 hash = ss.GetHash();

    // Sign the message
    std::vector<unsigned char> vchSig;
    BOOST_CHECK(key.SignCompact(hash, vchSig));
    BOOST_CHECK(!vchSig.empty());

    // Verify the signature by recovering the public key
    CKey recoveredKey;
    BOOST_CHECK(recoveredKey.SetCompactSignature(hash, vchSig));
    BOOST_CHECK(recoveredKey.GetPubKey().GetID() == keyID);

    // Verify with wrong message recovers a different key
    CHashWriter ssWrong(SER_GETHASH, 0);
    ssWrong << strMessageMagic;
    ssWrong << std::string("Wrong message");
    uint256 wrongHash = ssWrong.GetHash();

    CKey wrongRecoveredKey;
    BOOST_CHECK(wrongRecoveredKey.SetCompactSignature(wrongHash, vchSig));
    // Should recover a different key
    BOOST_CHECK(wrongRecoveredKey.GetPubKey().GetID() != keyID);
}

// ============================================================================
// RPC error codes tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_error_codes)
{
    // Verify error code values match expected JSON-RPC standards
    BOOST_CHECK_EQUAL(RPC_INVALID_REQUEST, -32600);
    BOOST_CHECK_EQUAL(RPC_METHOD_NOT_FOUND, -32601);
    BOOST_CHECK_EQUAL(RPC_INVALID_PARAMS, -32602);
    BOOST_CHECK_EQUAL(RPC_INTERNAL_ERROR, -32603);
    BOOST_CHECK_EQUAL(RPC_PARSE_ERROR, -32700);

    // Wallet-specific errors
    BOOST_CHECK_EQUAL(RPC_WALLET_ERROR, -4);
    BOOST_CHECK_EQUAL(RPC_WALLET_INSUFFICIENT_FUNDS, -6);
    BOOST_CHECK_EQUAL(RPC_WALLET_UNLOCK_NEEDED, -13);
}

// ============================================================================
// Precision tests for amount conversion
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_amount_precision)
{
    // Test that conversion doesn't lose precision for small amounts
    for (int64_t i = 1; i <= 100; i++) {
        double dAmount = static_cast<double>(i) / static_cast<double>(COIN);
        json v(dAmount);
        // Note: AmountFromValue rejects zero, so skip that
        if (dAmount > 0) {
            int64_t recovered = AmountFromValue(v);
            // Allow 1 satoshi rounding error due to floating point
            BOOST_CHECK(abs(recovered - i) <= 1);
        }
    }

    // Test roundtrip for typical transaction amounts
    std::vector<int64_t> testAmounts = {
        1,                  // 1 satoshi
        COIN / 100,         // 0.01 PINK
        COIN / 10,          // 0.1 PINK
        COIN,               // 1 PINK
        10 * COIN,          // 10 PINK
        100 * COIN,         // 100 PINK
        1000 * COIN,        // 1000 PINK
        123456789,          // Random amount in satoshis
    };

    for (int64_t amount : testAmounts) {
        json v = ValueFromAmount(amount);
        // For roundtrip, we go amount -> double -> amount
        // This tests ValueFromAmount precision
        double d = v.get<double>();
        int64_t recovered = roundint64(d * COIN);
        BOOST_CHECK_EQUAL(recovered, amount);
    }
}

BOOST_AUTO_TEST_SUITE_END()
