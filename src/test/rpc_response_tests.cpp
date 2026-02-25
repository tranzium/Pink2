// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Pre-Phase 6D test hardening: RPC response structure contracts.
// Pins field names, types, and consensus values for all RPC commands.
// Contract-based: checks field presence + type, not exact JSON strings.
// Adding new fields to responses will NOT break these tests.
// Only removing or renaming a field triggers failure.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "init.h"
#include "smessage/smessage.h"
#include "test_framework.h"

using std::runtime_error;
using std::string;

extern CWallet* pwalletMain;
extern json help(const json& params, bool fHelp);

// ============================================================================
// Suite: rpc_response_info — info & status RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_info, TestChain)

// ---------------------------------------------------------------------------
// getinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getinfo_response_contract)
{
    json p = json::array();
    json result = getinfo(p, false);

    // String fields
    BOOST_CHECK(result["version"].is_string());
    BOOST_CHECK(result["errors"].is_string());

    // Integer fields
    BOOST_CHECK(result["protocolversion"].is_number_integer());
    BOOST_CHECK_EQUAL(result["protocolversion"].get<int>(), 60019);
    BOOST_CHECK(result["walletversion"].is_number_integer());
    BOOST_CHECK(result["blocks"].is_number_integer());
    BOOST_CHECK(result["connections"].is_number_integer());
    BOOST_CHECK(result["keypoololdest"].is_number_integer());
    BOOST_CHECK(result["keypoolsize"].is_number_integer());

    BOOST_CHECK(result["timeoffset"].is_number_integer());

    // String fields (additional)
    BOOST_CHECK(result["offsetfrom"].is_string());
    BOOST_CHECK(result["proxy"].is_string());
    BOOST_CHECK(result["ip"].is_string());

    // Real (double) fields — ValueFromAmount returns double
    BOOST_CHECK(result["balance"].is_number_float());
    BOOST_CHECK(result["newmint"].is_number_float());
    BOOST_CHECK(result["stake"].is_number_float());
    BOOST_CHECK(result["moneysupply"].is_number_float());
    BOOST_CHECK(result["paytxfee"].is_number_float());
    BOOST_CHECK(result["mininput"].is_number_float());

    // Boolean fields
    BOOST_CHECK(result["testnet"].is_boolean());
    BOOST_CHECK_EQUAL(result["testnet"].get<bool>(), false);

    // Nested object: difficulty
    BOOST_CHECK(result["difficulty"].is_object());
    const json& diff = result["difficulty"];
    BOOST_CHECK(diff["proof-of-work"].is_number_float());
    BOOST_CHECK(diff["proof-of-stake"].is_number_float());
    BOOST_CHECK(diff["proof-of-stake (flash)"].is_number_float());
}

// ---------------------------------------------------------------------------
// getmininginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getmininginfo_response_contract)
{
    json p = json::array();
    json result = getmininginfo(p, false);

    // Core fields always present
    BOOST_CHECK(result["blocks"].is_number_integer());
    BOOST_CHECK(result["next-block-value-pos"].is_number_float());
    BOOST_CHECK(result["last-block-size"].is_number_integer());
    BOOST_CHECK(result["last-block-tx"].is_number_integer());
    BOOST_CHECK(result["pooledtx"].is_number_integer());
    BOOST_CHECK(result["tx-fee"].is_number_float());

    // Staking sub-object
    BOOST_CHECK(result["staking"].is_object());
    const json& staking = result["staking"];
    BOOST_CHECK(staking["enabled"].is_boolean());
    BOOST_CHECK(staking["targeting-fpos"].is_boolean());
    BOOST_CHECK(staking["estimated-time"].is_number_integer());
    BOOST_CHECK(staking["search-interval"].is_number_integer());
    BOOST_CHECK(staking["utxo-combine-threshold"].is_number_integer());
    BOOST_CHECK(staking["utxo-split-threshold"].is_number_integer());

    // Stake weight sub-object
    BOOST_CHECK(result["stakeweight"].is_object());
    const json& sw = result["stakeweight"];
    BOOST_CHECK(sw["minimum"].is_number_integer());
    BOOST_CHECK(sw["maximum"].is_number_integer());
    BOOST_CHECK(sw["combined"].is_number_integer());
    BOOST_CHECK(sw["network"].is_number_integer());

    // Difficulty sub-object
    BOOST_CHECK(result["difficulty"].is_object());
    const json& rdiff = result["difficulty"];
    BOOST_CHECK(rdiff["proof-of-stake"].is_number_float());
    BOOST_CHECK(rdiff["proof-of-stake(flash)"].is_number_float());

    // Top-level scalars
    BOOST_CHECK(result["netstakeweight"].is_number_integer());
    BOOST_CHECK(result["testnet"].is_boolean());
    BOOST_CHECK_EQUAL(result["testnet"].get<bool>(), false);
    BOOST_CHECK(result["errors"].is_string());
}

// ---------------------------------------------------------------------------
// getstakinginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakinginfo_response_contract)
{
    json p = json::array();
    json result = getstakinginfo(p, false);

    // Boolean fields
    BOOST_CHECK(result["enabled"].is_boolean());
    BOOST_CHECK(result["staking"].is_boolean());

    // String fields
    BOOST_CHECK(result["errors"].is_string());

    // Integer fields (uint64_t → number_integer in nlohmann)
    BOOST_CHECK(result["currentblocksize"].is_number_integer());
    BOOST_CHECK(result["currentblocktx"].is_number_integer());
    BOOST_CHECK(result["pooledtx"].is_number_integer());
    BOOST_CHECK(result["search-interval"].is_number_integer());
    BOOST_CHECK(result["weight"].is_number_integer());
    BOOST_CHECK(result["netstakeweight"].is_number_integer());
    BOOST_CHECK(result["expectedtime"].is_number_integer());

    // Real (double) fields
    BOOST_CHECK(result["difficulty"].is_number_float());
    BOOST_CHECK(result["difficulty (flash)"].is_number_float());
}

// ---------------------------------------------------------------------------
// getdifficulty — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getdifficulty_response_contract)
{
    json p = json::array();
    json result = getdifficulty(p, false);

    BOOST_CHECK(result["proof-of-work"].is_number_float());
    BOOST_CHECK(result["proof-of-stake"].is_number_float());
    BOOST_CHECK(result["proof-of-stake (flash)"].is_number_float());
    BOOST_CHECK(result["search-interval"].is_number_integer());
}

// ---------------------------------------------------------------------------
// getsubsidy — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getsubsidy_response_contract)
{
    json p = json::array();
    json result = getsubsidy(p, false);

    // getsubsidy returns a bare uint64_t (block subsidy in satoshis)
    BOOST_CHECK(result.is_number_integer());
    // At test chain height (~51), PoW subsidy is 0 — only block 1 and
    // heights >= 17000 have non-zero PoW rewards. Verify non-negative.
    BOOST_CHECK(result.get<int64_t>() >= 0);
}

// ---------------------------------------------------------------------------
// getwalletinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getwalletinfo_response_contract)
{
    json p = json::array();
    json result = getwalletinfo(p, false);

    BOOST_CHECK(result["walletversion"].is_number_integer());
    BOOST_CHECK(result["balance"].is_number_float());
    BOOST_CHECK(result["txcount"].is_number_integer());
    BOOST_CHECK(result["keypoololdest"].is_number_integer());
    BOOST_CHECK(result["keypoolsize"].is_number_integer());

    // "unlocked_until" only present when wallet is encrypted — not guaranteed
    // in test environment, so we do not assert its presence.
}

// ---------------------------------------------------------------------------
// getloglevel — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getloglevel_response_contract)
{
    json p = json::array();
    json result = getloglevel(p, false);

    BOOST_CHECK(result["level"].is_string());
    BOOST_CHECK(result["categories"].is_array());
}

// ---------------------------------------------------------------------------
// help — returns non-empty string
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_returns_string)
{
    json p = json::array();
    json result = help(p, false);

    BOOST_CHECK(result.is_string());
    BOOST_CHECK(!result.get<string>().empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_wallet — wallet RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_wallet, TestChain)

// ---------------------------------------------------------------------------
// getnewaddress — returns string, Pinkcoin address starts with "2"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnewaddress_response_contract)
{
    json p = json::array();
    json result = getnewaddress(p, false);
    BOOST_CHECK(result.is_string());
    string addr = result.get<string>();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');  // Pinkcoin PUBKEY_ADDRESS prefix
}

// ---------------------------------------------------------------------------
// getnewpubkey — returns string (hex-encoded public key)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnewpubkey_response_contract)
{
    json p = json::array();
    json result = getnewpubkey(p, false);
    BOOST_CHECK(result.is_string());
    string hexPubKey = result.get<string>();
    BOOST_CHECK(!hexPubKey.empty());
    // Compressed pubkey = 66 hex chars, uncompressed = 130 hex chars
    BOOST_CHECK(hexPubKey.size() == 66 || hexPubKey.size() == 130);
    BOOST_CHECK(IsHex(hexPubKey));
}

// ---------------------------------------------------------------------------
// getaccountaddress — param: "" (default account), returns string
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaccountaddress_response_contract)
{
    json p = json::array();
    p.push_back(string(""));  // default account
    json result = getaccountaddress(p, false);
    BOOST_CHECK(result.is_string());
    string addr = result.get<string>();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');
}

// ---------------------------------------------------------------------------
// getaccount — param: a valid address, returns string (account name)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaccount_response_contract)
{
    // First get a valid address
    json pNew = json::array();
    string addr = getnewaddress(pNew, false).get<string>();

    json p = json::array();
    p.push_back(addr);
    json result = getaccount(p, false);
    BOOST_CHECK(result.is_string());
    // Default account is ""
    BOOST_CHECK_EQUAL(result.get<string>(), "");
}

// ---------------------------------------------------------------------------
// getbalance — returns float, >= 0
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getbalance_response_contract)
{
    json p = json::array();
    json result = getbalance(p, false);
    BOOST_CHECK(result.is_number_float());
    BOOST_CHECK(result.get<double>() >= 0.0);
}

// ---------------------------------------------------------------------------
// getreceivedbyaddress — param: a valid address, returns float
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getreceivedbyaddress_response_contract)
{
    // Get a fresh address owned by the wallet
    json pNew = json::array();
    string addr = getnewaddress(pNew, false).get<string>();

    json p = json::array();
    p.push_back(addr);
    json result = getreceivedbyaddress(p, false);
    BOOST_CHECK(result.is_number_float());
    BOOST_CHECK(result.get<double>() >= 0.0);
}

// ---------------------------------------------------------------------------
// signmessage + verifymessage — sign returns string, verify returns bool
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(signmessage_response_contract)
{
    // Get a wallet address
    json pNew = json::array();
    string addr = getnewaddress(pNew, false).get<string>();

    json p = json::array();
    p.push_back(addr);
    p.push_back(string("test message"));
    json result = signmessage(p, false);
    BOOST_CHECK(result.is_string());
    string sig = result.get<string>();
    BOOST_CHECK(!sig.empty());
    // Base64 encoded signature — length should be reasonable
    BOOST_CHECK(sig.size() > 10);
}

BOOST_AUTO_TEST_CASE(verifymessage_response_contract)
{
    // Sign a message first
    json pNew = json::array();
    string addr = getnewaddress(pNew, false).get<string>();

    json pSign = json::array();
    pSign.push_back(addr);
    pSign.push_back(string("verify test"));
    string sig = signmessage(pSign, false).get<string>();

    // Verify
    json p = json::array();
    p.push_back(addr);
    p.push_back(sig);
    p.push_back(string("verify test"));
    json result = verifymessage(p, false);
    BOOST_CHECK(result.is_boolean());
    BOOST_CHECK_EQUAL(result.get<bool>(), true);
}

// ---------------------------------------------------------------------------
// settxfee — param: 0.0001, returns bool (true)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(settxfee_response_contract)
{
    json p = json::array();
    p.push_back(0.0001);
    json result = settxfee(p, false);
    BOOST_CHECK(result.is_boolean());
    BOOST_CHECK_EQUAL(result.get<bool>(), true);
}

// ---------------------------------------------------------------------------
// getrawmempool — returns array of string txids
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getrawmempool_response_contract)
{
    json p = json::array();
    json result = getrawmempool(p, false);
    BOOST_CHECK(result.is_array());
    // Mempool may be empty in test mode; just verify array type.
    // If non-empty, elements should be strings (txid hex strings).
    for (const auto& v : result)
        BOOST_CHECK(v.is_string());
}

// ---------------------------------------------------------------------------
// getaddressesbyaccount — param: "" (default), returns array
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaddressesbyaccount_response_contract)
{
    // Ensure at least one address exists in the default account
    json pNew = json::array();
    getnewaddress(pNew, false);

    json p = json::array();
    p.push_back(string(""));
    json result = getaddressesbyaccount(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_CHECK(!result.empty());
    // Each element should be a string address
    for (const auto& v : result)
    {
        BOOST_CHECK(v.is_string());
        char prefix = v.get<string>()[0];
        // '2' = PUBKEY_ADDRESS, 'C' = SCRIPT_ADDRESS (P2SH)
        BOOST_CHECK(prefix == '2' || prefix == 'C');
    }
}

// ---------------------------------------------------------------------------
// listreceivedbyaddress — returns array with field contracts
// Fields: "address"(str), "account"(str), "amount"(real), "confirmations"(int)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listreceivedbyaddress_response_contract)
{
    // Add a wallet-visible tx with output to a wallet-owned address
    json pAddr = json::array();
    string testAddr = getnewaddress(pAddr, false).get<string>();
    CBitcoinAddress destAddr(testAddr);
    CScript destScript;
    destScript.SetDestination(destAddr.Get());

    CTransaction tx;
    tx.vin.push_back(CTxIn(coinbaseTxns[1].GetHash(), 0));
    tx.vout.push_back(CTxOut(10 * COIN, destScript));

    CWalletTx wtx(pwalletMain, tx);
    wtx.hashBlock = blockIndexAt(nBaseHeight + 1)->GetBlockHash();
    wtx.nIndex = 0;
    wtx.fMerkleVerified = true;  // bypass Merkle branch check
    pwalletMain->AddToWallet(wtx);

    json p = json::array();
    p.push_back(1);      // minconf = 1
    p.push_back(false);  // includeempty = false
    json result = listreceivedbyaddress(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_REQUIRE(!result.empty());
    const json& elem = result[0];
    BOOST_CHECK(elem["address"].is_string());
    BOOST_CHECK(elem["account"].is_string());
    BOOST_CHECK(elem["amount"].is_number_float());
    BOOST_CHECK(elem["confirmations"].is_number_integer());
}

// ---------------------------------------------------------------------------
// listtransactions — returns array; check element structure
// Fields: "account"(str), "category"(str), "amount"(real), plus WalletTxToJSON fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listtransactions_response_contract)
{
    // Add a wallet-visible tx so listtransactions has entries to return
    json pAddr = json::array();
    string testAddr = getnewaddress(pAddr, false).get<string>();
    CBitcoinAddress destAddr(testAddr);
    CScript destScript;
    destScript.SetDestination(destAddr.Get());

    CTransaction tx;
    tx.vin.push_back(CTxIn(coinbaseTxns[2].GetHash(), 0));
    tx.vout.push_back(CTxOut(10 * COIN, destScript));

    CWalletTx wtx(pwalletMain, tx);
    wtx.hashBlock = blockIndexAt(nBaseHeight + 1)->GetBlockHash();
    wtx.nIndex = 0;
    wtx.fMerkleVerified = true;
    pwalletMain->AddToWallet(wtx);

    json p = json::array();
    json result = listtransactions(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_REQUIRE(!result.empty());
    // Every entry (whether tx or accounting "move") has these fields:
    for (const auto& v : result)
    {
        BOOST_CHECK(v["account"].is_string());
        BOOST_CHECK(v["category"].is_string());
        BOOST_CHECK(v["amount"].is_number_float());

        string category = v["category"].get<string>();
        if (category != "move")
        {
            // WalletTxToJSON fields (only for real transactions, not "move" entries)
            BOOST_CHECK(v["confirmations"].is_number_integer());
            BOOST_CHECK(v["txid"].is_string());
            BOOST_CHECK(v["time"].is_number_integer());
            BOOST_CHECK(v["timereceived"].is_number_integer());
        }
        else
        {
            // Accounting "move" entries have "time" and "otheraccount"
            BOOST_CHECK(v["time"].is_number_integer());
            BOOST_CHECK(v["otheraccount"].is_string());
        }
    }
}

// ---------------------------------------------------------------------------
// listunspent — returns array; check element structure
// Fields: "txid"(str), "vout"(int), "scriptPubKey"(str), "amount"(real),
//         "confirmations"(int), optionally "address"(str), "account"(str)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listunspent_response_contract)
{
    // Add a wallet-visible tx with unspent output to a wallet-owned address
    json pAddr = json::array();
    string testAddr = getnewaddress(pAddr, false).get<string>();
    CBitcoinAddress destAddr(testAddr);
    CScript destScript;
    destScript.SetDestination(destAddr.Get());

    CTransaction tx;
    tx.vin.push_back(CTxIn(coinbaseTxns[3].GetHash(), 0));
    tx.vout.push_back(CTxOut(10 * COIN, destScript));

    CWalletTx wtx(pwalletMain, tx);
    wtx.hashBlock = blockIndexAt(nBaseHeight + 1)->GetBlockHash();
    wtx.nIndex = 0;
    wtx.fMerkleVerified = true;
    pwalletMain->AddToWallet(wtx);

    json p = json::array();
    json result = listunspent(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_REQUIRE(!result.empty());
    const json& elem = result[0];
    BOOST_CHECK(elem["txid"].is_string());
    BOOST_CHECK(elem["vout"].is_number_integer());
    BOOST_CHECK(elem["scriptPubKey"].is_string());
    BOOST_CHECK(elem["amount"].is_number_float());
    BOOST_CHECK(elem["confirmations"].is_number_integer());
}

// ---------------------------------------------------------------------------
// getreceivedbyaccount — deprecated accounting API, throws by default
// Pins deprecation behavior: -enableaccounts not set -> runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getreceivedbyaccount_response_contract)
{
    json p = json::array();
    p.push_back(string(""));
    BOOST_CHECK_THROW(getreceivedbyaccount(p, false), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listreceivedbyaccount — deprecated accounting API, throws by default
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listreceivedbyaccount_response_contract)
{
    json p = json::array();
    p.push_back(0);
    p.push_back(true);
    BOOST_CHECK_THROW(listreceivedbyaccount(p, false), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listaddressgroupings — returns array (of array groupings)
// Inner structure: [[address_str, balance_num, ?account_str], ...]
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listaddressgroupings_response_contract)
{
    // Add a wallet-visible tx so address groupings are non-empty
    json pAddr = json::array();
    string testAddr = getnewaddress(pAddr, false).get<string>();
    CBitcoinAddress destAddr(testAddr);
    CScript destScript;
    destScript.SetDestination(destAddr.Get());

    CTransaction tx;
    tx.vin.push_back(CTxIn(coinbaseTxns[4].GetHash(), 0));
    tx.vout.push_back(CTxOut(10 * COIN, destScript));

    CWalletTx wtx(pwalletMain, tx);
    wtx.hashBlock = blockIndexAt(nBaseHeight + 1)->GetBlockHash();
    wtx.nIndex = 0;
    wtx.fMerkleVerified = true;
    pwalletMain->AddToWallet(wtx);

    json p = json::array();
    json result = listaddressgroupings(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_REQUIRE(!result.empty());
    // Validate inner structure
    for (const auto& grouping : result)
    {
        BOOST_CHECK(grouping.is_array());
        for (const auto& addrEntry : grouping)
        {
            BOOST_CHECK(addrEntry.is_array());
            // Must have at least 2 elements: [address, balance]
            BOOST_CHECK(addrEntry.size() >= 2);
            BOOST_CHECK(addrEntry[0].is_string());   // address
            BOOST_CHECK(addrEntry[1].is_number_float());   // balance
            // Optional 3rd element is account name
            if (addrEntry.size() >= 3)
                BOOST_CHECK(addrEntry[2].is_string());
        }
    }
}

// ---------------------------------------------------------------------------
// listaccounts — returns object with account names as keys
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listaccounts_response_contract)
{
    // Ensure at least one address exists
    json pNew = json::array();
    getnewaddress(pNew, false);

    json p = json::array();
    json result = listaccounts(p, false);
    BOOST_CHECK(result.is_object());
    // The implementation maps account -> address string (not amount!)
    // Should have at least the default "" account
    if (!result.empty())
    {
        // Each value should be a string (address)
        BOOST_CHECK(result.begin().value().is_string());
    }
}

// ---------------------------------------------------------------------------
// validateaddress — param: owned address, returns object
// Fields: "isvalid"(bool), "address"(str), "ismine"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validateaddress_response_contract)
{
    // Get an owned address
    json pNew = json::array();
    string addr = getnewaddress(pNew, false).get<string>();

    json p = json::array();
    p.push_back(addr);
    json result = validateaddress(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["isvalid"].is_boolean());
    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), true);
    BOOST_CHECK(result["address"].is_string());
    BOOST_CHECK_EQUAL(result["address"].get<string>(), addr);
    BOOST_CHECK(result["ismine"].is_boolean());
    BOOST_CHECK_EQUAL(result["ismine"].get<bool>(), true);

    // For owned key addresses: isscript, pubkey, iscompressed
    BOOST_CHECK(result["isscript"].is_boolean());
    BOOST_CHECK(result["pubkey"].is_string());
    BOOST_CHECK(result["iscompressed"].is_boolean());
}

// ---------------------------------------------------------------------------
// validateaddress — invalid address returns isvalid=false
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validateaddress_invalid_response_contract)
{
    json p = json::array();
    p.push_back(string("INVALID_ADDRESS"));
    json result = validateaddress(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["isvalid"].is_boolean());
    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), false);
}

// ---------------------------------------------------------------------------
// validatepubkey — param: hex pubkey, returns object
// Fields: "isvalid"(bool), optionally "address"(str), "ismine"(bool),
//         "iscompressed"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validatepubkey_response_contract)
{
    // Get a hex pubkey via getnewpubkey
    json pPub = json::array();
    string hexPubKey = getnewpubkey(pPub, false).get<string>();

    json p = json::array();
    p.push_back(hexPubKey);
    json result = validatepubkey(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["isvalid"].is_boolean());
    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), true);
    BOOST_CHECK(result["address"].is_string());
    BOOST_CHECK(result["ismine"].is_boolean());
    BOOST_CHECK(result["iscompressed"].is_boolean());
}

// ---------------------------------------------------------------------------
// gettransaction — uses confirmed coinbase from mined blocks
// Returns object with TxToJSON fields + "amount", "details", WalletTxToJSON
// Coinbase txs also have "generated"(bool), "blockhash", "blockindex", "blocktime"
// Note: TestChain uses anyone-can-spend coinbases (empty scriptPubKey), so
// mapWallet is empty by default. We manually add a CWalletTx from a known
// coinbase to the wallet before querying.
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(gettransaction_response_contract)
{
    // Add a confirmed coinbase tx to the wallet so gettransaction can find it
    BOOST_REQUIRE(!coinbaseTxns.empty());
    CWalletTx wtx(pwalletMain, coinbaseTxns[0]);
    wtx.hashBlock = blockIndexAt(nBaseHeight + 1)->GetBlockHash();
    wtx.nIndex = 0;  // coinbase is always tx index 0 in block
    pwalletMain->AddToWallet(wtx);

    uint256 txid = coinbaseTxns[0].GetHash();

    json p = json::array();
    p.push_back(txid.GetHex());
    json result = gettransaction(p, false);
    BOOST_CHECK(result.is_object());

    // TxToJSON fields
    BOOST_CHECK(result["txid"].is_string());
    BOOST_CHECK_EQUAL(result["txid"].get<string>(), txid.GetHex());
    BOOST_CHECK(result["version"].is_number_integer());
    BOOST_CHECK(result["time"].is_number_integer());
    BOOST_CHECK(result["locktime"].is_number_integer());
    BOOST_CHECK(result["vin"].is_array());
    BOOST_CHECK(result["vout"].is_array());

    // WalletTxToJSON fields — confirmed coinbase has all of these
    BOOST_CHECK(result["confirmations"].is_number_integer());
    BOOST_CHECK(result["confirmations"].get<int>() > 0);
    BOOST_CHECK(result["blockhash"].is_string());
    BOOST_CHECK_EQUAL(result["blockhash"].get<string>().size(), 64u);
    BOOST_CHECK(result["blockindex"].is_number_integer());
    BOOST_CHECK(result["blocktime"].is_number_integer());
    BOOST_CHECK(result["timereceived"].is_number_integer());

    // Coinbase-specific: "generated" field
    BOOST_CHECK(result["generated"].is_boolean());
    BOOST_CHECK_EQUAL(result["generated"].get<bool>(), true);

    // amount field
    BOOST_CHECK(result["amount"].is_number_float());

    // details array
    BOOST_CHECK(result["details"].is_array());
}

// ---------------------------------------------------------------------------
// listsinceblock — returns object with "transactions" and "lastblock"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listsinceblock_response_contract)
{
    json p = json::array();
    json result = listsinceblock(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["transactions"].is_array());
    BOOST_CHECK(result["lastblock"].is_string());

    // lastblock should be a 64-char hex hash
    string lastblock = result["lastblock"].get<string>();
    BOOST_CHECK_EQUAL(lastblock.size(), 64u);
}

// ---------------------------------------------------------------------------
// reservebalance — no params returns object with "reserve" and "amount"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(reservebalance_response_contract)
{
    json p = json::array();
    json result = reservebalance(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["reserve"].is_boolean());
    BOOST_CHECK(result["amount"].is_number_float());
    // Default: no reserve
    BOOST_CHECK_EQUAL(result["reserve"].get<bool>(), false);
    BOOST_CHECK(result["amount"].get<double>() >= 0.0);
}

// ---------------------------------------------------------------------------
// checkwallet — returns object with "wallet check passed"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(checkwallet_response_contract)
{
    json p = json::array();
    json result = checkwallet(p, false);
    BOOST_CHECK(result.is_object());

    // When wallet is healthy: "wallet check passed" = true
    BOOST_CHECK(result["wallet check passed"].is_boolean());
    BOOST_CHECK_EQUAL(result["wallet check passed"].get<bool>(), true);
}

// ---------------------------------------------------------------------------
// repairwallet — returns object with same structure as checkwallet
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(repairwallet_response_contract)
{
    json p = json::array();
    json result = repairwallet(p, false);
    BOOST_CHECK(result.is_object());

    // When wallet is healthy: "wallet check passed" = true
    BOOST_CHECK(result["wallet check passed"].is_boolean());
    BOOST_CHECK_EQUAL(result["wallet check passed"].get<bool>(), true);
}

// ---------------------------------------------------------------------------
// makekeypair — returns object with "PrivateKey" and "PublicKey"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(makekeypair_response_contract)
{
    json p = json::array();
    json result = makekeypair(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["PrivateKey"].is_string());
    BOOST_CHECK(result["PublicKey"].is_string());

    string privKey = result["PrivateKey"].get<string>();
    string pubKey = result["PublicKey"].get<string>();
    BOOST_CHECK(!privKey.empty());
    BOOST_CHECK(!pubKey.empty());
    BOOST_CHECK(IsHex(privKey));
    BOOST_CHECK(IsHex(pubKey));
}

// ---------------------------------------------------------------------------
// combinethreshold — no params returns object with "combine threshold"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(combinethreshold_response_contract)
{
    json p = json::array();
    json result = combinethreshold(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["combine threshold"].is_number_integer());
}

// ---------------------------------------------------------------------------
// splitthreshold — no params returns object with "split threshold"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(splitthreshold_response_contract)
{
    json p = json::array();
    json result = splitthreshold(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["split threshold"].is_number_integer());
}

// ---------------------------------------------------------------------------
// getstakesplitthreshold — returns object with "split threshold"(real)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakesplitthreshold_response_contract)
{
    json p = json::array();
    json result = getstakesplitthreshold(p, false);
    BOOST_CHECK(result.is_object());

    // Uses ValueFromAmount, so it's float
    BOOST_CHECK(result["split threshold"].is_number_float());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_blockchain — blockchain RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_blockchain, TestChain)

// ---------------------------------------------------------------------------
// getblockcount — returns integer equal to nBestHeight
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblockcount_response_contract)
{
    json p = json::array();
    json result = getblockcount(p, false);
    BOOST_CHECK(result.is_number_integer());
    BOOST_CHECK_EQUAL(result.get<int>(), nBestHeight);
}

// ---------------------------------------------------------------------------
// getbestblockhash — returns string, 64 hex characters
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getbestblockhash_response_contract)
{
    json p = json::array();
    json result = getbestblockhash(p, false);
    BOOST_CHECK(result.is_string());
    string hash = result.get<string>();
    BOOST_CHECK_EQUAL(hash.size(), 64u);
    BOOST_CHECK(IsHex(hash));
}

// ---------------------------------------------------------------------------
// getblockhash — param: height 0, returns string, 64 hex chars
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblockhash_response_contract)
{
    json p = json::array();
    p.push_back(0);
    json result = getblockhash(p, false);
    BOOST_CHECK(result.is_string());
    string hash = result.get<string>();
    BOOST_CHECK_EQUAL(hash.size(), 64u);
    BOOST_CHECK(IsHex(hash));
}

// ---------------------------------------------------------------------------
// getblock — param: genesis hash; verify field types from blockToJSON
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_response_contract)
{
    // Get genesis hash
    json pH = json::array();
    pH.push_back(0);
    string genesisHash = getblockhash(pH, false).get<string>();

    json p = json::array();
    p.push_back(genesisHash);
    json result = getblock(p, false);
    BOOST_CHECK(result.is_object());

    // String fields
    BOOST_CHECK(result["hash"].is_string());
    BOOST_CHECK(result["merkleroot"].is_string());
    BOOST_CHECK(result["bits"].is_string());

    // Integer fields
    BOOST_CHECK(result["confirmations"].is_number_integer());
    BOOST_CHECK(result["size"].is_number_integer());
    BOOST_CHECK(result["height"].is_number_integer());
    BOOST_CHECK(result["version"].is_number_integer());
    BOOST_CHECK(result["time"].is_number_integer());
    BOOST_CHECK(result["nonce"].is_number_integer());

    // Real (double) fields
    BOOST_CHECK(result["difficulty"].is_number_float());
    BOOST_CHECK(result["mint"].is_number_float());

    // Array fields
    BOOST_CHECK(result["tx"].is_array());

    // Additional blockToJSON fields
    BOOST_CHECK(result["blocktrust"].is_string());
    BOOST_CHECK(result["chaintrust"].is_string());
    BOOST_CHECK(result["flags"].is_string());
    BOOST_CHECK(result["proofhash"].is_string());
    BOOST_CHECK(result["entropybit"].is_number_integer());
    BOOST_CHECK(result["modifier"].is_string());
}

// ---------------------------------------------------------------------------
// getblockbynumber — param: 0; same fields as getblock
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblockbynumber_response_contract)
{
    json p = json::array();
    p.push_back(0);
    json result = getblockbynumber(p, false);
    BOOST_CHECK(result.is_object());

    // Verify same core fields as getblock
    BOOST_CHECK(result["hash"].is_string());
    BOOST_CHECK(result["confirmations"].is_number_integer());
    BOOST_CHECK(result["size"].is_number_integer());
    BOOST_CHECK(result["height"].is_number_integer());
    BOOST_CHECK(result["version"].is_number_integer());
    BOOST_CHECK(result["merkleroot"].is_string());
    BOOST_CHECK(result["time"].is_number_integer());
    BOOST_CHECK(result["nonce"].is_number_integer());
    BOOST_CHECK(result["bits"].is_string());
    BOOST_CHECK(result["difficulty"].is_number_float());
    BOOST_CHECK(result["tx"].is_array());
}

// ---------------------------------------------------------------------------
// getcheckpoint — returns object with field contracts
// Fields: "synccheckpoint"(str), "height"(int), "timestamp"(str), "policy"(str)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getcheckpoint_response_contract)
{
    json p = json::array();
    json result = getcheckpoint(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["synccheckpoint"].is_string());
    BOOST_CHECK(result["height"].is_number_integer());
    BOOST_CHECK(result["timestamp"].is_string());
    // "policy" is always present (one of strict/advisory/permissive)
    BOOST_CHECK(result["policy"].is_string());
}

// ---------------------------------------------------------------------------
// getblock — genesis block hash pinned to hashGenesisBlock
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_genesis_hash_pinned)
{
    json pH = json::array();
    pH.push_back(0);
    string genesisHash = getblockhash(pH, false).get<string>();

    json p = json::array();
    p.push_back(genesisHash);
    json result = getblock(p, false);

    // Pin the genesis block hash exactly
    BOOST_CHECK_EQUAL(result["hash"].get<string>(), hashGenesisBlock.GetHex());
    BOOST_CHECK_EQUAL(result["height"].get<int>(), 0);
}

// ---------------------------------------------------------------------------
// getblock — genesis block version == 1
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_height_zero_version)
{
    json p = json::array();
    p.push_back(0);
    json result = getblockbynumber(p, false);

    BOOST_CHECK_EQUAL(result["version"].get<int>(), 1);
}

// ---------------------------------------------------------------------------
// getblock — genesis tx array is non-empty (has at least 1 tx)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_tx_array_nonempty)
{
    json p = json::array();
    p.push_back(0);
    json result = getblockbynumber(p, false);

    BOOST_CHECK(!result["tx"].empty());
}

// ---------------------------------------------------------------------------
// getblock — genesis confirmations > 0
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_confirmations_positive)
{
    json p = json::array();
    p.push_back(0);
    json result = getblockbynumber(p, false);

    BOOST_CHECK(result["confirmations"].get<int>() > 0);
}

// ---------------------------------------------------------------------------
// getblockcount — matches TestChain::chainHeight()
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblockcount_matches_chain_height)
{
    json p = json::array();
    json result = getblockcount(p, false);
    BOOST_CHECK_EQUAL(result.get<int>(), chainHeight());
}

// ---------------------------------------------------------------------------
// getbestblockhash — matches pindexBest->GetBlockHash()
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getbestblockhash_matches_tip)
{
    json p = json::array();
    json result = getbestblockhash(p, false);
    BOOST_CHECK_EQUAL(result.get<string>(), pindexBest->GetBlockHash().GetHex());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_network — network RPC response contracts
// ============================================================================

BOOST_AUTO_TEST_SUITE(rpc_response_network)

// ---------------------------------------------------------------------------
// getconnectioncount — returns integer, >= 0
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getconnectioncount_response_contract)
{
    json p = json::array();
    json result = getconnectioncount(p, false);
    BOOST_CHECK(result.is_number_integer());
    BOOST_CHECK(result.get<int>() >= 0);
}

// ---------------------------------------------------------------------------
// getpeerinfo — returns array (may be empty in test environment)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getpeerinfo_response_contract)
{
    json p = json::array();
    json result = getpeerinfo(p, false);
    BOOST_CHECK(result.is_array());
}

// ---------------------------------------------------------------------------
// getnodes — returns string (addnode= lines, empty in test environment)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnodes_response_contract)
{
    json p = json::array();
    json result = getnodes(p, false);
    // getnodes returns a string, not object/array
    BOOST_CHECK(result.is_string());
}

// ---------------------------------------------------------------------------
// getconnectioncount — is zero in test environment (no real peers)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getconnectioncount_is_zero_in_test)
{
    json p = json::array();
    json result = getconnectioncount(p, false);
    BOOST_CHECK_EQUAL(result.get<int>(), 0);
}

// ---------------------------------------------------------------------------
// getpeerinfo — empty in test environment (no real peers)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getpeerinfo_empty_in_test)
{
    json p = json::array();
    json result = getpeerinfo(p, false);
    BOOST_CHECK(result.empty());
}

// ---------------------------------------------------------------------------
// getpeerinfo — help mode throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getpeerinfo_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getpeerinfo(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getconnectioncount — help mode throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getconnectioncount_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getconnectioncount(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getnodes — help mode throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnodes_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getnodes(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_raw — raw transaction RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_raw, TestChain)

// ---------------------------------------------------------------------------
// createrawtransaction — returns string (hex-encoded raw tx)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(createrawtransaction_response_contract)
{
    // Create a raw transaction with empty inputs and a valid output
    json inputs = json::array();
    json sendTo = json::object();
    json addrParams = json::array();
    string addr = getnewaddress(addrParams, false).get<string>();
    sendTo[addr] = 0.01;

    json p = json::array();
    p.push_back(inputs);
    p.push_back(sendTo);
    json result = createrawtransaction(p, false);
    BOOST_CHECK(result.is_string());
    BOOST_CHECK(IsHex(result.get<string>()));
}

// ---------------------------------------------------------------------------
// decoderawtransaction — returns object with TxToJSON fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decoderawtransaction_response_contract)
{
    json inputs = json::array();
    json sendTo = json::object();
    json addrParams = json::array();
    string addr = getnewaddress(addrParams, false).get<string>();
    sendTo[addr] = 0.01;

    json pCreate = json::array();
    pCreate.push_back(inputs);
    pCreate.push_back(sendTo);
    string rawHex = createrawtransaction(pCreate, false).get<string>();

    json pDecode = json::array();
    pDecode.push_back(rawHex);
    json result = decoderawtransaction(pDecode, false);
    BOOST_CHECK(result.is_object());

    // TxToJSON fields
    BOOST_CHECK(result["txid"].is_string());
    BOOST_CHECK(result["version"].is_number_integer());
    BOOST_CHECK(result["time"].is_number_integer());
    BOOST_CHECK(result["locktime"].is_number_integer());
    BOOST_CHECK(result["vin"].is_array());
    BOOST_CHECK(result["vout"].is_array());
}

// ---------------------------------------------------------------------------
// decoderawtransaction — Pinkcoin-specific nTime field present
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decoderawtransaction_has_ntime)
{
    json inputs = json::array();
    json sendTo = json::object();
    json addrParams = json::array();
    string addr = getnewaddress(addrParams, false).get<string>();
    sendTo[addr] = 0.01;

    json pCreate = json::array();
    pCreate.push_back(inputs);
    pCreate.push_back(sendTo);
    string rawHex = createrawtransaction(pCreate, false).get<string>();

    json pDecode = json::array();
    pDecode.push_back(rawHex);
    json result = decoderawtransaction(pDecode, false);

    // nTime is Pinkcoin-specific (not in Bitcoin Core)
    BOOST_CHECK(result["time"].is_number_integer());
    BOOST_CHECK(result["time"].get<int64_t>() >= 0);
}

// ---------------------------------------------------------------------------
// decoderawtransaction — vout scriptPubKey sub-object fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decoderawtransaction_vout_fields)
{
    json inputs = json::array();
    json sendTo = json::object();
    json addrParams = json::array();
    string addr = getnewaddress(addrParams, false).get<string>();
    sendTo[addr] = 0.01;

    json pCreate = json::array();
    pCreate.push_back(inputs);
    pCreate.push_back(sendTo);
    string rawHex = createrawtransaction(pCreate, false).get<string>();

    json pDecode = json::array();
    pDecode.push_back(rawHex);
    json result = decoderawtransaction(pDecode, false);

    const json& vout = result["vout"];
    BOOST_REQUIRE(!vout.empty());
    const json& out0 = vout[0];

    BOOST_CHECK(out0["value"].is_number_float());
    BOOST_CHECK(out0["n"].is_number_integer());

    const json& spk = out0["scriptPubKey"];
    BOOST_CHECK(spk["asm"].is_string());
    BOOST_CHECK(spk["type"].is_string());
}

// ---------------------------------------------------------------------------
// decodescript — returns object with asm, type, p2sh
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decodescript_response_contract)
{
    json p = json::array();
    p.push_back(string("76a91489abcdefabbaabbaabbaabbaabbaabbaabbaabba88ac"));
    json result = decodescript(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["asm"].is_string());
    BOOST_CHECK(result["type"].is_string());
    BOOST_CHECK(result["p2sh"].is_string());
}

// ---------------------------------------------------------------------------
// decodescript — P2PKH type detection
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decodescript_p2pkh_type)
{
    json p = json::array();
    p.push_back(string("76a91489abcdefabbaabbaabbaabbaabbaabbaabbaabba88ac"));
    json result = decodescript(p, false);
    BOOST_CHECK_EQUAL(result["type"].get<string>(), "pubkeyhash");
}

// ---------------------------------------------------------------------------
// decodescript — P2SH address starts with "C" (Pinkcoin SCRIPT_ADDRESS=28)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decodescript_p2sh_prefix)
{
    json p = json::array();
    p.push_back(string("76a91489abcdefabbaabbaabbaabbaabbaabbaabbaabba88ac"));
    json result = decodescript(p, false);
    string p2sh = result["p2sh"].get<string>();
    BOOST_CHECK(!p2sh.empty());
    BOOST_CHECK_EQUAL(p2sh[0], 'C');
}

// ---------------------------------------------------------------------------
// makekeypair — returns obj with PrivateKey, PublicKey (both hex)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(makekeypair_response_contract)
{
    json p = json::array();
    json result = makekeypair(p, false);
    BOOST_CHECK(result.is_object());

    BOOST_CHECK(result["PrivateKey"].is_string());
    BOOST_CHECK(result["PublicKey"].is_string());
    BOOST_CHECK(IsHex(result["PrivateKey"].get<string>()));
    BOOST_CHECK(IsHex(result["PublicKey"].get<string>()));
}

// ---------------------------------------------------------------------------
// makekeypair — two calls produce different keys
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(makekeypair_unique_each_call)
{
    json p = json::array();
    json obj1 = makekeypair(p, false);
    json obj2 = makekeypair(p, false);
    BOOST_CHECK(obj1["PrivateKey"].get<string>() != obj2["PrivateKey"].get<string>());
    BOOST_CHECK(obj1["PublicKey"].get<string>() != obj2["PublicKey"].get<string>());
}

// ---------------------------------------------------------------------------
// createrawtransaction — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(createrawtransaction_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(createrawtransaction(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// decoderawtransaction — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decoderawtransaction_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(decoderawtransaction(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_mining — mining RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_mining, TestChain)

// ---------------------------------------------------------------------------
// getsubsidy — returns integer (proof-of-work subsidy)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getsubsidy_response_contract)
{
    json p = json::array();
    json result = getsubsidy(p, false);
    BOOST_CHECK(result.is_number_integer());
    BOOST_CHECK(result.get<int64_t>() >= 0);
}

// ---------------------------------------------------------------------------
// getsubsidy — with explicit height param
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getsubsidy_with_height_param)
{
    json p = json::array();
    // getsubsidy takes string param (uses atoi)
    p.push_back(string("100"));
    json result = getsubsidy(p, false);
    BOOST_CHECK(result.is_number_integer());
    // PoW subsidy may be zero if PoW is disabled at this height
    BOOST_CHECK(result.get<int64_t>() >= 0);
}

// NOTE: getstakinginfo and getmininginfo response contracts are in rpc_response_info suite
// (lines 84-157). Only mining-specific tests below.

// ---------------------------------------------------------------------------
// getmininginfo — blocks matches nBestHeight
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getmininginfo_blocks_matches_height)
{
    json p = json::array();
    json result = getmininginfo(p, false);
    BOOST_CHECK_EQUAL(result["blocks"].get<int>(), nBestHeight);
}

// ---------------------------------------------------------------------------
// getstakinginfo — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakinginfo_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getstakinginfo(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getmininginfo — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getmininginfo_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getmininginfo(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getwork — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getwork_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getwork(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getblocktemplate — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblocktemplate_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(getblocktemplate(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_smessage — secure messaging RPC response contracts
// ============================================================================

// RAII guard to save/restore fSecMsgenabled state
struct SmsgGuard {
    bool savedState;
    SmsgGuard() : savedState(fSecMsgenabled) {}
    ~SmsgGuard() {
        if (fSecMsgenabled != savedState) {
            if (savedState) {
                json p = json::array();
                try { smsgenable(p, false); } catch (...) {}
            } else {
                json p = json::array();
                try { smsgdisable(p, false); } catch (...) {}
            }
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(rpc_response_smessage, TestChain)

// ---------------------------------------------------------------------------
// smsgoptions — returns object with option fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgoptions_response_contract)
{
    json p = json::array();
    p.push_back(string("list"));
    json result = smsgoptions(p, false);
    BOOST_CHECK(result.is_object());
    BOOST_CHECK(result["result"].is_string());
}

// ---------------------------------------------------------------------------
// smsgenable — returns obj with "result" field
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgenable_response_contract)
{
    SmsgGuard guard;
    if (fSecMsgenabled) {
        json pDisable = json::array();
        smsgdisable(pDisable, false);
    }

    json p = json::array();
    json result = smsgenable(p, false);
    BOOST_CHECK(result.is_object());
    BOOST_CHECK(result["result"].is_string());
    BOOST_CHECK_EQUAL(result["result"].get<string>(), "Enabled secure messaging.");
}

// ---------------------------------------------------------------------------
// smsgdisable — returns obj with "result" field
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgdisable_response_contract)
{
    SmsgGuard guard;
    if (!fSecMsgenabled) {
        json pEnable = json::array();
        smsgenable(pEnable, false);
    }

    json p = json::array();
    json result = smsgdisable(p, false);
    BOOST_CHECK(result.is_object());
    BOOST_CHECK(result["result"].is_string());
    BOOST_CHECK_EQUAL(result["result"].get<string>(), "Disabled secure messaging.");
}

// ---------------------------------------------------------------------------
// smsgenable — already enabled throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgenable_already_enabled_throws)
{
    SmsgGuard guard;
    if (!fSecMsgenabled) {
        json pEnable = json::array();
        smsgenable(pEnable, false);
    }
    json p = json::array();
    BOOST_CHECK_THROW(smsgenable(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// smsgdisable — already disabled throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgdisable_already_disabled_throws)
{
    SmsgGuard guard;
    if (fSecMsgenabled) {
        json pDisable = json::array();
        smsgdisable(pDisable, false);
    }
    json p = json::array();
    BOOST_CHECK_THROW(smsgdisable(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// smsgoptions — set with too few params returns error result
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgoptions_set_too_few_params)
{
    json p = json::array();
    p.push_back(string("set"));
    json result = smsgoptions(p, false);
    BOOST_CHECK_EQUAL(result["result"].get<string>(), "Too few parameters.");
    BOOST_CHECK(result["expected"].is_string());
}

// ---------------------------------------------------------------------------
// smsgbuckets — requires smsg enabled, returns obj
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgbuckets_response_contract)
{
    SmsgGuard guard;
    if (!fSecMsgenabled) {
        json pEnable = json::array();
        smsgenable(pEnable, false);
    }

    json p = json::array();
    json result = smsgbuckets(p, false);
    BOOST_CHECK(result.is_object());
}

// ---------------------------------------------------------------------------
// smsglocalkeys — requires smsg enabled, returns obj
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsglocalkeys_response_contract)
{
    SmsgGuard guard;
    if (!fSecMsgenabled) {
        json pEnable = json::array();
        smsgenable(pEnable, false);
    }

    json p = json::array();
    json result = smsglocalkeys(p, false);
    BOOST_CHECK(result.is_object());
}

// ---------------------------------------------------------------------------
// smsglocalkeys — disabled throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsglocalkeys_disabled_throws)
{
    SmsgGuard guard;
    if (fSecMsgenabled) {
        json pDisable = json::array();
        smsgdisable(pDisable, false);
    }
    json p = json::array();
    BOOST_CHECK_THROW(smsglocalkeys(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// smsgenable — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgenable_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgenable(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// smsgdisable — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgdisable_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgdisable(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// smsgoptions — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(smsgoptions_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgoptions(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_errors — RPC error envelope & code contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_errors, TestChain)

// ---------------------------------------------------------------------------
// JSONRPCError envelope: {code: int, message: str}
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(error_envelope_structure)
{
    json err = JSONRPCError(RPC_MISC_ERROR, "test error");
    BOOST_CHECK(err["code"].is_number_integer());
    BOOST_CHECK(err["message"].is_string());
    BOOST_CHECK_EQUAL(err["code"].get<int>(), -1);
    BOOST_CHECK_EQUAL(err["message"].get<string>(), "test error");
}

// ---------------------------------------------------------------------------
// Pin RPCErrorCode enum values (consensus for RPC clients)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(rpc_error_code_values_pinned)
{
    BOOST_CHECK_EQUAL(RPC_INVALID_REQUEST,  -32600);
    BOOST_CHECK_EQUAL(RPC_METHOD_NOT_FOUND, -32601);
    BOOST_CHECK_EQUAL(RPC_INVALID_PARAMS,   -32602);
    BOOST_CHECK_EQUAL(RPC_PARSE_ERROR,      -32700);

    BOOST_CHECK_EQUAL(RPC_MISC_ERROR,                  -1);
    BOOST_CHECK_EQUAL(RPC_INVALID_ADDRESS_OR_KEY,      -5);
    BOOST_CHECK_EQUAL(RPC_INVALID_PARAMETER,           -8);

    BOOST_CHECK_EQUAL(RPC_WALLET_ERROR,                -4);
    BOOST_CHECK_EQUAL(RPC_WALLET_INSUFFICIENT_FUNDS,   -6);
    BOOST_CHECK_EQUAL(RPC_WALLET_INVALID_ACCOUNT_NAME, -11);
    BOOST_CHECK_EQUAL(RPC_WALLET_KEYPOOL_RAN_OUT,      -12);
    BOOST_CHECK_EQUAL(RPC_WALLET_UNLOCK_NEEDED,        -13);
    BOOST_CHECK_EQUAL(RPC_WALLET_PASSPHRASE_INCORRECT, -14);
    BOOST_CHECK_EQUAL(RPC_WALLET_WRONG_ENC_STATE,      -15);
    BOOST_CHECK_EQUAL(RPC_WALLET_ENCRYPTION_FAILED,    -16);
    BOOST_CHECK_EQUAL(RPC_WALLET_ALREADY_UNLOCKED,     -17);
}

// ---------------------------------------------------------------------------
// Invalid address throws Object with RPC_INVALID_ADDRESS_OR_KEY
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validateaddress_invalid_does_not_throw)
{
    // validateaddress doesn't throw on invalid — it returns isvalid=false
    json p = json::array();
    p.push_back(string("notanaddress"));
    json result = validateaddress(p, false);
    BOOST_CHECK_EQUAL(result["isvalid"].get<bool>(), false);
}

// ---------------------------------------------------------------------------
// createrawtransaction — invalid address throws json (RPC_INVALID_ADDRESS_OR_KEY)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(createrawtx_invalid_address_throws)
{
    json inputs = json::array();
    json sendTo = json::object();
    sendTo["notavalidaddress"] = 0.01;

    json p = json::array();
    p.push_back(inputs);
    p.push_back(sendTo);

    BOOST_CHECK_THROW(createrawtransaction(p, false), json);
}

// ---------------------------------------------------------------------------
// createrawtransaction — invalid address error code is RPC_INVALID_ADDRESS_OR_KEY
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(createrawtx_invalid_address_error_code)
{
    json inputs = json::array();
    json sendTo = json::object();
    sendTo["notavalidaddress"] = 0.01;

    json p = json::array();
    p.push_back(inputs);
    p.push_back(sendTo);

    try {
        createrawtransaction(p, false);
        BOOST_FAIL("Should have thrown");
    } catch (json& err) {
        BOOST_CHECK_EQUAL(err["code"].get<int>(), RPC_INVALID_ADDRESS_OR_KEY);
        BOOST_CHECK(err["message"].get<string>().find("Invalid") != string::npos);
    }
}

// ---------------------------------------------------------------------------
// decoderawtransaction — invalid hex throws json (RPC_DESERIALIZATION_ERROR)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decoderawtx_invalid_hex_throws)
{
    json p = json::array();
    p.push_back(string("zzzz"));
    // Non-hex string causes ParseHex to return empty, then deserialization fails
    BOOST_CHECK_THROW(decoderawtransaction(p, false), json);
}

// ---------------------------------------------------------------------------
// createrawtransaction — wrong param types throws json (RPC_TYPE_ERROR)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(createrawtx_wrong_param_type_throws)
{
    json p = json::array();
    p.push_back(string("notanarray"));
    p.push_back(string("notanobject"));

    BOOST_CHECK_THROW(createrawtransaction(p, false), json);
}

// ---------------------------------------------------------------------------
// sendtoaddress — missing params throws runtime_error (help text)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sendtoaddress_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendtoaddress(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// sendfrom — missing params throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(sendfrom_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendfrom(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// signmessage — missing params throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(signmessage_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(signmessage(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// verifymessage — missing params throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(verifymessage_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(verifymessage(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// dumpprivkey — missing params throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(dumpprivkey_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(dumpprivkey(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// importprivkey — missing params throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(importprivkey_missing_params_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(importprivkey(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// getblock — invalid hash throws json
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblock_invalid_hash_throws)
{
    json p = json::array();
    p.push_back(string("0000000000000000000000000000000000000000000000000000000000000000"));
    BOOST_CHECK_THROW(getblock(p, false), json);
}

// ---------------------------------------------------------------------------
// getblockhash — out of range height throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getblockhash_out_of_range_throws)
{
    json p = json::array();
    p.push_back(999999999);
    BOOST_CHECK_THROW(getblockhash(p, false), runtime_error);
}

// ---------------------------------------------------------------------------
// decodescript — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(decodescript_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(decodescript(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// listunspent — help throws runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listunspent_help_works)
{
    json p = json::array();
    BOOST_CHECK_THROW(listunspent(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
