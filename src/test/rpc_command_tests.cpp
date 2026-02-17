// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "bitcoinrpc.h"
#include "base58.h"
#include "key.h"
#include "main.h"
#include "script.h"
#include "util.h"
#include "test_framework.h"

using namespace std;
using namespace json_spirit;

// Extern declarations for functions in RPC source files not declared in headers
extern string AccountFromValue(const Value& value);
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, Object& out, bool fIncludeHex);
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, Object& entry);

BOOST_AUTO_TEST_SUITE(rpc_command_tests)

// ============================================================================
// verifymessage tests
// ============================================================================

BOOST_AUTO_TEST_CASE(verifymessage_valid_signature)
{
    // Generate key, sign, then verify via the RPC handler
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    CBitcoinAddress addr(pubkey.GetID());

    string message = "test message for verification";

    // Sign (same logic as signmessage RPC)
    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message;
    vector<unsigned char> vchSig;
    BOOST_REQUIRE(key.SignCompact(Hash(ss.begin(), ss.end()), vchSig));
    string sig = EncodeBase64(&vchSig[0], vchSig.size());

    // Call verifymessage RPC
    Array params;
    params.push_back(addr.ToString());
    params.push_back(sig);
    params.push_back(message);
    Value result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get_bool(), true);
}

BOOST_AUTO_TEST_CASE(verifymessage_wrong_message)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    CBitcoinAddress addr(pubkey.GetID());

    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << string("original message");
    vector<unsigned char> vchSig;
    BOOST_REQUIRE(key.SignCompact(Hash(ss.begin(), ss.end()), vchSig));
    string sig = EncodeBase64(&vchSig[0], vchSig.size());

    // Verify with different message → false
    Array params;
    params.push_back(addr.ToString());
    params.push_back(sig);
    params.push_back(string("wrong message"));
    Value result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get_bool(), false);
}

BOOST_AUTO_TEST_CASE(verifymessage_invalid_address)
{
    Array params;
    params.push_back(string("invalid_address"));
    params.push_back(string("dummysig"));
    params.push_back(string("message"));
    BOOST_CHECK_THROW(verifymessage(params, false), Object);
}

BOOST_AUTO_TEST_CASE(verifymessage_malformed_base64)
{
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    // "A" is 1 base64 char (4n+1): DecodeBase64 sets fInvalid=true
    Array params;
    params.push_back(addr.ToString());
    params.push_back(string("A"));
    params.push_back(string("message"));
    BOOST_CHECK_THROW(verifymessage(params, false), Object);
}

BOOST_AUTO_TEST_CASE(verifymessage_invalid_sig_returns_false)
{
    // Non-base64 chars at start: DecodeBase64 returns empty, SetCompactSignature fails → false
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    Array params;
    params.push_back(addr.ToString());
    params.push_back(string("!!!not-base64!!!"));
    params.push_back(string("message"));
    Value result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get_bool(), false);
}

BOOST_AUTO_TEST_CASE(verifymessage_help)
{
    Array params;
    BOOST_CHECK_THROW(verifymessage(params, true), runtime_error);
}

// ============================================================================
// decodescript tests
// ============================================================================

BOOST_AUTO_TEST_CASE(decodescript_p2pkh)
{
    // Build a P2PKH script: OP_DUP OP_HASH160 <20-byte hash> OP_EQUALVERIFY OP_CHECKSIG
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyID = key.GetPubKey().GetID();
    CScript script;
    script.SetDestination(keyID);
    string hex = HexStr(script.begin(), script.end());

    Array params;
    params.push_back(hex);
    Value result = decodescript(params, false);
    Object obj = result.get_obj();

    string asm_str = find_value(obj, "asm").get_str();
    BOOST_CHECK(asm_str.find("OP_DUP") != string::npos);
    BOOST_CHECK(asm_str.find("OP_HASH160") != string::npos);
    BOOST_CHECK(asm_str.find("OP_CHECKSIG") != string::npos);

    string type = find_value(obj, "type").get_str();
    BOOST_CHECK_EQUAL(type, "pubkeyhash");

    // p2sh field contains valid Pinkcoin address with "C" prefix
    string p2sh = find_value(obj, "p2sh").get_str();
    BOOST_CHECK_EQUAL(p2sh[0], 'C');
    CBitcoinAddress p2shAddr(p2sh);
    BOOST_CHECK(p2shAddr.IsValid());
}

BOOST_AUTO_TEST_CASE(decodescript_p2sh)
{
    // Build a P2SH-style script: OP_HASH160 <20-byte hash> OP_EQUAL
    CKey key;
    key.MakeNewKey(true);
    CScript innerScript;
    innerScript << OP_1 << key.GetPubKey().Raw() << OP_1 << OP_CHECKMULTISIG;
    CScriptID scriptID = innerScript.GetID();
    CScript script;
    script.SetDestination(scriptID);
    string hex = HexStr(script.begin(), script.end());

    Array params;
    params.push_back(hex);
    Value result = decodescript(params, false);
    Object obj = result.get_obj();

    string type = find_value(obj, "type").get_str();
    BOOST_CHECK_EQUAL(type, "scripthash");
}

BOOST_AUTO_TEST_CASE(decodescript_empty)
{
    Array params;
    params.push_back(string(""));
    Value result = decodescript(params, false);
    Object obj = result.get_obj();

    // Empty script has empty asm
    string asm_str = find_value(obj, "asm").get_str();
    BOOST_CHECK(asm_str.empty());

    // p2sh field still present
    string p2sh = find_value(obj, "p2sh").get_str();
    BOOST_CHECK(!p2sh.empty());
}

BOOST_AUTO_TEST_CASE(decodescript_p2sh_address_prefix)
{
    // Any decoded script's p2sh should start with "C" (Pinkcoin P2SH prefix)
    CScript script;
    script << OP_1;
    string hex = HexStr(script.begin(), script.end());

    Array params;
    params.push_back(hex);
    Value result = decodescript(params, false);
    Object obj = result.get_obj();
    string p2sh = find_value(obj, "p2sh").get_str();
    BOOST_CHECK_EQUAL(p2sh[0], 'C');
}

// ============================================================================
// decoderawtransaction tests
// ============================================================================

BOOST_AUTO_TEST_CASE(decoderawtransaction_valid)
{
    // Construct a CTransaction, serialize, then decode
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    tx.nLockTime = 0;
    tx.vin.push_back(CTxIn(COutPoint(uint256("0000000000000000000000000000000000000000000000000000000000000001"), 0)));
    CScript scriptPubKey;
    CKey key;
    key.MakeNewKey(true);
    scriptPubKey.SetDestination(key.GetPubKey().GetID());
    tx.vout.push_back(CTxOut(COIN, scriptPubKey));

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;
    string hexTx = HexStr(ss.begin(), ss.end());

    Array params;
    params.push_back(hexTx);
    Value result = decoderawtransaction(params, false);
    Object obj = result.get_obj();

    // Verify Pinkcoin-specific "time" field
    BOOST_CHECK_EQUAL(find_value(obj, "time").get_int64(), 1700000000);
    BOOST_CHECK_EQUAL(find_value(obj, "version").get_int(), 1);
    BOOST_CHECK_EQUAL(find_value(obj, "locktime").get_int64(), 0);

    // txid should be non-empty hex
    string txid = find_value(obj, "txid").get_str();
    BOOST_CHECK(!txid.empty());
    BOOST_CHECK(IsHex(txid));

    // vin and vout arrays
    Array vin = find_value(obj, "vin").get_array();
    BOOST_CHECK_EQUAL(vin.size(), 1u);
    Array vout = find_value(obj, "vout").get_array();
    BOOST_CHECK_EQUAL(vout.size(), 1u);
}

BOOST_AUTO_TEST_CASE(decoderawtransaction_invalid_hex)
{
    Array params;
    params.push_back(string("not_valid_hex_data_zzzz"));
    BOOST_CHECK_THROW(decoderawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(decoderawtransaction_truncated)
{
    // Valid hex but not a valid serialized transaction
    Array params;
    params.push_back(string("deadbeef"));
    BOOST_CHECK_THROW(decoderawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(decoderawtransaction_pinkcoin_time_field)
{
    // Pinkcoin transactions have a "time" field unlike Bitcoin
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1234567890;
    // Add a dummy input and output
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(0, CScript()));

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << tx;
    string hexTx = HexStr(ss.begin(), ss.end());

    Array params;
    params.push_back(hexTx);
    Value result = decoderawtransaction(params, false);
    Object obj = result.get_obj();

    // The "time" field must be present and correct
    BOOST_CHECK_EQUAL(find_value(obj, "time").get_int64(), 1234567890);
}

// ============================================================================
// createrawtransaction tests
// ============================================================================

BOOST_AUTO_TEST_CASE(createrawtransaction_valid)
{
    // Create a valid Pinkcoin address
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    // Build inputs array
    Object input;
    input.push_back(Pair("txid", string("0000000000000000000000000000000000000000000000000000000000000001")));
    input.push_back(Pair("vout", 0));
    Array inputs;
    inputs.push_back(input);

    // Build outputs object
    Object outputs;
    outputs.push_back(Pair(addr.ToString(), 1.0));

    Array params;
    params.push_back(inputs);
    params.push_back(outputs);
    Value result = createrawtransaction(params, false);

    // Result is a hex string
    string hex = result.get_str();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));

    // Round-trip: decode the result
    Array decodeParams;
    decodeParams.push_back(hex);
    Value decoded = decoderawtransaction(decodeParams, false);
    Object obj = decoded.get_obj();
    Array vin = find_value(obj, "vin").get_array();
    BOOST_CHECK_EQUAL(vin.size(), 1u);
    Array vout = find_value(obj, "vout").get_array();
    BOOST_CHECK_EQUAL(vout.size(), 1u);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_invalid_address)
{
    Object input;
    input.push_back(Pair("txid", string("0000000000000000000000000000000000000000000000000000000000000001")));
    input.push_back(Pair("vout", 0));
    Array inputs;
    inputs.push_back(input);

    Object outputs;
    outputs.push_back(Pair("invalid_address", 1.0));

    Array params;
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_duplicate_address)
{
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    Object input;
    input.push_back(Pair("txid", string("0000000000000000000000000000000000000000000000000000000000000001")));
    input.push_back(Pair("vout", 0));
    Array inputs;
    inputs.push_back(input);

    Object outputs;
    outputs.push_back(Pair(addr.ToString(), 1.0));
    outputs.push_back(Pair(addr.ToString(), 2.0));

    Array params;
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_missing_txid)
{
    Object input;
    input.push_back(Pair("vout", 0));
    Array inputs;
    inputs.push_back(input);

    CKey key;
    key.MakeNewKey(true);
    Object outputs;
    outputs.push_back(Pair(CBitcoinAddress(key.GetPubKey().GetID()).ToString(), 1.0));

    Array params;
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_negative_vout)
{
    Object input;
    input.push_back(Pair("txid", string("0000000000000000000000000000000000000000000000000000000000000001")));
    input.push_back(Pair("vout", -1));
    Array inputs;
    inputs.push_back(input);

    CKey key;
    key.MakeNewKey(true);
    Object outputs;
    outputs.push_back(Pair(CBitcoinAddress(key.GetPubKey().GetID()).ToString(), 1.0));

    Array params;
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), Object);
}

// ============================================================================
// makekeypair tests
// ============================================================================

BOOST_AUTO_TEST_CASE(makekeypair_returns_keys)
{
    Array params;
    Value result = makekeypair(params, false);
    Object obj = result.get_obj();

    string privKey = find_value(obj, "PrivateKey").get_str();
    string pubKey = find_value(obj, "PublicKey").get_str();

    BOOST_CHECK(!privKey.empty());
    BOOST_CHECK(!pubKey.empty());
    BOOST_CHECK(IsHex(privKey));
    BOOST_CHECK(IsHex(pubKey));
}

BOOST_AUTO_TEST_CASE(makekeypair_valid_pubkey)
{
    Array params;
    Value result = makekeypair(params, false);
    Object obj = result.get_obj();

    string pubKeyHex = find_value(obj, "PublicKey").get_str();
    vector<unsigned char> pubKeyBytes = ParseHex(pubKeyHex);

    // Uncompressed public key: 65 bytes, prefix 0x04
    BOOST_CHECK_EQUAL(pubKeyBytes.size(), 65u);
    BOOST_CHECK_EQUAL(pubKeyBytes[0], 0x04);
}

BOOST_AUTO_TEST_CASE(makekeypair_unique_keys)
{
    Array params;
    Value result1 = makekeypair(params, false);
    Value result2 = makekeypair(params, false);

    string pub1 = find_value(result1.get_obj(), "PublicKey").get_str();
    string pub2 = find_value(result2.get_obj(), "PublicKey").get_str();
    BOOST_CHECK(pub1 != pub2);
}

// ============================================================================
// AccountFromValue tests
// ============================================================================

BOOST_AUTO_TEST_CASE(accountfromvalue_valid)
{
    BOOST_CHECK_EQUAL(AccountFromValue(Value("myaccount")), "myaccount");
}

BOOST_AUTO_TEST_CASE(accountfromvalue_wildcard_throws)
{
    BOOST_CHECK_THROW(AccountFromValue(Value("*")), Object);
}

BOOST_AUTO_TEST_CASE(accountfromvalue_empty_is_default)
{
    // Empty string is the default account — valid
    BOOST_CHECK_EQUAL(AccountFromValue(Value("")), "");
}

// ============================================================================
// ScriptPubKeyToJSON tests
// ============================================================================

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_p2pkh)
{
    CKey key;
    key.MakeNewKey(true);
    CKeyID keyID = key.GetPubKey().GetID();
    CScript script;
    script.SetDestination(keyID);

    Object out;
    ScriptPubKeyToJSON(script, out, false);

    string type = find_value(out, "type").get_str();
    BOOST_CHECK_EQUAL(type, "pubkeyhash");
    BOOST_CHECK_EQUAL(find_value(out, "reqSigs").get_int(), 1);

    Array addrs = find_value(out, "addresses").get_array();
    BOOST_CHECK_EQUAL(addrs.size(), 1u);
    string addr = addrs[0].get_str();
    BOOST_CHECK_EQUAL(addr[0], '2'); // Pinkcoin P2PKH prefix
}

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_p2sh)
{
    CKey key;
    key.MakeNewKey(true);
    CScript innerScript;
    innerScript << OP_1 << key.GetPubKey().Raw() << OP_1 << OP_CHECKMULTISIG;
    CScriptID scriptID = innerScript.GetID();
    CScript script;
    script.SetDestination(scriptID);

    Object out;
    ScriptPubKeyToJSON(script, out, false);

    string type = find_value(out, "type").get_str();
    BOOST_CHECK_EQUAL(type, "scripthash");

    Array addrs = find_value(out, "addresses").get_array();
    BOOST_CHECK_EQUAL(addrs.size(), 1u);
    string addr = addrs[0].get_str();
    BOOST_CHECK_EQUAL(addr[0], 'C'); // Pinkcoin P2SH prefix
}

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_op_return)
{
    CScript script;
    script << OP_RETURN << ParseHex("deadbeef");

    Object out;
    ScriptPubKeyToJSON(script, out, false);

    string type = find_value(out, "type").get_str();
    BOOST_CHECK_EQUAL(type, "nulldata");

    // OP_RETURN scripts have no addresses
    BOOST_CHECK(find_value(out, "addresses").type() == null_type);
}

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_include_hex)
{
    CKey key;
    key.MakeNewKey(true);
    CScript script;
    script.SetDestination(key.GetPubKey().GetID());

    Object out;
    ScriptPubKeyToJSON(script, out, true);

    // With fIncludeHex=true, "hex" field must be present
    string hex = find_value(out, "hex").get_str();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));

    // asm field is always present
    BOOST_CHECK(!find_value(out, "asm").get_str().empty());
}

// ============================================================================
// TxToJSON tests
// ============================================================================

BOOST_AUTO_TEST_CASE(txtojson_coinbase)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    // Coinbase: single input with null prevout
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(50 * COIN, CScript()));

    Object entry;
    TxToJSON(tx, 0, entry);

    // Coinbase vin has "coinbase" key
    Array vin = find_value(entry, "vin").get_array();
    BOOST_CHECK_EQUAL(vin.size(), 1u);
    Object vinObj = vin[0].get_obj();
    BOOST_CHECK(find_value(vinObj, "coinbase").type() != null_type);
}

BOOST_AUTO_TEST_CASE(txtojson_regular_tx)
{
    CTransaction tx;
    tx.nVersion = 1;
    tx.nTime = 1700000000;
    // Regular tx: non-null prevout, two inputs
    tx.vin.push_back(CTxIn(COutPoint(uint256("0000000000000000000000000000000000000000000000000000000000000001"), 0)));
    tx.vin.push_back(CTxIn(COutPoint(uint256("0000000000000000000000000000000000000000000000000000000000000002"), 1)));
    tx.vout.push_back(CTxOut(COIN, CScript()));

    Object entry;
    TxToJSON(tx, 0, entry);

    // Regular vin has "txid"/"vout"/"scriptSig" keys
    Array vin = find_value(entry, "vin").get_array();
    BOOST_CHECK_EQUAL(vin.size(), 2u);
    Object vinObj = vin[0].get_obj();
    BOOST_CHECK(find_value(vinObj, "txid").type() == str_type);
    BOOST_CHECK(find_value(vinObj, "vout").type() == int_type);
    BOOST_CHECK(find_value(vinObj, "scriptSig").type() == obj_type);
}

BOOST_AUTO_TEST_CASE(txtojson_pinkcoin_fields)
{
    CTransaction tx;
    tx.nVersion = 2;
    tx.nTime = 1234567890;
    tx.nLockTime = 500000;
    tx.vin.push_back(CTxIn(COutPoint(uint256("0000000000000000000000000000000000000000000000000000000000000001"), 0)));
    tx.vout.push_back(CTxOut(COIN, CScript()));

    Object entry;
    TxToJSON(tx, 0, entry);

    // All Pinkcoin-specific fields present
    BOOST_CHECK(find_value(entry, "txid").type() == str_type);
    BOOST_CHECK_EQUAL(find_value(entry, "version").get_int(), 2);
    BOOST_CHECK_EQUAL(find_value(entry, "time").get_int64(), 1234567890);
    BOOST_CHECK_EQUAL(find_value(entry, "locktime").get_int64(), 500000);
    BOOST_CHECK(find_value(entry, "vin").type() == array_type);
    BOOST_CHECK(find_value(entry, "vout").type() == array_type);
}

BOOST_AUTO_TEST_CASE(txtojson_no_blockhash_when_zero)
{
    CTransaction tx;
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(0, CScript()));

    Object entry;
    TxToJSON(tx, 0, entry);

    // hashBlock=0 → no blockhash/confirmations fields
    BOOST_CHECK(find_value(entry, "blockhash").type() == null_type);
    BOOST_CHECK(find_value(entry, "confirmations").type() == null_type);
}

// ============================================================================
// GetDifficulty tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getdifficulty_null_returns_one)
{
    // With nullptr and pindexBest=nullptr → returns 1.0
    // Save and restore pindexBest
    CBlockIndex* savedBest = pindexBest;
    pindexBest = nullptr;

    double diff = GetDifficulty(nullptr);
    BOOST_CHECK_EQUAL(diff, 1.0);

    pindexBest = savedBest;
}

BOOST_AUTO_TEST_CASE(getdifficulty_genesis_nbits)
{
    // Construct a mock CBlockIndex with genesis nBits
    CBlockIndex mockIndex;
    mockIndex.nBits = 0x1e0fffff; // Pinkcoin bnProofOfWorkLimit
    double diff = GetDifficulty(&mockIndex);
    // nShift = 0x1e = 30, mantissa = 0x0fffff
    // dDiff = 0x0000ffff / 0x0fffff = ~0.0625
    // nShift=30 > 29, so dDiff /= 256 → ~0.000244
    // This should be a small positive number
    BOOST_CHECK(diff > 0.0);
    BOOST_CHECK(diff < 1.0);
}

BOOST_AUTO_TEST_CASE(getdifficulty_minimum_nbits)
{
    // nBits = 0x1d00ffff is the Bitcoin-style minimum difficulty
    CBlockIndex mockIndex;
    mockIndex.nBits = 0x1d00ffff;
    double diff = GetDifficulty(&mockIndex);
    // nShift = 0x1d = 29, mantissa = 0x00ffff
    // dDiff = 0x0000ffff / 0x00ffff = 1.0
    // nShift == 29, no loop → diff = 1.0
    BOOST_CHECK_CLOSE(diff, 1.0, 0.001);
}

// ============================================================================
// getnewaddress / getnewpubkey tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getnewaddress_default)
{
    Array params;
    Value result = getnewaddress(params, false);
    string addr = result.get_str();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');  // Pinkcoin P2PKH prefix
    CBitcoinAddress address(addr);
    BOOST_CHECK(address.IsValid());
}

BOOST_AUTO_TEST_CASE(getnewaddress_with_account)
{
    Array params;
    params.push_back(string("testaccount"));
    Value result = getnewaddress(params, false);
    string addr = result.get_str();
    BOOST_CHECK(!addr.empty());
    CBitcoinAddress address(addr);
    BOOST_CHECK(address.IsValid());
}

BOOST_AUTO_TEST_CASE(getnewpubkey_returns_hex)
{
    Array params;
    Value result = getnewpubkey(params, false);
    string pubkeyHex = result.get_str();
    BOOST_CHECK(!pubkeyHex.empty());
    BOOST_CHECK(IsHex(pubkeyHex));

    vector<unsigned char> vchPubKey = ParseHex(pubkeyHex);
    CPubKey pubkey(vchPubKey);
    BOOST_CHECK(pubkey.IsValid());
}

// ============================================================================
// getaccount / setaccount / getaddressesbyaccount tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getaccount_valid_address)
{
    // Create a new address with an account
    Array newAddr;
    newAddr.push_back(string("test_getaccount"));
    string addr = getnewaddress(newAddr, false).get_str();

    Array params;
    params.push_back(addr);
    Value result = getaccount(params, false);
    BOOST_CHECK_EQUAL(result.get_str(), "test_getaccount");
}

BOOST_AUTO_TEST_CASE(getaccount_invalid_address)
{
    Array params;
    params.push_back(string("invalid_address_here"));
    BOOST_CHECK_THROW(getaccount(params, false), Object);
}

BOOST_AUTO_TEST_CASE(setaccount_assigns_account)
{
    // Get a new address
    Array empty;
    string addr = getnewaddress(empty, false).get_str();

    // Set its account
    Array params;
    params.push_back(addr);
    params.push_back(string("newlabel"));
    setaccount(params, false);

    // Verify
    Array getParams;
    getParams.push_back(addr);
    BOOST_CHECK_EQUAL(getaccount(getParams, false).get_str(), "newlabel");
}

BOOST_AUTO_TEST_CASE(getaddressesbyaccount_populated)
{
    // Create address with specific account
    Array newAddr;
    newAddr.push_back(string("addrbyacct_test"));
    string addr = getnewaddress(newAddr, false).get_str();

    Array params;
    params.push_back(string("addrbyacct_test"));
    Value result = getaddressesbyaccount(params, false);
    Array addrs = result.get_array();
    BOOST_CHECK(!addrs.empty());

    // The address we created should be in the list
    bool found = false;
    for (const Value& v : addrs) {
        if (v.get_str() == addr) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);
}

// ============================================================================
// getbalance tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getbalance_default)
{
    Array params;
    Value result = getbalance(params, false);
    // Balance is a real number (may be 0 in test env)
    BOOST_CHECK(result.type() == real_type);
    BOOST_CHECK(result.get_real() >= 0.0);
}

BOOST_AUTO_TEST_CASE(getbalance_with_star)
{
    // "*" returns total balance across all accounts
    Array params;
    params.push_back(string("*"));
    Value result = getbalance(params, false);
    BOOST_CHECK(result.type() == real_type);
    BOOST_CHECK(result.get_real() >= 0.0);
}

// ============================================================================
// validateaddress / validatepubkey tests
// ============================================================================

BOOST_AUTO_TEST_CASE(validateaddress_valid)
{
    // Generate a known address
    Array empty;
    string addr = getnewaddress(empty, false).get_str();

    Array params;
    params.push_back(addr);
    Value result = validateaddress(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), true);
    BOOST_CHECK(find_value(obj, "address").type() == str_type);
    BOOST_CHECK(find_value(obj, "ismine").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "ismine").get_bool(), true);
}

BOOST_AUTO_TEST_CASE(validateaddress_invalid)
{
    Array params;
    params.push_back(string("not_a_valid_address"));
    Value result = validateaddress(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), false);
}

BOOST_AUTO_TEST_CASE(validatepubkey_valid)
{
    // Get a pubkey from the wallet
    Array empty;
    string pubkeyHex = getnewpubkey(empty, false).get_str();

    Array params;
    params.push_back(pubkeyHex);
    Value result = validatepubkey(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), true);
    BOOST_CHECK(find_value(obj, "address").type() == str_type);
    BOOST_CHECK(find_value(obj, "iscompressed").type() == bool_type);
}

BOOST_AUTO_TEST_CASE(validatepubkey_invalid)
{
    Array params;
    params.push_back(string("deadbeef"));
    Value result = validatepubkey(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), false);
}

// ============================================================================
// listaccounts / listreceivedbyaddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(listaccounts_returns_map)
{
    Array params;
    Value result = listaccounts(params, false);
    Object obj = result.get_obj();
    // Should have at least the default "" account
    BOOST_CHECK(obj.size() >= 1);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaddress_default)
{
    Array params;
    Value result = listreceivedbyaddress(params, false);
    BOOST_CHECK(result.type() == array_type);
}

// ============================================================================
// getwalletinfo / getstakesplitthreshold tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getwalletinfo_returns_object)
{
    Array params;
    Value result = getwalletinfo(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "txcount").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);
}

BOOST_AUTO_TEST_CASE(getstakesplitthreshold_returns_object)
{
    Array params;
    Value result = getstakesplitthreshold(params, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "split threshold").type() == real_type);
}

// ============================================================================
// keypoolrefill tests
// ============================================================================

BOOST_AUTO_TEST_CASE(keypoolrefill_default)
{
    Array params;
    // Should not throw — refills keypool
    BOOST_CHECK_NO_THROW(keypoolrefill(params, false));
}

// ============================================================================
// reservebalance tests
// ============================================================================

BOOST_AUTO_TEST_CASE(reservebalance_query)
{
    // No params → returns current reserve setting
    Array params;
    Value result = reservebalance(params, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "reserve").type() == bool_type);
    BOOST_CHECK(find_value(obj, "amount").type() == real_type);
}

BOOST_AUTO_TEST_CASE(reservebalance_set_and_query)
{
    // Set reserve on with 10.0
    Array setParams;
    setParams.push_back(true);
    setParams.push_back(10.0);
    Value result = reservebalance(setParams, false);
    Object obj = result.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "reserve").get_bool(), true);

    // Disable reserve
    Array offParams;
    offParams.push_back(false);
    reservebalance(offParams, false);
}

// ============================================================================
// getreceivedbyaddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getreceivedbyaddress_zero_for_unused)
{
    Array empty;
    string addr = getnewaddress(empty, false).get_str();

    Array params;
    params.push_back(addr);
    Value result = getreceivedbyaddress(params, false);
    BOOST_CHECK_CLOSE(result.get_real(), 0.0, 0.001);
}

// ============================================================================
// rpcnet: getconnectioncount / getpeerinfo
// ============================================================================

BOOST_AUTO_TEST_CASE(getconnectioncount_zero)
{
    Array params;
    Value result = getconnectioncount(params, false);
    // In test mode, no peers connected
    BOOST_CHECK_EQUAL(result.get_int(), 0);
}

BOOST_AUTO_TEST_CASE(getpeerinfo_empty)
{
    Array params;
    Value result = getpeerinfo(params, false);
    Array arr = result.get_array();
    BOOST_CHECK(arr.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Tests requiring a real chain (TestChain fixture)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_chain_command_tests, TestChain)

BOOST_AUTO_TEST_CASE(getrawtransaction_hex)
{
    BOOST_REQUIRE(!coinbaseTxns.empty());
    string txid = coinbaseTxns[0].GetHash().GetHex();

    Array params;
    params.push_back(txid);
    params.push_back(0);  // verbose=0 → hex string
    Value result = getrawtransaction(params, false);
    string hex = result.get_str();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));
}

BOOST_AUTO_TEST_CASE(getrawtransaction_json)
{
    BOOST_REQUIRE(!coinbaseTxns.empty());
    string txid = coinbaseTxns[0].GetHash().GetHex();

    Array params;
    params.push_back(txid);
    params.push_back(1);  // verbose=1 → JSON object
    Value result = getrawtransaction(params, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "txid").type() == str_type);
    BOOST_CHECK(find_value(obj, "version").type() == int_type);
}

BOOST_AUTO_TEST_CASE(getrawtransaction_notfound)
{
    Array params;
    params.push_back(string("0000000000000000000000000000000000000000000000000000000000000bad"));
    params.push_back(0);
    BOOST_CHECK_THROW(getrawtransaction(params, false), Object);
}

BOOST_AUTO_TEST_CASE(listunspent_default)
{
    Array params;
    Value result = listunspent(params, false);
    BOOST_CHECK(result.type() == array_type);
}

BOOST_AUTO_TEST_CASE(listunspent_with_minconf)
{
    Array params;
    params.push_back(1);   // minconf
    params.push_back(999); // maxconf
    Value result = listunspent(params, false);
    BOOST_CHECK(result.type() == array_type);
}

BOOST_AUTO_TEST_CASE(decodescript_multisig_2of3)
{
    // Build a 2-of-3 multisig script
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    key3.MakeNewKey(true);

    CScript script;
    script << OP_2
           << key1.GetPubKey().Raw()
           << key2.GetPubKey().Raw()
           << key3.GetPubKey().Raw()
           << OP_3
           << OP_CHECKMULTISIG;
    string hex = HexStr(script.begin(), script.end());

    Array params;
    params.push_back(hex);
    Value result = decodescript(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "type").get_str(), "multisig");
    BOOST_CHECK_EQUAL(find_value(obj, "reqSigs").get_int(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
