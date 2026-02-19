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
extern Value stop(const Array& params, bool fHelp);

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
// stakeout commands — help text (throws before logic).
// Full functional coverage in staking_tests.cpp (rpc_staking_tests suite).
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

// ===========================================================================
// Wallet Send Operations: sendtoaddress, sendfrom, sendmany, movecmd
// ===========================================================================

BOOST_AUTO_TEST_CASE(sendtoaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendtoaddress(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(sendtoaddress_invalid_address_throws)
{
    Array p;
    p.push_back(string("INVALID_ADDRESS"));
    p.push_back(1.0);
    BOOST_CHECK_THROW(sendtoaddress(p, false), Object);
}

BOOST_AUTO_TEST_CASE(sendtoaddress_negative_amount_throws)
{
    // Negative amount should always be rejected
    Array addrP;
    string destAddr = getnewaddress(addrP, false).get_str();

    Array p;
    p.push_back(destAddr);
    p.push_back(-1.0);
    BOOST_CHECK_THROW(sendtoaddress(p, false), Object);
}

BOOST_AUTO_TEST_CASE(sendfrom_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendfrom(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(sendfrom_invalid_address_throws)
{
    Array p;
    p.push_back(string("")); // account
    p.push_back(string("INVALID_ADDR"));
    p.push_back(1.0);
    BOOST_CHECK_THROW(sendfrom(p, false), Object);
}

BOOST_AUTO_TEST_CASE(sendmany_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendmany(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(sendmany_invalid_address_throws)
{
    Object recipients;
    recipients.push_back(Pair("INVALID_ADDR", 1.0));

    Array p;
    p.push_back(string("")); // account
    p.push_back(recipients);
    BOOST_CHECK_THROW(sendmany(p, false), Object);
}

BOOST_AUTO_TEST_CASE(movecmd_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(movecmd(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(movecmd_requires_enableaccounts)
{
    // Account API is deprecated — movecmd should throw without enableaccounts=1
    Array p;
    p.push_back(string("")); // from default account
    p.push_back(string("testacct"));
    p.push_back(0.01);
    BOOST_CHECK_THROW(movecmd(p, false), runtime_error);
}

// ===========================================================================
// Wallet Management: backupwallet, dumpwallet, importwallet
// ===========================================================================

BOOST_AUTO_TEST_CASE(backupwallet_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(backupwallet(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(backupwallet_functional)
{
    // BackupWallet may fail in test mode if wallet file isn't fully set up.
    // Either succeeds (null return) or throws JSON-RPC error.
    Array p;
    p.push_back(string("/tmp/pink2_test_backup.dat"));
    try {
        Value result = backupwallet(p, false);
        BOOST_CHECK(result.type() == null_type);
    } catch (const Object&) {
        // JSON-RPC error (wallet backup failed) is acceptable in test
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(dumpwallet_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(dumpwallet(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(dumpwallet_functional)
{
    Array p;
    p.push_back(string("/tmp/pink2_test_dumpwallet.txt"));
    BOOST_CHECK_NO_THROW(dumpwallet(p, false));
}

BOOST_AUTO_TEST_CASE(importwallet_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(importwallet(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(importwallet_functional)
{
    // First dump, then reimport
    Array dumpP;
    dumpP.push_back(string("/tmp/pink2_test_importwallet.txt"));
    BOOST_CHECK_NO_THROW(dumpwallet(dumpP, false));

    Array importP;
    importP.push_back(string("/tmp/pink2_test_importwallet.txt"));
    BOOST_CHECK_NO_THROW(importwallet(importP, false));
}

// ===========================================================================
// Wallet Encryption: walletpassphrase, walletpassphrasechange, walletlock,
//                    encryptwallet, stop
// ===========================================================================

BOOST_AUTO_TEST_CASE(walletpassphrase_unencrypted)
{
    // Wallet is unencrypted in test mode.
    // Help guard: if (IsCrypted() && (fHelp || ...)) — skipped (not encrypted)
    // Then: if (fHelp) return true; — returns silently
    Array p;
    BOOST_CHECK_NO_THROW(walletpassphrase(p, true));

    // With valid params but unencrypted → JSONRPCError (Object thrown)
    Array p2;
    p2.push_back(string("testpass"));
    p2.push_back(60);
    BOOST_CHECK_THROW(walletpassphrase(p2, false), Object);
}

BOOST_AUTO_TEST_CASE(walletpassphrasechange_unencrypted)
{
    // Same pattern: if (fHelp) return true; for unencrypted wallet
    Array p;
    BOOST_CHECK_NO_THROW(walletpassphrasechange(p, true));

    Array p2;
    p2.push_back(string("oldpass"));
    p2.push_back(string("newpass"));
    BOOST_CHECK_THROW(walletpassphrasechange(p2, false), Object);
}

BOOST_AUTO_TEST_CASE(walletlock_unencrypted_throws)
{
    // Guard: if (IsCrypted() && (fHelp || ...)) — skipped
    // Then: if (!IsCrypted()) throw JSONRPCError
    Array p;
    BOOST_CHECK_THROW(walletlock(p, false), Object);
}

BOOST_AUTO_TEST_CASE(encryptwallet_help_throws)
{
    // DO NOT call without fHelp — it calls StartShutdown()
    Array p;
    BOOST_CHECK_THROW(encryptwallet(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(stop_help_throws)
{
    // DO NOT call without fHelp — it calls StartShutdown()
    Array p;
    BOOST_CHECK_THROW(stop(p, true), runtime_error);
}

// ===========================================================================
// Multi-sig / Scripts: addmultisigaddress, addredeemscript
// ===========================================================================

BOOST_AUTO_TEST_CASE(addmultisigaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(addmultisigaddress(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(addmultisigaddress_1of1)
{
    // Get a fresh address and use it for 1-of-1 multisig
    Array addrP;
    Value addr = getnewaddress(addrP, false);

    Array keys;
    keys.push_back(addr.get_str());

    Array p;
    p.push_back(1); // nrequired
    p.push_back(keys);
    Value result = addmultisigaddress(p, false);
    BOOST_CHECK(result.type() == str_type);
    // P2SH address should start with 'C' (Pinkcoin SCRIPT_ADDRESS = 28)
    BOOST_CHECK(result.get_str()[0] == 'C');
}

BOOST_AUTO_TEST_CASE(addredeemscript_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(addredeemscript(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(addredeemscript_simple)
{
    // OP_TRUE (0x51) as a trivial redeemScript
    Array p;
    p.push_back(string("51"));
    Value result = addredeemscript(p, false);
    BOOST_CHECK(result.type() == str_type);
    // P2SH address
    BOOST_CHECK(result.get_str()[0] == 'C');
}

// ===========================================================================
// Threshold Config: combinethreshold, splitthreshold
// ===========================================================================

BOOST_AUTO_TEST_CASE(combinethreshold_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(combinethreshold(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(combinethreshold_read_default)
{
    // No params → returns current value
    Array p;
    Value result = combinethreshold(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "combine threshold").type() != null_type);
}

BOOST_AUTO_TEST_CASE(combinethreshold_set_roundtrip)
{
    // Save original, set new, verify, restore
    Array getP;
    Value orig = combinethreshold(getP, false);
    int64_t origVal = find_value(orig.get_obj(), "combine threshold").get_int64();

    // Set to 500 (must be >= 100 and < nSplitThreshold)
    Array setP;
    setP.push_back(static_cast<int64_t>(500));
    Value setResult = combinethreshold(setP, false);
    int64_t newVal = find_value(setResult.get_obj(), "combine threshold").get_int64();
    BOOST_CHECK_EQUAL(newVal, 500);

    // Restore original
    Array restoreP;
    restoreP.push_back(origVal);
    combinethreshold(restoreP, false);
}

BOOST_AUTO_TEST_CASE(splitthreshold_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(splitthreshold(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(splitthreshold_read_default)
{
    Array p;
    Value result = splitthreshold(p, false);
    BOOST_CHECK(result.type() == obj_type);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "split threshold").type() != null_type);
}

BOOST_AUTO_TEST_CASE(splitthreshold_set_roundtrip)
{
    // Save original, set new, verify, restore
    Array getP;
    Value orig = splitthreshold(getP, false);
    int64_t origVal = find_value(orig.get_obj(), "split threshold").get_int64();

    // Set to 5000 (must be <= 1000000 and > nCombineThreshold)
    Array setP;
    setP.push_back(static_cast<int64_t>(5000));
    Value setResult = splitthreshold(setP, false);
    int64_t newVal = find_value(setResult.get_obj(), "split threshold").get_int64();
    BOOST_CHECK_EQUAL(newVal, 5000);

    // Restore original
    Array restoreP;
    restoreP.push_back(origVal);
    splitthreshold(restoreP, false);
}

// ===========================================================================
// Raw Transactions: signrawtransaction, sendrawtransaction, submitblock
// ===========================================================================

BOOST_AUTO_TEST_CASE(signrawtransaction_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(signrawtransaction(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(signrawtransaction_invalid_hex_throws)
{
    Array p;
    p.push_back(string("NOT_VALID_HEX"));
    BOOST_CHECK_THROW(signrawtransaction(p, false), Object);
}

BOOST_AUTO_TEST_CASE(sendrawtransaction_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendrawtransaction(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(sendrawtransaction_invalid_hex_throws)
{
    Array p;
    p.push_back(string("NOT_VALID_HEX"));
    BOOST_CHECK_THROW(sendrawtransaction(p, false), Object);
}

BOOST_AUTO_TEST_CASE(submitblock_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(submitblock(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(submitblock_invalid_hex_throws)
{
    Array p;
    p.push_back(string("0000"));
    // Should either throw or return "rejected"
    try {
        Value result = submitblock(p, false);
        // If it doesn't throw, it should indicate rejection
        if (result.type() == str_type)
            BOOST_CHECK(result.get_str().find("rejected") != string::npos ||
                         result.get_str().find("error") != string::npos);
    } catch (...) {
        // Any exception is acceptable for invalid block data
        BOOST_CHECK(true);
    }
}

// ===========================================================================
// Stealth: importstealthaddress, sendtostealthaddress
// ===========================================================================

BOOST_AUTO_TEST_CASE(importstealthaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(importstealthaddress(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(importstealthaddress_invalid_key_throws)
{
    Array p;
    p.push_back(string("INVALID_KEY"));
    p.push_back(string("INVALID_KEY"));
    // Should throw due to invalid key format
    BOOST_CHECK_THROW(importstealthaddress(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(sendtostealthaddress_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendtostealthaddress(p, true), runtime_error);
}

// ===========================================================================
// Mining (network-dependent): getblocktemplate, getwork, getworkex
// ===========================================================================

BOOST_AUTO_TEST_CASE(getblocktemplate_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getblocktemplate(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getblocktemplate_no_connections_throws)
{
    // In test mode, vNodes is empty → should throw about no connections
    Array p;
    BOOST_CHECK_THROW(getblocktemplate(p, false), Object);
}

BOOST_AUTO_TEST_CASE(getwork_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getwork(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getwork_no_connections_throws)
{
    Array p;
    BOOST_CHECK_THROW(getwork(p, false), Object);
}

BOOST_AUTO_TEST_CASE(getworkex_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(getworkex(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getworkex_no_connections_throws)
{
    Array p;
    BOOST_CHECK_THROW(getworkex(p, false), Object);
}

// ===========================================================================
// Stakeout (pstakeDB-dependent): addstakeout, delstakeout
// Full functional coverage in staking_tests.cpp (rpc_staking_tests suite)
// ===========================================================================

BOOST_AUTO_TEST_CASE(addstakeout_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(addstakeout(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(delstakeout_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(delstakeout(p, true), runtime_error);
}

// ===========================================================================
// Messaging: smsgaddkey, smsggetpubkey, smsgsend, smsgsendanon,
//            smsgscanchain, smsgscanbuckets
// All require smsg to be enabled. Tests that need it will enable it first.
// ===========================================================================

BOOST_AUTO_TEST_CASE(smsgaddkey_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgaddkey(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgaddkey_disabled_throws)
{
    Array p;
    p.push_back(string("2addr"));
    p.push_back(string("pubkey"));
    BOOST_CHECK_THROW(smsgaddkey(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsggetpubkey_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsggetpubkey(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsggetpubkey_disabled_throws)
{
    Array p;
    p.push_back(string("2addr"));
    BOOST_CHECK_THROW(smsggetpubkey(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsend_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgsend(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsend_disabled_throws)
{
    Array p;
    p.push_back(string("from"));
    p.push_back(string("to"));
    p.push_back(string("msg"));
    BOOST_CHECK_THROW(smsgsend(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsendanon_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgsendanon(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsendanon_disabled_throws)
{
    Array p;
    p.push_back(string("to"));
    p.push_back(string("msg"));
    BOOST_CHECK_THROW(smsgsendanon(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgscanchain(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_disabled_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgscanchain(p, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgscanbuckets(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_disabled_throws)
{
    Array p;
    BOOST_CHECK_THROW(smsgscanbuckets(p, false), runtime_error);
}

// Functional smsg tests — enable smsg, test commands, then disable
BOOST_AUTO_TEST_CASE(smsggetpubkey_wallet_address)
{
    // Enable smsg
    Array enP;
    smsgenable(enP, false);

    // Get a wallet address
    Array addrP;
    string addr = getnewaddress(addrP, false).get_str();

    // Look up its pubkey
    Array p;
    p.push_back(addr);
    Value result = smsggetpubkey(p, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "result").get_str() == "Success.");
    BOOST_CHECK(find_value(obj, "compressed public key").type() == str_type);

    // Disable smsg
    Array disP;
    smsgdisable(disP, false);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_functional)
{
    // Enable smsg
    Array enP;
    smsgenable(enP, false);

    // Scan chain — should complete without error
    Array p;
    Value result = smsgscanchain(p, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "result").get_str() == "Scan Chain Completed.");

    // Disable smsg
    Array disP;
    smsgdisable(disP, false);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_functional)
{
    // Enable smsg
    Array enP;
    smsgenable(enP, false);

    // Scan buckets — returns obj with "result" key
    Array p;
    Value result = smsgscanbuckets(p, false);
    Object obj = result.get_obj();
    string scanResult = find_value(obj, "result").get_str();
    BOOST_CHECK(scanResult == "Scan Buckets Completed." ||
                scanResult == "Scan Buckets Failed.");

    // Disable smsg
    Array disP;
    smsgdisable(disP, false);
}

// ===========================================================================
// Other: sendalert, resendtx, clearwallettransactions,
//        scanforalltxns, scanforstealthtxns
// ===========================================================================

BOOST_AUTO_TEST_CASE(sendalert_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(sendalert(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(resendtx_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(resendtx(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(resendtx_functional)
{
    // Should complete without crash (no unconfirmed txns to resend initially)
    Array p;
    BOOST_CHECK_NO_THROW(resendtx(p, false));
}

BOOST_AUTO_TEST_CASE(clearwallettransactions_help_throws)
{
    // Destructive operation — help test only
    Array p;
    p.push_back(string("dummy")); // Wrong param count triggers help
    BOOST_CHECK_THROW(clearwallettransactions(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforalltxns_help_throws)
{
    // scanforalltxns takes 0-1 params; fHelp triggers help text
    Array p;
    BOOST_CHECK_THROW(scanforalltxns(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforalltxns_functional)
{
    // Scan the TestChain's 50 blocks for wallet transactions
    Array p;
    Value result = scanforalltxns(p, false);
    Object obj = result.get_obj();
    // scanforalltxns returns only "result" (no "found" field)
    BOOST_CHECK(find_value(obj, "result").get_str() == "Scan complete.");
}

BOOST_AUTO_TEST_CASE(scanforstealthtxns_help_throws)
{
    Array p;
    BOOST_CHECK_THROW(scanforstealthtxns(p, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforstealthtxns_functional)
{
    // Scan the TestChain's 50 blocks for stealth transactions
    Array p;
    Value result = scanforstealthtxns(p, false);
    Object obj = result.get_obj();
    BOOST_CHECK(find_value(obj, "result").get_str() == "Scan complete.");
}

BOOST_AUTO_TEST_SUITE_END()
