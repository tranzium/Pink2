// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tier I: RPC coverage expansion — tests for previously untested commands.
// Covers: getinfo, signmessage, dumpprivkey/importprivkey, listtransactions,
//         gettransaction, getaccountaddress, listaddressgroupings,
//         listsinceblock, getreceivedbyaccount, listreceivedbyaccount,
//         help, checkwallet, repairwallet, stakeout, stealth, smsg basics.

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

// Declared in bitcoinrpc.cpp but not in bitcoinrpc.h
extern Value help(const Array& params, bool fHelp);

// ============================================================================
// Helpers
// ============================================================================

// ============================================================================
// Tests that need a mined chain (TestChain fixture)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_coverage_tests, TestChain)

// ---------------------------------------------------------------------------
// getinfo — output format pinning
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getinfo_returns_required_fields)
{
    Array p;
    Value result = getinfo(p, false);
    Object obj = result.get_obj();

    // Pin every field that clients may depend on
    BOOST_CHECK(find_value(obj, "version").type() == str_type);
    BOOST_CHECK(find_value(obj, "protocolversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "newmint").type() == real_type);
    BOOST_CHECK(find_value(obj, "stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "moneysupply").type() == real_type);
    BOOST_CHECK(find_value(obj, "connections").type() == int_type);
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);
    BOOST_CHECK(find_value(obj, "paytxfee").type() == real_type);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);
}

BOOST_AUTO_TEST_CASE(getinfo_blocks_matches_nbest_height)
{
    Array p;
    Value result = getinfo(p, false);
    Object obj = result.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "blocks").get_int(), nBestHeight);
}

BOOST_AUTO_TEST_CASE(getinfo_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getinfo(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// signmessage — pairs with existing verifymessage tests
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(signmessage_produces_verifiable_sig)
{
    // Generate an address we own
    Array genP;
    string addr = getnewaddress(genP, false).get_str();

    // Sign a message
    Array signP;
    signP.push_back(addr);
    signP.push_back("test message for signing");
    Value sig = signmessage(signP, false);
    BOOST_CHECK(sig.type() == str_type);
    BOOST_CHECK(!sig.get_str().empty());

    // Verify round-trip
    Array verP;
    verP.push_back(addr);
    verP.push_back(sig.get_str());
    verP.push_back("test message for signing");
    BOOST_CHECK_EQUAL(verifymessage(verP, false).get_bool(), true);
}

BOOST_AUTO_TEST_CASE(signmessage_wrong_param_count_throws)
{
    Array p;
    p.push_back("addr_only");
    BOOST_CHECK_THROW(signmessage(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(signmessage_unknown_address_throws)
{
    Array p;
    p.push_back("2InvalidAddressXXX");
    p.push_back("message");
    BOOST_CHECK_THROW(signmessage(p, false), Object);
}

BOOST_AUTO_TEST_CASE(signmessage_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(signmessage(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// dumpprivkey / importprivkey — key export/import round-trip
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dumpprivkey_returns_wif_key)
{
    Array genP;
    string addr = getnewaddress(genP, false).get_str();

    Array dumpP;
    dumpP.push_back(addr);
    Value wif = dumpprivkey(dumpP, false);
    BOOST_CHECK(wif.type() == str_type);
    BOOST_CHECK(wif.get_str().length() > 40);
}

BOOST_AUTO_TEST_CASE(dumpprivkey_invalid_address_throws)
{
    Array p;
    p.push_back("not_a_valid_address");
    BOOST_CHECK_THROW(dumpprivkey(p, false), Object);
}

BOOST_AUTO_TEST_CASE(dumpprivkey_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(dumpprivkey(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(importprivkey_roundtrip)
{
    // Generate a key, dump it, then import it under a new account
    Array genP;
    string addr = getnewaddress(genP, false).get_str();

    Array dumpP;
    dumpP.push_back(addr);
    string wif = dumpprivkey(dumpP, false).get_str();

    // Import under a label — should not throw (key already exists in wallet)
    Array importP;
    importP.push_back(wif);
    importP.push_back("imported_test_account");
    BOOST_CHECK_NO_THROW(importprivkey(importP, false));

    // Validate the address still works
    Array valP;
    valP.push_back(addr);
    Object obj = validateaddress(valP, false).get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "isvalid").get_bool(), true);
    BOOST_CHECK_EQUAL(find_value(obj, "ismine").get_bool(), true);
}

BOOST_AUTO_TEST_CASE(importprivkey_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(importprivkey(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getaccountaddress
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getaccountaddress_returns_valid)
{
    Array p;
    p.push_back("coverage_test_account");
    Value result = getaccountaddress(p, false);
    BOOST_CHECK(result.type() == str_type);

    CBitcoinAddress parsed(result.get_str());
    BOOST_CHECK(parsed.IsValid());
}

BOOST_AUTO_TEST_CASE(getaccountaddress_same_account_same_address)
{
    Array p;
    p.push_back("deterministic_test");
    string addr1 = getaccountaddress(p, false).get_str();

    // Calling again for the same account should return the same address
    // (until a payment is received on it)
    string addr2 = getaccountaddress(p, false).get_str();
    BOOST_CHECK_EQUAL(addr1, addr2);
}

BOOST_AUTO_TEST_CASE(getaccountaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getaccountaddress(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// listaddressgroupings
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listaddressgroupings_returns_array)
{
    Array p;
    Value result = listaddressgroupings(p, false);
    BOOST_CHECK(result.type() == array_type);
}

BOOST_AUTO_TEST_CASE(listaddressgroupings_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(listaddressgroupings(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// listtransactions
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listtransactions_default_returns_array)
{
    Array p;
    Value result = listtransactions(p, false);
    BOOST_CHECK(result.type() == array_type);
}

BOOST_AUTO_TEST_CASE(listtransactions_with_account)
{
    Array p;
    p.push_back(""); // default account
    p.push_back(10); // count
    Value result = listtransactions(p, false);
    BOOST_CHECK(result.type() == array_type);
}

BOOST_AUTO_TEST_CASE(listtransactions_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(listtransactions(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// listsinceblock
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listsinceblock_no_params_returns_all)
{
    Array p;
    Value result = listsinceblock(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "transactions").type() == array_type);
    BOOST_CHECK(find_value(obj, "lastblock").type() == str_type);
}

BOOST_AUTO_TEST_CASE(listsinceblock_with_blockhash)
{
    // Use genesis block hash
    Array p;
    p.push_back(pindexGenesisBlock->GetBlockHash().GetHex());
    Value result = listsinceblock(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "transactions").type() == array_type);
    BOOST_CHECK(find_value(obj, "lastblock").type() == str_type);
}

BOOST_AUTO_TEST_CASE(listsinceblock_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(listsinceblock(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getreceivedbyaccount / listreceivedbyaccount
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getreceivedbyaccount_requires_enableaccounts)
{
    // Account API is deprecated; calling without enableaccounts=1 throws
    Array p;
    p.push_back("nonexistent_account_xyz");
    BOOST_CHECK_THROW(getreceivedbyaccount(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(getreceivedbyaccount_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getreceivedbyaccount(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaccount_requires_enableaccounts)
{
    // Account API is deprecated; calling without enableaccounts=1 throws
    Array p;
    BOOST_CHECK_THROW(listreceivedbyaccount(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaccount_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(listreceivedbyaccount(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// gettransaction — needs a real tx hash from mined blocks
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(gettransaction_coinbase)
{
    // coinbaseTxns[0] is a mined coinbase — should be retrievable
    BOOST_REQUIRE(!coinbaseTxns.empty());
    string txid = coinbaseTxns[0].GetHash().GetHex();

    Array p;
    p.push_back(txid);
    Value result = gettransaction(p, false);
    Object obj = result.get_obj();

    // gettransaction returns txid and confirmations for both wallet and non-wallet tx
    BOOST_CHECK(find_value(obj, "txid").type() == str_type);
    BOOST_CHECK_EQUAL(find_value(obj, "txid").get_str(), txid);
    BOOST_CHECK(find_value(obj, "confirmations").type() == int_type);
    BOOST_CHECK(find_value(obj, "confirmations").get_int() > 0);
}

BOOST_AUTO_TEST_CASE(gettransaction_unknown_throws)
{
    Array p;
    p.push_back("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    BOOST_CHECK_THROW(gettransaction(p, false), Object);
}

BOOST_AUTO_TEST_CASE(gettransaction_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(gettransaction(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// help — the meta-command
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_no_args_returns_listing)
{
    Array p;
    Value result = help(p, false);
    BOOST_CHECK(result.type() == str_type);
    // Should contain at least some known command names
    string helpText = result.get_str();
    BOOST_CHECK(helpText.find("getinfo") != string::npos);
    BOOST_CHECK(helpText.find("getbalance") != string::npos);
}

BOOST_AUTO_TEST_CASE(help_specific_command)
{
    Array p;
    p.push_back("getinfo");
    Value result = help(p, false);
    BOOST_CHECK(result.type() == str_type);
    BOOST_CHECK(!result.get_str().empty());
}

BOOST_AUTO_TEST_CASE(help_unknown_command)
{
    Array p;
    p.push_back("nonexistent_command_xyz");
    // help for unknown command should return an error message, not throw
    Value result = help(p, false);
    BOOST_CHECK(result.type() == str_type);
    BOOST_CHECK(result.get_str().find("help") != string::npos);
}

// ---------------------------------------------------------------------------
// checkwallet / repairwallet — wallet diagnostics
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(checkwallet_returns_status)
{
    Array p;
    Value result = checkwallet(p, false);
    Object obj = result.get_obj();

    // Should report wallet check status
    BOOST_CHECK(find_value(obj, "wallet check passed").type() == bool_type);
}

BOOST_AUTO_TEST_CASE(checkwallet_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(checkwallet(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(repairwallet_returns_status)
{
    Array p;
    Value result = repairwallet(p, false);
    Object obj = result.get_obj();

    // Should report wallet repair status
    BOOST_CHECK(find_value(obj, "wallet check passed").type() == bool_type);
}

BOOST_AUTO_TEST_CASE(repairwallet_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(repairwallet(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// stakeout commands — pstakeDB not initialized in test mode, so we can
// only test help text (which throws before dereferencing pstakeDB).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(liststakeout_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(liststakeout(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getstakeoutinfo_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getstakeoutinfo(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// stealth address commands
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getnewstealthaddress_returns_address)
{
    Array p;
    Value result = getnewstealthaddress(p, false);
    BOOST_CHECK(result.type() == str_type);
    BOOST_CHECK(!result.get_str().empty());
}

BOOST_AUTO_TEST_CASE(getnewstealthaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getnewstealthaddress(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(liststealthaddresses_returns_array)
{
    // First generate one so we have something to list
    Array genP;
    getnewstealthaddress(genP, false);

    Array p;
    Value result = liststealthaddresses(p, false);
    BOOST_CHECK(result.type() == array_type);
    BOOST_CHECK(result.get_array().size() >= 1);
}

BOOST_AUTO_TEST_CASE(liststealthaddresses_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(liststealthaddresses(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// smsg commands — basic operations
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(smsgdisable_already_disabled_throws)
{
    // In test mode, secure messaging is not enabled by default.
    // smsgdisable should throw "already disabled".
    Array disP;
    BOOST_CHECK_THROW(smsgdisable(disP, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgenable_then_disable)
{
    // Enable messaging, then disable it
    Array enP;
    BOOST_CHECK_NO_THROW(smsgenable(enP, false));

    Array disP;
    BOOST_CHECK_NO_THROW(smsgdisable(disP, false));
}

BOOST_AUTO_TEST_CASE(smsgenable_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgenable(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgdisable_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgdisable(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoptions_returns_list)
{
    Array p;
    Value result = smsgoptions(p, false);
    // Should return option listing
    BOOST_CHECK(result.type() == obj_type || result.type() == str_type);
}

BOOST_AUTO_TEST_CASE(smsgoptions_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgoptions(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsglocalkeys_disabled_throws)
{
    // When smsg is disabled, smsglocalkeys should throw
    Array p;
    BOOST_CHECK_THROW(smsglocalkeys(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsglocalkeys_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsglocalkeys(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgbuckets_disabled_throws)
{
    // When smsg is disabled, smsgbuckets should throw
    Array p;
    BOOST_CHECK_THROW(smsgbuckets(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgbuckets_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgbuckets(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsginbox_disabled_throws)
{
    // When smsg is disabled, smsginbox should throw
    Array p;
    BOOST_CHECK_THROW(smsginbox(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsginbox_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsginbox(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoutbox_disabled_throws)
{
    // When smsg is disabled, smsgoutbox should throw
    Array p;
    BOOST_CHECK_THROW(smsgoutbox(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoutbox_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgoutbox(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// getnodes
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getnodes_returns_string)
{
    // getnodes returns a formatted string of "addnode=ip\n" entries (not an array)
    Array p;
    Value result = getnodes(p, false);
    BOOST_CHECK(result.type() == str_type);
}

BOOST_AUTO_TEST_CASE(getnodes_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getnodes(p, true), runtime_error);
}

// ---------------------------------------------------------------------------
// Wallet management: setstakesplitthreshold, combinethreshold, splitthreshold
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(setstakesplitthreshold_roundtrip)
{
    // AmountFromValue multiplies by COIN; threshold check is nAmount <= 1000000
    // and nAmount >= nCombineThreshold*2 (default 1000*2 = 2000).
    // So pass a tiny coin value: 0.005 => 500000 satoshis (fits both bounds).
    Array setP;
    setP.push_back(0.005);
    Value setResult = setstakesplitthreshold(setP, false);
    BOOST_CHECK(setResult.type() == obj_type);

    // Read it back — key is "split threshold"
    Array getP;
    Value getResult = getstakesplitthreshold(getP, false);
    Object obj = getResult.get_obj();
    BOOST_CHECK(find_value(obj, "split threshold").type() != null_type);
}

BOOST_AUTO_TEST_CASE(setstakesplitthreshold_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(setstakesplitthreshold(p, true), runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
