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

// Extern declarations for functions in RPC source files not declared in headers
extern std::string AccountFromValue(const json& value);
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, json& out, bool fIncludeHex);
extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, json& entry);

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

    std::string message = "test message for verification";

    // Sign (same logic as signmessage RPC)
    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << message;
    std::vector<unsigned char> vchSig;
    BOOST_REQUIRE(key.SignCompact(Hash(ss.begin(), ss.end()), vchSig));
    std::string sig = EncodeBase64(&vchSig[0], vchSig.size());

    // Call verifymessage RPC
    json params = json::array();
    params.push_back(addr.ToString());
    params.push_back(sig);
    params.push_back(message);
    json result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get<bool>(), true);
}

BOOST_AUTO_TEST_CASE(verifymessage_wrong_message)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    CBitcoinAddress addr(pubkey.GetID());

    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << std::string("original message");
    std::vector<unsigned char> vchSig;
    BOOST_REQUIRE(key.SignCompact(Hash(ss.begin(), ss.end()), vchSig));
    std::string sig = EncodeBase64(&vchSig[0], vchSig.size());

    // Verify with different message -> false
    json params = json::array();
    params.push_back(addr.ToString());
    params.push_back(sig);
    params.push_back(std::string("wrong message"));
    json result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get<bool>(), false);
}

BOOST_AUTO_TEST_CASE(verifymessage_invalid_address)
{
    json params = json::array();
    params.push_back(std::string("invalid_address"));
    params.push_back(std::string("dummysig"));
    params.push_back(std::string("message"));
    BOOST_CHECK_THROW(verifymessage(params, false), json);
}

BOOST_AUTO_TEST_CASE(verifymessage_malformed_base64)
{
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    // "A" is 1 base64 char (4n+1): DecodeBase64 sets fInvalid=true
    json params = json::array();
    params.push_back(addr.ToString());
    params.push_back(std::string("A"));
    params.push_back(std::string("message"));
    BOOST_CHECK_THROW(verifymessage(params, false), json);
}

BOOST_AUTO_TEST_CASE(verifymessage_invalid_sig_returns_false)
{
    // Non-base64 chars at start: DecodeBase64 returns empty, SetCompactSignature fails -> false
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    json params = json::array();
    params.push_back(addr.ToString());
    params.push_back(std::string("!!!not-base64!!!"));
    params.push_back(std::string("message"));
    json result = verifymessage(params, false);
    BOOST_CHECK_EQUAL(result.get<bool>(), false);
}

BOOST_AUTO_TEST_CASE(verifymessage_help)
{
    json params = json::array();
    BOOST_CHECK_THROW(verifymessage(params, true), std::runtime_error);
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
    std::string hex = HexStr(script.begin(), script.end());

    json params = json::array();
    params.push_back(hex);
    json result = decodescript(params, false);

    std::string asm_str = result["asm"].get<std::string>();
    BOOST_CHECK(asm_str.find("OP_DUP") != std::string::npos);
    BOOST_CHECK(asm_str.find("OP_HASH160") != std::string::npos);
    BOOST_CHECK(asm_str.find("OP_CHECKSIG") != std::string::npos);

    std::string type = result["type"].get<std::string>();
    BOOST_CHECK_EQUAL(type, "pubkeyhash");

    // p2sh field contains valid Pinkcoin address with "C" prefix
    std::string p2sh = result["p2sh"].get<std::string>();
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
    std::string hex = HexStr(script.begin(), script.end());

    json params = json::array();
    params.push_back(hex);
    json result = decodescript(params, false);

    std::string type = result["type"].get<std::string>();
    BOOST_CHECK_EQUAL(type, "scripthash");
}

BOOST_AUTO_TEST_CASE(decodescript_empty)
{
    json params = json::array();
    params.push_back(std::string(""));
    json result = decodescript(params, false);

    // Empty script has empty asm
    std::string asm_str = result["asm"].get<std::string>();
    BOOST_CHECK(asm_str.empty());

    // p2sh field still present
    std::string p2sh = result["p2sh"].get<std::string>();
    BOOST_CHECK(!p2sh.empty());
}

BOOST_AUTO_TEST_CASE(decodescript_p2sh_address_prefix)
{
    // Any decoded script's p2sh should start with "C" (Pinkcoin P2SH prefix)
    CScript script;
    script << OP_1;
    std::string hex = HexStr(script.begin(), script.end());

    json params = json::array();
    params.push_back(hex);
    json result = decodescript(params, false);
    std::string p2sh = result["p2sh"].get<std::string>();
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
    std::string hexTx = HexStr(ss.begin(), ss.end());

    json params = json::array();
    params.push_back(hexTx);
    json result = decoderawtransaction(params, false);

    // Verify Pinkcoin-specific "time" field
    BOOST_CHECK_EQUAL(result["time"].get<int64_t>(), 1700000000);
    BOOST_CHECK_EQUAL(result["version"].get<int>(), 1);
    BOOST_CHECK_EQUAL(result["locktime"].get<int64_t>(), 0);

    // txid should be non-empty hex
    std::string txid = result["txid"].get<std::string>();
    BOOST_CHECK(!txid.empty());
    BOOST_CHECK(IsHex(txid));

    // vin and vout arrays
    json vin = result["vin"];
    BOOST_CHECK_EQUAL(vin.size(), 1u);
    json vout = result["vout"];
    BOOST_CHECK_EQUAL(vout.size(), 1u);
}

BOOST_AUTO_TEST_CASE(decoderawtransaction_invalid_hex)
{
    json params = json::array();
    params.push_back(std::string("not_valid_hex_data_zzzz"));
    BOOST_CHECK_THROW(decoderawtransaction(params, false), json);
}

BOOST_AUTO_TEST_CASE(decoderawtransaction_truncated)
{
    // Valid hex but not a valid serialized transaction
    json params = json::array();
    params.push_back(std::string("deadbeef"));
    BOOST_CHECK_THROW(decoderawtransaction(params, false), json);
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
    std::string hexTx = HexStr(ss.begin(), ss.end());

    json params = json::array();
    params.push_back(hexTx);
    json result = decoderawtransaction(params, false);

    // The "time" field must be present and correct
    BOOST_CHECK_EQUAL(result["time"].get<int64_t>(), 1234567890);
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
    json input;
    input["txid"] = std::string("0000000000000000000000000000000000000000000000000000000000000001");
    input["vout"] = 0;
    json inputs = json::array();
    inputs.push_back(input);

    // Build outputs object
    json outputs;
    outputs[addr.ToString()] = 1.0;

    json params = json::array();
    params.push_back(inputs);
    params.push_back(outputs);
    json result = createrawtransaction(params, false);

    // Result is a hex string
    std::string hex = result.get<std::string>();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));

    // Round-trip: decode the result
    json decodeParams = json::array();
    decodeParams.push_back(hex);
    json decoded = decoderawtransaction(decodeParams, false);
    BOOST_CHECK_EQUAL(decoded["vin"].size(), 1u);
    BOOST_CHECK_EQUAL(decoded["vout"].size(), 1u);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_invalid_address)
{
    json input;
    input["txid"] = std::string("0000000000000000000000000000000000000000000000000000000000000001");
    input["vout"] = 0;
    json inputs = json::array();
    inputs.push_back(input);

    json outputs;
    outputs["invalid_address"] = 1.0;

    json params = json::array();
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), json);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_duplicate_address)
{
    CKey key;
    key.MakeNewKey(true);
    CBitcoinAddress addr(key.GetPubKey().GetID());

    json input;
    input["txid"] = std::string("0000000000000000000000000000000000000000000000000000000000000001");
    input["vout"] = 0;
    json inputs = json::array();
    inputs.push_back(input);

    // nlohmann/json objects cannot have duplicate keys -- the second assignment
    // overwrites the first.  The production code checks for duplicates via
    // iteration, so we need to pass two distinct entries.  With nlohmann/json
    // we cannot represent duplicate keys in a json object, so we construct
    // the scenario by testing that a single-key object does NOT throw.
    // The original json_spirit test relied on Object supporting duplicate keys.
    // With nlohmann/json, duplicate key insertion silently overwrites, so the
    // duplicate-address error path is unreachable via JSON.  We verify the
    // single-address case succeeds instead.
    json outputs;
    outputs[addr.ToString()] = 1.0;

    json params = json::array();
    params.push_back(inputs);
    params.push_back(outputs);
    // Should succeed (single address, no duplicate)
    json result = createrawtransaction(params, false);
    BOOST_CHECK(!result.get<std::string>().empty());
}

BOOST_AUTO_TEST_CASE(createrawtransaction_missing_txid)
{
    json input;
    input["vout"] = 0;
    json inputs = json::array();
    inputs.push_back(input);

    CKey key;
    key.MakeNewKey(true);
    json outputs;
    outputs[CBitcoinAddress(key.GetPubKey().GetID()).ToString()] = 1.0;

    json params = json::array();
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), json);
}

BOOST_AUTO_TEST_CASE(createrawtransaction_negative_vout)
{
    json input;
    input["txid"] = std::string("0000000000000000000000000000000000000000000000000000000000000001");
    input["vout"] = -1;
    json inputs = json::array();
    inputs.push_back(input);

    CKey key;
    key.MakeNewKey(true);
    json outputs;
    outputs[CBitcoinAddress(key.GetPubKey().GetID()).ToString()] = 1.0;

    json params = json::array();
    params.push_back(inputs);
    params.push_back(outputs);
    BOOST_CHECK_THROW(createrawtransaction(params, false), json);
}

// ============================================================================
// makekeypair tests
// ============================================================================

BOOST_AUTO_TEST_CASE(makekeypair_returns_keys)
{
    json params = json::array();
    json result = makekeypair(params, false);

    std::string privKey = result["PrivateKey"].get<std::string>();
    std::string pubKey = result["PublicKey"].get<std::string>();

    BOOST_CHECK(!privKey.empty());
    BOOST_CHECK(!pubKey.empty());
    BOOST_CHECK(IsHex(privKey));
    BOOST_CHECK(IsHex(pubKey));
}

BOOST_AUTO_TEST_CASE(makekeypair_valid_pubkey)
{
    json params = json::array();
    json result = makekeypair(params, false);

    std::string pubKeyHex = result["PublicKey"].get<std::string>();
    std::vector<unsigned char> pubKeyBytes = ParseHex(pubKeyHex);

    // Uncompressed public key: 65 bytes, prefix 0x04
    BOOST_CHECK_EQUAL(pubKeyBytes.size(), 65u);
    BOOST_CHECK_EQUAL(pubKeyBytes[0], 0x04);
}

BOOST_AUTO_TEST_CASE(makekeypair_unique_keys)
{
    json params = json::array();
    json result1 = makekeypair(params, false);
    json result2 = makekeypair(params, false);

    std::string pub1 = result1["PublicKey"].get<std::string>();
    std::string pub2 = result2["PublicKey"].get<std::string>();
    BOOST_CHECK(pub1 != pub2);
}

// ============================================================================
// AccountFromValue tests
// ============================================================================

BOOST_AUTO_TEST_CASE(accountfromvalue_valid)
{
    BOOST_CHECK_EQUAL(AccountFromValue(json("myaccount")), "myaccount");
}

BOOST_AUTO_TEST_CASE(accountfromvalue_wildcard_throws)
{
    BOOST_CHECK_THROW(AccountFromValue(json("*")), json);
}

BOOST_AUTO_TEST_CASE(accountfromvalue_empty_is_default)
{
    // Empty string is the default account -- valid
    BOOST_CHECK_EQUAL(AccountFromValue(json("")), "");
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

    json out;
    ScriptPubKeyToJSON(script, out, false);

    std::string type = out["type"].get<std::string>();
    BOOST_CHECK_EQUAL(type, "pubkeyhash");
    BOOST_CHECK_EQUAL(out["reqSigs"].get<int>(), 1);

    json addrs = out["addresses"];
    BOOST_CHECK_EQUAL(addrs.size(), 1u);
    std::string addr = addrs[0].get<std::string>();
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

    json out;
    ScriptPubKeyToJSON(script, out, false);

    std::string type = out["type"].get<std::string>();
    BOOST_CHECK_EQUAL(type, "scripthash");

    json addrs = out["addresses"];
    BOOST_CHECK_EQUAL(addrs.size(), 1u);
    std::string addr = addrs[0].get<std::string>();
    BOOST_CHECK_EQUAL(addr[0], 'C'); // Pinkcoin P2SH prefix
}

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_op_return)
{
    CScript script;
    script << OP_RETURN << ParseHex("deadbeef");

    json out;
    ScriptPubKeyToJSON(script, out, false);

    std::string type = out["type"].get<std::string>();
    BOOST_CHECK_EQUAL(type, "nulldata");

    // OP_RETURN scripts have no addresses
    BOOST_CHECK(out["addresses"].is_null());
}

BOOST_AUTO_TEST_CASE(scriptpubkeytojson_include_hex)
{
    CKey key;
    key.MakeNewKey(true);
    CScript script;
    script.SetDestination(key.GetPubKey().GetID());

    json out;
    ScriptPubKeyToJSON(script, out, true);

    // With fIncludeHex=true, "hex" field must be present
    std::string hex = out["hex"].get<std::string>();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));

    // asm field is always present
    BOOST_CHECK(!out["asm"].get<std::string>().empty());
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

    json entry;
    TxToJSON(tx, 0, entry);

    // Coinbase vin has "coinbase" key
    json vin = entry["vin"];
    BOOST_CHECK_EQUAL(vin.size(), 1u);
    json vinObj = vin[0];
    BOOST_CHECK(!vinObj["coinbase"].is_null());
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

    json entry;
    TxToJSON(tx, 0, entry);

    // Regular vin has "txid"/"vout"/"scriptSig" keys
    json vin = entry["vin"];
    BOOST_CHECK_EQUAL(vin.size(), 2u);
    json vinObj = vin[0];
    BOOST_CHECK(vinObj["txid"].is_string());
    BOOST_CHECK(vinObj["vout"].is_number_integer());
    BOOST_CHECK(vinObj["scriptSig"].is_object());
}

BOOST_AUTO_TEST_CASE(txtojson_pinkcoin_fields)
{
    CTransaction tx;
    tx.nVersion = 2;
    tx.nTime = 1234567890;
    tx.nLockTime = 500000;
    tx.vin.push_back(CTxIn(COutPoint(uint256("0000000000000000000000000000000000000000000000000000000000000001"), 0)));
    tx.vout.push_back(CTxOut(COIN, CScript()));

    json entry;
    TxToJSON(tx, 0, entry);

    // All Pinkcoin-specific fields present
    BOOST_CHECK(entry["txid"].is_string());
    BOOST_CHECK_EQUAL(entry["version"].get<int>(), 2);
    BOOST_CHECK_EQUAL(entry["time"].get<int64_t>(), 1234567890);
    BOOST_CHECK_EQUAL(entry["locktime"].get<int64_t>(), 500000);
    BOOST_CHECK(entry["vin"].is_array());
    BOOST_CHECK(entry["vout"].is_array());
}

BOOST_AUTO_TEST_CASE(txtojson_no_blockhash_when_zero)
{
    CTransaction tx;
    tx.vin.push_back(CTxIn());
    tx.vout.push_back(CTxOut(0, CScript()));

    json entry;
    TxToJSON(tx, 0, entry);

    // hashBlock=0 -> no blockhash/confirmations fields
    BOOST_CHECK(entry["blockhash"].is_null());
    BOOST_CHECK(entry["confirmations"].is_null());
}

// ============================================================================
// GetDifficulty tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getdifficulty_null_returns_one)
{
    // With nullptr and pindexBest=nullptr -> returns 1.0
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
    // nShift=30 > 29, so dDiff /= 256 -> ~0.000244
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
    // nShift == 29, no loop -> diff = 1.0
    BOOST_CHECK_CLOSE(diff, 1.0, 0.001);
}

// ============================================================================
// getnewaddress / getnewpubkey tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getnewaddress_default)
{
    json params = json::array();
    json result = getnewaddress(params, false);
    std::string addr = result.get<std::string>();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');  // Pinkcoin P2PKH prefix
    CBitcoinAddress address(addr);
    BOOST_CHECK(address.IsValid());
}

BOOST_AUTO_TEST_CASE(getnewaddress_with_account)
{
    json params = json::array();
    params.push_back(std::string("testaccount"));
    json result = getnewaddress(params, false);
    std::string addr = result.get<std::string>();
    BOOST_CHECK(!addr.empty());
    CBitcoinAddress address(addr);
    BOOST_CHECK(address.IsValid());
}

BOOST_AUTO_TEST_CASE(getnewpubkey_returns_hex)
{
    json params = json::array();
    json result = getnewpubkey(params, false);
    std::string pubkeyHex = result.get<std::string>();
    BOOST_CHECK(!pubkeyHex.empty());
    BOOST_CHECK(IsHex(pubkeyHex));

    std::vector<unsigned char> vchPubKey = ParseHex(pubkeyHex);
    CPubKey pubkey(vchPubKey);
    BOOST_CHECK(pubkey.IsValid());
}

// ============================================================================
// getaccount / setaccount / getaddressesbyaccount tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getaccount_valid_address)
{
    // Create a new address with an account
    json newAddr = json::array();
    newAddr.push_back(std::string("test_getaccount"));
    std::string addr = getnewaddress(newAddr, false).get<std::string>();

    json params = json::array();
    params.push_back(addr);
    json result = getaccount(params, false);
    BOOST_CHECK_EQUAL(result.get<std::string>(), "test_getaccount");
}

BOOST_AUTO_TEST_CASE(getaccount_invalid_address)
{
    json params = json::array();
    params.push_back(std::string("invalid_address_here"));
    BOOST_CHECK_THROW(getaccount(params, false), json);
}

BOOST_AUTO_TEST_CASE(setaccount_assigns_account)
{
    // Get a new address
    json empty = json::array();
    std::string addr = getnewaddress(empty, false).get<std::string>();

    // Set its account
    json params = json::array();
    params.push_back(addr);
    params.push_back(std::string("newlabel"));
    setaccount(params, false);

    // Verify
    json getParams = json::array();
    getParams.push_back(addr);
    BOOST_CHECK_EQUAL(getaccount(getParams, false).get<std::string>(), "newlabel");
}

BOOST_AUTO_TEST_CASE(getaddressesbyaccount_populated)
{
    // Create address with specific account
    json newAddr = json::array();
    newAddr.push_back(std::string("addrbyacct_test"));
    std::string addr = getnewaddress(newAddr, false).get<std::string>();

    json params = json::array();
    params.push_back(std::string("addrbyacct_test"));
    json result = getaddressesbyaccount(params, false);
    BOOST_CHECK(!result.empty());

    // The address we created should be in the list
    bool found = false;
    for (const json& v : result) {
        if (v.get<std::string>() == addr) {
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
    json params = json::array();
    json result = getbalance(params, false);
    // Balance is a real number (may be 0 in test env)
    BOOST_CHECK(result.is_number_float());
    BOOST_CHECK(result.get<double>() >= 0.0);
}

BOOST_AUTO_TEST_CASE(getbalance_with_star)
{
    // "*" returns total balance across all accounts
    json params = json::array();
    params.push_back(std::string("*"));
    json result = getbalance(params, false);
    BOOST_CHECK(result.is_number_float());
    BOOST_CHECK(result.get<double>() >= 0.0);
}

// ============================================================================
// validateaddress / validatepubkey tests
// ============================================================================

BOOST_AUTO_TEST_CASE(validateaddress_valid)
{
    // Generate a known address
    json empty = json::array();
    std::string addr = getnewaddress(empty, false).get<std::string>();

    json params = json::array();
    params.push_back(addr);
    json result = validateaddress(params, false);

    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), true);
    BOOST_CHECK(result["address"].is_string());
    BOOST_CHECK(result["ismine"].is_boolean());
    BOOST_CHECK_EQUAL(result["ismine"].get<bool>(), true);
}

BOOST_AUTO_TEST_CASE(validateaddress_invalid)
{
    json params = json::array();
    params.push_back(std::string("not_a_valid_address"));
    json result = validateaddress(params, false);

    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), false);
}

BOOST_AUTO_TEST_CASE(validatepubkey_valid)
{
    // Get a pubkey from the wallet
    json empty = json::array();
    std::string pubkeyHex = getnewpubkey(empty, false).get<std::string>();

    json params = json::array();
    params.push_back(pubkeyHex);
    json result = validatepubkey(params, false);

    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), true);
    BOOST_CHECK(result["address"].is_string());
    BOOST_CHECK(result["iscompressed"].is_boolean());
}

BOOST_AUTO_TEST_CASE(validatepubkey_invalid)
{
    json params = json::array();
    params.push_back(std::string("deadbeef"));
    json result = validatepubkey(params, false);

    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), false);
}

// ============================================================================
// listaccounts / listreceivedbyaddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(listaccounts_returns_map)
{
    json params = json::array();
    json result = listaccounts(params, false);
    // Should have at least the default "" account
    BOOST_CHECK(result.size() >= 1);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaddress_default)
{
    json params = json::array();
    json result = listreceivedbyaddress(params, false);
    BOOST_CHECK(result.is_array());
}

// ============================================================================
// getwalletinfo / getstakesplitthreshold tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getwalletinfo_returns_object)
{
    json params = json::array();
    json result = getwalletinfo(params, false);

    BOOST_CHECK(result["walletversion"].is_number_integer());
    BOOST_CHECK(result["balance"].is_number_float());
    BOOST_CHECK(result["txcount"].is_number_integer());
    BOOST_CHECK(result["keypoololdest"].is_number_integer());
    BOOST_CHECK(result["keypoolsize"].is_number_integer());
}

BOOST_AUTO_TEST_CASE(getstakesplitthreshold_returns_object)
{
    json params = json::array();
    json result = getstakesplitthreshold(params, false);
    BOOST_CHECK(result["split threshold"].is_number_float());
}

// ============================================================================
// keypoolrefill tests
// ============================================================================

BOOST_AUTO_TEST_CASE(keypoolrefill_default)
{
    json params = json::array();
    // Should not throw -- refills keypool
    BOOST_CHECK_NO_THROW(keypoolrefill(params, false));
}

// ============================================================================
// reservebalance tests
// ============================================================================

BOOST_AUTO_TEST_CASE(reservebalance_query)
{
    // No params -> returns current reserve setting
    json params = json::array();
    json result = reservebalance(params, false);
    BOOST_CHECK(result["reserve"].is_boolean());
    BOOST_CHECK(result["amount"].is_number_float());
}

BOOST_AUTO_TEST_CASE(reservebalance_set_and_query)
{
    // Set reserve on with 10.0
    json setParams = json::array();
    setParams.push_back(true);
    setParams.push_back(10.0);
    json result = reservebalance(setParams, false);
    BOOST_CHECK_EQUAL(result["reserve"].get<bool>(), true);

    // Disable reserve
    json offParams = json::array();
    offParams.push_back(false);
    reservebalance(offParams, false);
}

// ============================================================================
// getreceivedbyaddress tests
// ============================================================================

BOOST_AUTO_TEST_CASE(getreceivedbyaddress_zero_for_unused)
{
    json empty = json::array();
    std::string addr = getnewaddress(empty, false).get<std::string>();

    json params = json::array();
    params.push_back(addr);
    json result = getreceivedbyaddress(params, false);
    BOOST_CHECK_CLOSE(result.get<double>(), 0.0, 0.001);
}

// ============================================================================
// rpcnet: getconnectioncount / getpeerinfo
// ============================================================================

BOOST_AUTO_TEST_CASE(getconnectioncount_zero)
{
    json params = json::array();
    json result = getconnectioncount(params, false);
    // In test mode, no peers connected
    BOOST_CHECK_EQUAL(result.get<int>(), 0);
}

BOOST_AUTO_TEST_CASE(getpeerinfo_empty)
{
    json params = json::array();
    json result = getpeerinfo(params, false);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Tests requiring a real chain (TestChain fixture)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_chain_command_tests, TestChain)

BOOST_AUTO_TEST_CASE(getrawtransaction_hex)
{
    BOOST_REQUIRE(!coinbaseTxns.empty());
    std::string txid = coinbaseTxns[0].GetHash().GetHex();

    json params = json::array();
    params.push_back(txid);
    params.push_back(0);  // verbose=0 -> hex string
    json result = getrawtransaction(params, false);
    std::string hex = result.get<std::string>();
    BOOST_CHECK(!hex.empty());
    BOOST_CHECK(IsHex(hex));
}

BOOST_AUTO_TEST_CASE(getrawtransaction_json)
{
    BOOST_REQUIRE(!coinbaseTxns.empty());
    std::string txid = coinbaseTxns[0].GetHash().GetHex();

    json params = json::array();
    params.push_back(txid);
    params.push_back(1);  // verbose=1 -> JSON object
    json result = getrawtransaction(params, false);
    BOOST_CHECK(result["txid"].is_string());
    BOOST_CHECK(result["version"].is_number_integer());
}

BOOST_AUTO_TEST_CASE(getrawtransaction_notfound)
{
    json params = json::array();
    params.push_back(std::string("0000000000000000000000000000000000000000000000000000000000000bad"));
    params.push_back(0);
    BOOST_CHECK_THROW(getrawtransaction(params, false), json);
}

BOOST_AUTO_TEST_CASE(listunspent_default)
{
    json params = json::array();
    json result = listunspent(params, false);
    BOOST_CHECK(result.is_array());
}

BOOST_AUTO_TEST_CASE(listunspent_with_minconf)
{
    json params = json::array();
    params.push_back(1);   // minconf
    params.push_back(999); // maxconf
    json result = listunspent(params, false);
    BOOST_CHECK(result.is_array());
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
    std::string hex = HexStr(script.begin(), script.end());

    json params = json::array();
    params.push_back(hex);
    json result = decodescript(params, false);

    BOOST_CHECK_EQUAL(result["type"].get<std::string>(), "multisig");
    BOOST_CHECK_EQUAL(result["reqSigs"].get<int>(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
