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

using namespace std;
using namespace json_spirit;

BOOST_AUTO_TEST_SUITE(rpc_tests)

// ============================================================================
// ValueFromAmount / AmountFromValue tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_ValueFromAmount)
{
    // Test conversion from satoshis to JSON value
    BOOST_CHECK_EQUAL(ValueFromAmount(0).get_real(), 0.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN).get_real(), 1.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 2).get_real(), 0.5);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10).get_real(), 0.1);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 100).get_real(), 0.01);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 1000).get_real(), 0.001);
    BOOST_CHECK_EQUAL(ValueFromAmount(COIN / 10000).get_real(), 0.0001);

    // Large amounts
    BOOST_CHECK_EQUAL(ValueFromAmount(100 * COIN).get_real(), 100.0);
    BOOST_CHECK_EQUAL(ValueFromAmount(1000000 * COIN).get_real(), 1000000.0);

    // Negative amounts (internal representation)
    BOOST_CHECK_EQUAL(ValueFromAmount(-COIN).get_real(), -1.0);
}

BOOST_AUTO_TEST_CASE(rpc_AmountFromValue)
{
    // Test conversion from JSON value to satoshis
    BOOST_CHECK_EQUAL(AmountFromValue(Value(1.0)), COIN);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.5)), COIN / 2);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.1)), COIN / 10);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.01)), COIN / 100);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.001)), COIN / 1000);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.0001)), COIN / 10000);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(0.00000001)), 1); // 1 satoshi

    // Larger amounts
    BOOST_CHECK_EQUAL(AmountFromValue(Value(100.0)), 100 * COIN);
    BOOST_CHECK_EQUAL(AmountFromValue(Value(1000.0)), 1000 * COIN);

    // Invalid amounts should throw (throws json_spirit::Object)
    BOOST_CHECK_THROW(AmountFromValue(Value(0.0)), Object);
    BOOST_CHECK_THROW(AmountFromValue(Value(-1.0)), Object);
}

// ============================================================================
// ParseHashV / ParseHexV tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_ParseHashV)
{
    // Valid 64-character hex string (256-bit hash)
    string validHash = "0000000000000000000000000000000000000000000000000000000000000001";
    Value v(validHash);
    uint256 result = ParseHashV(v, "testhash");
    BOOST_CHECK_EQUAL(result.GetHex(), validHash);

    // All zeros
    string zeroHash = "0000000000000000000000000000000000000000000000000000000000000000";
    result = ParseHashV(Value(zeroHash), "zerohash");
    BOOST_CHECK(result == 0);

    // Invalid: non-hex characters (throws json_spirit::Object)
    BOOST_CHECK_THROW(ParseHashV(Value("not_a_hex_string"), "badhash"), Object);

    // Invalid: empty string
    BOOST_CHECK_THROW(ParseHashV(Value(""), "emptyhash"), Object);

    // Invalid: wrong type (not a string)
    BOOST_CHECK_THROW(ParseHashV(Value(12345), "wrongtype"), Object);
}

BOOST_AUTO_TEST_CASE(rpc_ParseHexV)
{
    // Valid hex string
    Value v("deadbeef");
    vector<unsigned char> result = ParseHexV(v, "testhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);
    BOOST_CHECK_EQUAL(result[0], 0xde);
    BOOST_CHECK_EQUAL(result[1], 0xad);
    BOOST_CHECK_EQUAL(result[2], 0xbe);
    BOOST_CHECK_EQUAL(result[3], 0xef);

    // Valid: uppercase hex
    result = ParseHexV(Value("DEADBEEF"), "upperhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);

    // Valid: mixed case
    result = ParseHexV(Value("DeAdBeEf"), "mixedhex");
    BOOST_CHECK_EQUAL(result.size(), 4u);

    // Invalid: non-hex (throws json_spirit::Object)
    BOOST_CHECK_THROW(ParseHexV(Value("xyz"), "badhex"), Object);

    // Invalid: empty
    BOOST_CHECK_THROW(ParseHexV(Value(""), "emptyhex"), Object);

    // Odd length hex is invalid (IsHex rejects odd-length strings)
    BOOST_CHECK_THROW(ParseHexV(Value("abc"), "oddhex"), Object);
}

// ============================================================================
// RPCTypeCheck tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_array)
{
    // Test array type checking
    Array params;
    params.push_back("string_value");
    params.push_back(123);
    params.push_back(45.67);
    params.push_back(true);

    // Valid types - should not throw
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, {str_type, int_type, real_type, bool_type}));

    // Valid with fewer expected types
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, {str_type, int_type}));

    // Invalid: wrong type at position 0 (throws json_spirit::Object)
    BOOST_CHECK_THROW(RPCTypeCheck(params, {int_type}), Object);

    // Invalid: wrong type at position 1
    BOOST_CHECK_THROW(RPCTypeCheck(params, {str_type, str_type}), Object);
}

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_array_with_null)
{
    Array params;
    params.push_back(Value::null);
    params.push_back("test");

    // Without fAllowNull, null doesn't match str_type (throws json_spirit::Object)
    BOOST_CHECK_THROW(RPCTypeCheck(params, {str_type, str_type}, false), Object);

    // With fAllowNull, null is acceptable
    BOOST_CHECK_NO_THROW(RPCTypeCheck(params, {str_type, str_type}, true));
}

BOOST_AUTO_TEST_CASE(rpc_TypeCheck_object)
{
    Object obj;
    obj.push_back(Pair("name", "test"));
    obj.push_back(Pair("count", 42));
    obj.push_back(Pair("enabled", true));

    // Valid types
    map<string, Value_type> expected;
    expected["name"] = str_type;
    expected["count"] = int_type;
    expected["enabled"] = bool_type;
    BOOST_CHECK_NO_THROW(RPCTypeCheck(obj, expected));

    // Check subset of fields
    map<string, Value_type> subset;
    subset["name"] = str_type;
    BOOST_CHECK_NO_THROW(RPCTypeCheck(obj, subset));

    // Wrong type for existing field (throws json_spirit::Object)
    map<string, Value_type> wrongType;
    wrongType["name"] = int_type;
    BOOST_CHECK_THROW(RPCTypeCheck(obj, wrongType), Object);

    // Missing required field (without fAllowNull)
    map<string, Value_type> missing;
    missing["nonexistent"] = str_type;
    BOOST_CHECK_THROW(RPCTypeCheck(obj, missing, false), Object);

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
    string result = HexBits(0x1d00ffff); // Genesis block difficulty
    BOOST_CHECK_EQUAL(result.size(), 8u); // 4 bytes = 8 hex chars

    // Verify it's valid hex
    BOOST_CHECK(IsHex(result));
}

// ============================================================================
// JSONRPCError tests
// ============================================================================

BOOST_AUTO_TEST_CASE(rpc_JSONRPCError)
{
    Object error = JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Test error message");

    // Check structure
    BOOST_CHECK_EQUAL(find_value(error, "code").get_int(), RPC_INVALID_ADDRESS_OR_KEY);
    BOOST_CHECK_EQUAL(find_value(error, "message").get_str(), "Test error message");
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
    string addrStr = addr.ToString();
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
    string addrStr = addr.ToString();
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
    string message = "Hello, Pinkcoin!";

    // Create the message hash (Bitcoin signed message format)
    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message;
    uint256 hash = ss.GetHash();

    // Sign the message
    vector<unsigned char> vchSig;
    BOOST_CHECK(key.SignCompact(hash, vchSig));
    BOOST_CHECK(!vchSig.empty());

    // Verify the signature by recovering the public key
    CKey recoveredKey;
    BOOST_CHECK(recoveredKey.SetCompactSignature(hash, vchSig));
    BOOST_CHECK(recoveredKey.GetPubKey().GetID() == keyID);

    // Verify with wrong message recovers a different key
    CHashWriter ssWrong(SER_GETHASH, 0);
    ssWrong << strMessageMagic;
    ssWrong << string("Wrong message");
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
        double dAmount = (double)i / (double)COIN;
        Value v(dAmount);
        // Note: AmountFromValue rejects zero, so skip that
        if (dAmount > 0) {
            int64_t recovered = AmountFromValue(v);
            // Allow 1 satoshi rounding error due to floating point
            BOOST_CHECK(abs(recovered - i) <= 1);
        }
    }

    // Test roundtrip for typical transaction amounts
    vector<int64_t> testAmounts = {
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
        Value v = ValueFromAmount(amount);
        // For roundtrip, we go amount -> double -> amount
        // This tests ValueFromAmount precision
        double d = v.get_real();
        int64_t recovered = roundint64(d * COIN);
        BOOST_CHECK_EQUAL(recovered, amount);
    }
}

BOOST_AUTO_TEST_SUITE_END()
