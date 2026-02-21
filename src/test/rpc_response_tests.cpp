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
#include "test_framework.h"

using namespace json_spirit;
using namespace std;

extern CWallet* pwalletMain;
extern Value help(const Array& params, bool fHelp);

// ============================================================================
// Suite: rpc_response_info — info & status RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_info, TestChain)

// ---------------------------------------------------------------------------
// getinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getinfo_response_contract)
{
    Array p;
    Value result = getinfo(p, false);
    Object obj = result.get_obj();

    // String fields
    BOOST_CHECK(find_value(obj, "version").type() == str_type);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);

    // Integer fields
    BOOST_CHECK(find_value(obj, "protocolversion").type() == int_type);
    BOOST_CHECK_EQUAL(find_value(obj, "protocolversion").get_int(), 60019);
    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "connections").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);

    BOOST_CHECK(find_value(obj, "timeoffset").type() == int_type);

    // String fields (additional)
    BOOST_CHECK(find_value(obj, "offsetfrom").type() == str_type);
    BOOST_CHECK(find_value(obj, "proxy").type() == str_type);
    BOOST_CHECK(find_value(obj, "ip").type() == str_type);

    // Real (double) fields — ValueFromAmount returns double
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "newmint").type() == real_type);
    BOOST_CHECK(find_value(obj, "stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "moneysupply").type() == real_type);
    BOOST_CHECK(find_value(obj, "paytxfee").type() == real_type);
    BOOST_CHECK(find_value(obj, "mininput").type() == real_type);

    // Boolean fields
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "testnet").get_bool(), false);

    // Nested object: difficulty
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    Object diff = find_value(obj, "difficulty").get_obj();
    BOOST_CHECK(find_value(diff, "proof-of-work").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake (flash)").type() == real_type);
}

// ---------------------------------------------------------------------------
// getmininginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getmininginfo_response_contract)
{
    Array p;
    Value result = getmininginfo(p, false);
    Object obj = result.get_obj();

    // Core fields always present
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "next-block-value-pos").type() == real_type);
    BOOST_CHECK(find_value(obj, "last-block-size").type() == int_type);
    BOOST_CHECK(find_value(obj, "last-block-tx").type() == int_type);
    BOOST_CHECK(find_value(obj, "pooledtx").type() == int_type);
    BOOST_CHECK(find_value(obj, "tx-fee").type() == real_type);

    // Staking sub-object
    BOOST_CHECK(find_value(obj, "staking").type() == obj_type);
    Object staking = find_value(obj, "staking").get_obj();
    BOOST_CHECK(find_value(staking, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(staking, "targeting-fpos").type() == bool_type);
    BOOST_CHECK(find_value(staking, "estimated-time").type() == int_type);
    BOOST_CHECK(find_value(staking, "search-interval").type() == int_type);
    BOOST_CHECK(find_value(staking, "utxo-combine-threshold").type() == int_type);
    BOOST_CHECK(find_value(staking, "utxo-split-threshold").type() == int_type);

    // Stake weight sub-object
    BOOST_CHECK(find_value(obj, "stakeweight").type() == obj_type);
    Object sw = find_value(obj, "stakeweight").get_obj();
    BOOST_CHECK(find_value(sw, "minimum").type() == int_type);
    BOOST_CHECK(find_value(sw, "maximum").type() == int_type);
    BOOST_CHECK(find_value(sw, "combined").type() == int_type);
    BOOST_CHECK(find_value(sw, "network").type() == int_type);

    // Difficulty sub-object
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    Object diff = find_value(obj, "difficulty").get_obj();
    BOOST_CHECK(find_value(diff, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake(flash)").type() == real_type);

    // Top-level scalars
    BOOST_CHECK(find_value(obj, "netstakeweight").type() == int_type);
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "testnet").get_bool(), false);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);
}

// ---------------------------------------------------------------------------
// getstakinginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakinginfo_response_contract)
{
    Array p;
    Value result = getstakinginfo(p, false);
    Object obj = result.get_obj();

    // Boolean fields
    BOOST_CHECK(find_value(obj, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(obj, "staking").type() == bool_type);

    // String fields
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);

    // Integer fields (uint64_t → int_type in json_spirit)
    BOOST_CHECK(find_value(obj, "currentblocksize").type() == int_type);
    BOOST_CHECK(find_value(obj, "currentblocktx").type() == int_type);
    BOOST_CHECK(find_value(obj, "pooledtx").type() == int_type);
    BOOST_CHECK(find_value(obj, "search-interval").type() == int_type);
    BOOST_CHECK(find_value(obj, "weight").type() == int_type);
    BOOST_CHECK(find_value(obj, "netstakeweight").type() == int_type);
    BOOST_CHECK(find_value(obj, "expectedtime").type() == int_type);

    // Real (double) fields
    BOOST_CHECK(find_value(obj, "difficulty").type() == real_type);
    BOOST_CHECK(find_value(obj, "difficulty (flash)").type() == real_type);
}

// ---------------------------------------------------------------------------
// getdifficulty — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getdifficulty_response_contract)
{
    Array p;
    Value result = getdifficulty(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "proof-of-work").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake (flash)").type() == real_type);
    BOOST_CHECK(find_value(obj, "search-interval").type() == int_type);
}

// ---------------------------------------------------------------------------
// getsubsidy — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getsubsidy_response_contract)
{
    Array p;
    Value result = getsubsidy(p, false);

    // getsubsidy returns a bare uint64_t (block subsidy in satoshis)
    BOOST_CHECK(result.type() == int_type);
    // At test chain height (~51), PoW subsidy is 0 — only block 1 and
    // heights >= 17000 have non-zero PoW rewards. Verify non-negative.
    BOOST_CHECK(result.get_int64() >= 0);
}

// ---------------------------------------------------------------------------
// getwalletinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getwalletinfo_response_contract)
{
    Array p;
    Value result = getwalletinfo(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "txcount").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);

    // "unlocked_until" only present when wallet is encrypted — not guaranteed
    // in test environment, so we do not assert its presence.
}

// ---------------------------------------------------------------------------
// getloglevel — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getloglevel_response_contract)
{
    Array p;
    Value result = getloglevel(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "level").type() == str_type);
    BOOST_CHECK(find_value(obj, "categories").type() == array_type);
}

// ---------------------------------------------------------------------------
// help — returns non-empty string
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_returns_string)
{
    Array p;
    Value result = help(p, false);

    BOOST_CHECK(result.type() == str_type);
    BOOST_CHECK(!result.get_str().empty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite: rpc_response_wallet — wallet RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_wallet, TestChain)

// ---------------------------------------------------------------------------
// getnewaddress — returns str_type, Pinkcoin address starts with "2"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnewaddress_response_contract)
{
    Array p;
    Value result = getnewaddress(p, false);
    BOOST_CHECK(result.type() == str_type);
    string addr = result.get_str();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');  // Pinkcoin PUBKEY_ADDRESS prefix
}

// ---------------------------------------------------------------------------
// getnewpubkey — returns str_type (hex-encoded public key)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getnewpubkey_response_contract)
{
    Array p;
    Value result = getnewpubkey(p, false);
    BOOST_CHECK(result.type() == str_type);
    string hexPubKey = result.get_str();
    BOOST_CHECK(!hexPubKey.empty());
    // Compressed pubkey = 66 hex chars, uncompressed = 130 hex chars
    BOOST_CHECK(hexPubKey.size() == 66 || hexPubKey.size() == 130);
    BOOST_CHECK(IsHex(hexPubKey));
}

// ---------------------------------------------------------------------------
// getaccountaddress — param: "" (default account), returns str_type
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaccountaddress_response_contract)
{
    Array p;
    p.push_back(string(""));  // default account
    Value result = getaccountaddress(p, false);
    BOOST_CHECK(result.type() == str_type);
    string addr = result.get_str();
    BOOST_CHECK(!addr.empty());
    BOOST_CHECK_EQUAL(addr[0], '2');
}

// ---------------------------------------------------------------------------
// getaccount — param: a valid address, returns str_type (account name)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaccount_response_contract)
{
    // First get a valid address
    Array pNew;
    string addr = getnewaddress(pNew, false).get_str();

    Array p;
    p.push_back(addr);
    Value result = getaccount(p, false);
    BOOST_CHECK(result.type() == str_type);
    // Default account is ""
    BOOST_CHECK_EQUAL(result.get_str(), "");
}

// ---------------------------------------------------------------------------
// getbalance — returns real_type, >= 0
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getbalance_response_contract)
{
    Array p;
    Value result = getbalance(p, false);
    BOOST_CHECK(result.type() == real_type);
    BOOST_CHECK(result.get_real() >= 0.0);
}

// ---------------------------------------------------------------------------
// getreceivedbyaddress — param: a valid address, returns real_type
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getreceivedbyaddress_response_contract)
{
    // Get a fresh address owned by the wallet
    Array pNew;
    string addr = getnewaddress(pNew, false).get_str();

    Array p;
    p.push_back(addr);
    Value result = getreceivedbyaddress(p, false);
    BOOST_CHECK(result.type() == real_type);
    BOOST_CHECK(result.get_real() >= 0.0);
}

// ---------------------------------------------------------------------------
// signmessage + verifymessage — sign returns str_type, verify returns bool_type
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(signmessage_response_contract)
{
    // Get a wallet address
    Array pNew;
    string addr = getnewaddress(pNew, false).get_str();

    Array p;
    p.push_back(addr);
    p.push_back(string("test message"));
    Value result = signmessage(p, false);
    BOOST_CHECK(result.type() == str_type);
    string sig = result.get_str();
    BOOST_CHECK(!sig.empty());
    // Base64 encoded signature — length should be reasonable
    BOOST_CHECK(sig.size() > 10);
}

BOOST_AUTO_TEST_CASE(verifymessage_response_contract)
{
    // Sign a message first
    Array pNew;
    string addr = getnewaddress(pNew, false).get_str();

    Array pSign;
    pSign.push_back(addr);
    pSign.push_back(string("verify test"));
    string sig = signmessage(pSign, false).get_str();

    // Verify
    Array p;
    p.push_back(addr);
    p.push_back(sig);
    p.push_back(string("verify test"));
    Value result = verifymessage(p, false);
    BOOST_CHECK(result.type() == bool_type);
    BOOST_CHECK_EQUAL(result.get_bool(), true);
}

// ---------------------------------------------------------------------------
// settxfee — param: 0.0001, returns bool_type (true)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(settxfee_response_contract)
{
    Array p;
    p.push_back(0.0001);
    Value result = settxfee(p, false);
    BOOST_CHECK(result.type() == bool_type);
    BOOST_CHECK_EQUAL(result.get_bool(), true);
}

// ---------------------------------------------------------------------------
// getrawmempool — returns array_type of str_type txids
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getrawmempool_response_contract)
{
    Array p;
    Value result = getrawmempool(p, false);
    BOOST_CHECK(result.type() == array_type);
    // Mempool may be empty in test mode; just verify array type.
    // If non-empty, elements should be str_type (txid hex strings).
    const Array& arr = result.get_array();
    for (const Value& v : arr)
        BOOST_CHECK(v.type() == str_type);
}

// ---------------------------------------------------------------------------
// getaddressesbyaccount — param: "" (default), returns array_type
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getaddressesbyaccount_response_contract)
{
    // Ensure at least one address exists in the default account
    Array pNew;
    getnewaddress(pNew, false);

    Array p;
    p.push_back(string(""));
    Value result = getaddressesbyaccount(p, false);
    BOOST_CHECK(result.type() == array_type);
    const Array& arr = result.get_array();
    BOOST_CHECK(!arr.empty());
    // Each element should be a string address
    for (const Value& v : arr)
    {
        BOOST_CHECK(v.type() == str_type);
        char prefix = v.get_str()[0];
        // '2' = PUBKEY_ADDRESS, 'C' = SCRIPT_ADDRESS (P2SH)
        BOOST_CHECK(prefix == '2' || prefix == 'C');
    }
}

// ---------------------------------------------------------------------------
// listreceivedbyaddress — returns array_type with field contracts
// Fields: "address"(str), "account"(str), "amount"(real), "confirmations"(int)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listreceivedbyaddress_response_contract)
{
    // Add a wallet-visible tx with output to a wallet-owned address
    Array pAddr;
    string testAddr = getnewaddress(pAddr, false).get_str();
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

    Array p;
    p.push_back(1);      // minconf = 1
    p.push_back(false);  // includeempty = false
    Value result = listreceivedbyaddress(p, false);
    BOOST_CHECK(result.type() == array_type);
    const Array& arr = result.get_array();
    BOOST_REQUIRE(!arr.empty());
    Object elem = arr[0].get_obj();
    BOOST_CHECK(find_value(elem, "address").type() == str_type);
    BOOST_CHECK(find_value(elem, "account").type() == str_type);
    BOOST_CHECK(find_value(elem, "amount").type() == real_type);
    BOOST_CHECK(find_value(elem, "confirmations").type() == int_type);
}

// ---------------------------------------------------------------------------
// listtransactions — returns array_type; check element structure
// Fields: "account"(str), "category"(str), "amount"(real), plus WalletTxToJSON fields
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listtransactions_response_contract)
{
    // Add a wallet-visible tx so listtransactions has entries to return
    Array pAddr;
    string testAddr = getnewaddress(pAddr, false).get_str();
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

    Array p;
    Value result = listtransactions(p, false);
    BOOST_CHECK(result.type() == array_type);
    const Array& arr = result.get_array();
    BOOST_REQUIRE(!arr.empty());
    // Every entry (whether tx or accounting "move") has these fields:
    for (const Value& v : arr)
    {
        Object elem = v.get_obj();
        BOOST_CHECK(find_value(elem, "account").type() == str_type);
        BOOST_CHECK(find_value(elem, "category").type() == str_type);
        BOOST_CHECK(find_value(elem, "amount").type() == real_type);

        string category = find_value(elem, "category").get_str();
        if (category != "move")
        {
            // WalletTxToJSON fields (only for real transactions, not "move" entries)
            BOOST_CHECK(find_value(elem, "confirmations").type() == int_type);
            BOOST_CHECK(find_value(elem, "txid").type() == str_type);
            BOOST_CHECK(find_value(elem, "time").type() == int_type);
            BOOST_CHECK(find_value(elem, "timereceived").type() == int_type);
        }
        else
        {
            // Accounting "move" entries have "time" and "otheraccount"
            BOOST_CHECK(find_value(elem, "time").type() == int_type);
            BOOST_CHECK(find_value(elem, "otheraccount").type() == str_type);
        }
    }
}

// ---------------------------------------------------------------------------
// listunspent — returns array_type; check element structure
// Fields: "txid"(str), "vout"(int), "scriptPubKey"(str), "amount"(real),
//         "confirmations"(int), optionally "address"(str), "account"(str)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listunspent_response_contract)
{
    // Add a wallet-visible tx with unspent output to a wallet-owned address
    Array pAddr;
    string testAddr = getnewaddress(pAddr, false).get_str();
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

    Array p;
    Value result = listunspent(p, false);
    BOOST_CHECK(result.type() == array_type);
    const Array& arr = result.get_array();
    BOOST_REQUIRE(!arr.empty());
    Object elem = arr[0].get_obj();
    BOOST_CHECK(find_value(elem, "txid").type() == str_type);
    BOOST_CHECK(find_value(elem, "vout").type() == int_type);
    BOOST_CHECK(find_value(elem, "scriptPubKey").type() == str_type);
    BOOST_CHECK(find_value(elem, "amount").type() == real_type);
    BOOST_CHECK(find_value(elem, "confirmations").type() == int_type);
}

// ---------------------------------------------------------------------------
// getreceivedbyaccount — deprecated accounting API, throws by default
// Pins deprecation behavior: -enableaccounts not set → runtime_error
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getreceivedbyaccount_response_contract)
{
    Array p;
    p.push_back(string(""));
    BOOST_CHECK_THROW(getreceivedbyaccount(p, false), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listreceivedbyaccount — deprecated accounting API, throws by default
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listreceivedbyaccount_response_contract)
{
    Array p;
    p.push_back(0);
    p.push_back(true);
    BOOST_CHECK_THROW(listreceivedbyaccount(p, false), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listaddressgroupings — returns array_type (of array_type groupings)
// Inner structure: [[address_str, balance_num, ?account_str], ...]
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listaddressgroupings_response_contract)
{
    // Add a wallet-visible tx so address groupings are non-empty
    Array pAddr;
    string testAddr = getnewaddress(pAddr, false).get_str();
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

    Array p;
    Value result = listaddressgroupings(p, false);
    BOOST_CHECK(result.type() == array_type);
    const Array& groupings = result.get_array();
    BOOST_REQUIRE(!groupings.empty());
    // Validate inner structure
    for (const Value& grouping : groupings)
    {
        BOOST_CHECK(grouping.type() == array_type);
        const Array& addrs = grouping.get_array();
        for (const Value& addrEntry : addrs)
        {
            BOOST_CHECK(addrEntry.type() == array_type);
            const Array& info = addrEntry.get_array();
            // Must have at least 2 elements: [address, balance]
            BOOST_CHECK(info.size() >= 2);
            BOOST_CHECK(info[0].type() == str_type);   // address
            BOOST_CHECK(info[1].type() == real_type);   // balance
            // Optional 3rd element is account name
            if (info.size() >= 3)
                BOOST_CHECK(info[2].type() == str_type);
        }
    }
}

// ---------------------------------------------------------------------------
// listaccounts — returns obj_type with account names as keys
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listaccounts_response_contract)
{
    // Ensure at least one address exists
    Array pNew;
    getnewaddress(pNew, false);

    Array p;
    Value result = listaccounts(p, false);
    BOOST_CHECK(result.type() == obj_type);
    // The implementation maps account → address string (not amount!)
    Object obj = result.get_obj();
    // Should have at least the default "" account
    if (!obj.empty())
    {
        // Each value should be a string (address)
        BOOST_CHECK(obj[0].value_.type() == str_type);
    }
}

// ---------------------------------------------------------------------------
// validateaddress — param: owned address, returns obj_type
// Fields: "isvalid"(bool), "address"(str), "ismine"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validateaddress_response_contract)
{
    // Get an owned address
    Array pNew;
    string addr = getnewaddress(pNew, false).get_str();

    Array p;
    p.push_back(addr);
    Value result = validateaddress(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "isvalid").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), true);
    BOOST_CHECK(find_value(obj, "address").type() == str_type);
    BOOST_CHECK_EQUAL(find_value(obj, "address").get_str(), addr);
    BOOST_CHECK(find_value(obj, "ismine").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "ismine").get_bool(), true);

    // For owned key addresses: isscript, pubkey, iscompressed
    BOOST_CHECK(find_value(obj, "isscript").type() == bool_type);
    BOOST_CHECK(find_value(obj, "pubkey").type() == str_type);
    BOOST_CHECK(find_value(obj, "iscompressed").type() == bool_type);
}

// ---------------------------------------------------------------------------
// validateaddress — invalid address returns isvalid=false
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validateaddress_invalid_response_contract)
{
    Array p;
    p.push_back(string("INVALID_ADDRESS"));
    Value result = validateaddress(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "isvalid").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), false);
}

// ---------------------------------------------------------------------------
// validatepubkey — param: hex pubkey, returns obj_type
// Fields: "isvalid"(bool), optionally "address"(str), "ismine"(bool),
//         "iscompressed"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(validatepubkey_response_contract)
{
    // Get a hex pubkey via getnewpubkey
    Array pPub;
    string hexPubKey = getnewpubkey(pPub, false).get_str();

    Array p;
    p.push_back(hexPubKey);
    Value result = validatepubkey(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "isvalid").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), true);
    BOOST_CHECK(find_value(obj, "address").type() == str_type);
    BOOST_CHECK(find_value(obj, "ismine").type() == bool_type);
    BOOST_CHECK(find_value(obj, "iscompressed").type() == bool_type);
}

// ---------------------------------------------------------------------------
// gettransaction — uses confirmed coinbase from mined blocks
// Returns obj_type with TxToJSON fields + "amount", "details", WalletTxToJSON
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

    Array p;
    p.push_back(txid.GetHex());
    Value result = gettransaction(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    // TxToJSON fields
    BOOST_CHECK(find_value(obj, "txid").type() == str_type);
    BOOST_CHECK_EQUAL(find_value(obj, "txid").get_str(), txid.GetHex());
    BOOST_CHECK(find_value(obj, "version").type() == int_type);
    BOOST_CHECK(find_value(obj, "time").type() == int_type);
    BOOST_CHECK(find_value(obj, "locktime").type() == int_type);
    BOOST_CHECK(find_value(obj, "vin").type() == array_type);
    BOOST_CHECK(find_value(obj, "vout").type() == array_type);

    // WalletTxToJSON fields — confirmed coinbase has all of these
    BOOST_CHECK(find_value(obj, "confirmations").type() == int_type);
    BOOST_CHECK(find_value(obj, "confirmations").get_int() > 0);
    BOOST_CHECK(find_value(obj, "blockhash").type() == str_type);
    BOOST_CHECK_EQUAL(find_value(obj, "blockhash").get_str().size(), 64u);
    BOOST_CHECK(find_value(obj, "blockindex").type() == int_type);
    BOOST_CHECK(find_value(obj, "blocktime").type() == int_type);
    BOOST_CHECK(find_value(obj, "timereceived").type() == int_type);

    // Coinbase-specific: "generated" field
    BOOST_CHECK(find_value(obj, "generated").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "generated").get_bool(), true);

    // amount field
    BOOST_CHECK(find_value(obj, "amount").type() == real_type);

    // details array
    BOOST_CHECK(find_value(obj, "details").type() == array_type);
}

// ---------------------------------------------------------------------------
// listsinceblock — returns obj_type with "transactions" and "lastblock"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(listsinceblock_response_contract)
{
    Array p;
    Value result = listsinceblock(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "transactions").type() == array_type);
    BOOST_CHECK(find_value(obj, "lastblock").type() == str_type);

    // lastblock should be a 64-char hex hash
    string lastblock = find_value(obj, "lastblock").get_str();
    BOOST_CHECK_EQUAL(lastblock.size(), 64u);
}

// ---------------------------------------------------------------------------
// reservebalance — no params returns obj_type with "reserve" and "amount"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(reservebalance_response_contract)
{
    Array p;
    Value result = reservebalance(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "reserve").type() == bool_type);
    BOOST_CHECK(find_value(obj, "amount").type() == real_type);
    // Default: no reserve
    BOOST_CHECK_EQUAL(find_value(obj, "reserve").get_bool(), false);
    BOOST_CHECK(find_value(obj, "amount").get_real() >= 0.0);
}

// ---------------------------------------------------------------------------
// checkwallet — returns obj_type with "wallet check passed"(bool)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(checkwallet_response_contract)
{
    Array p;
    Value result = checkwallet(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    // When wallet is healthy: "wallet check passed" = true
    BOOST_CHECK(find_value(obj, "wallet check passed").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "wallet check passed").get_bool(), true);
}

// ---------------------------------------------------------------------------
// repairwallet — returns obj_type with same structure as checkwallet
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(repairwallet_response_contract)
{
    Array p;
    Value result = repairwallet(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    // When wallet is healthy: "wallet check passed" = true
    BOOST_CHECK(find_value(obj, "wallet check passed").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "wallet check passed").get_bool(), true);
}

// ---------------------------------------------------------------------------
// makekeypair — returns obj_type with "PrivateKey" and "PublicKey"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(makekeypair_response_contract)
{
    Array p;
    Value result = makekeypair(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "PrivateKey").type() == str_type);
    BOOST_CHECK(find_value(obj, "PublicKey").type() == str_type);

    string privKey = find_value(obj, "PrivateKey").get_str();
    string pubKey = find_value(obj, "PublicKey").get_str();
    BOOST_CHECK(!privKey.empty());
    BOOST_CHECK(!pubKey.empty());
    BOOST_CHECK(IsHex(privKey));
    BOOST_CHECK(IsHex(pubKey));
}

// ---------------------------------------------------------------------------
// combinethreshold — no params returns obj_type with "combine threshold"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(combinethreshold_response_contract)
{
    Array p;
    Value result = combinethreshold(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "combine threshold").type() == int_type);
}

// ---------------------------------------------------------------------------
// splitthreshold — no params returns obj_type with "split threshold"
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(splitthreshold_response_contract)
{
    Array p;
    Value result = splitthreshold(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "split threshold").type() == int_type);
}

// ---------------------------------------------------------------------------
// getstakesplitthreshold — returns obj_type with "split threshold"(real)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakesplitthreshold_response_contract)
{
    Array p;
    Value result = getstakesplitthreshold(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();

    // Uses ValueFromAmount, so it's real_type
    BOOST_CHECK(find_value(obj, "split threshold").type() == real_type);
}

BOOST_AUTO_TEST_SUITE_END()
