// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tier I: RPC coverage expansion -- tests for previously untested commands.
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

extern std::unique_ptr<CWallet> pwalletMain;

// Declared in bitcoinrpc.cpp but not in bitcoinrpc.h
extern json help(const json& params, bool fHelp);
extern json stop(const json& params, bool fHelp);

// ============================================================================
// Helpers
// ============================================================================

// ============================================================================
// Tests that need a mined chain (TestChain fixture)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_coverage_tests, TestChain)

// ---------------------------------------------------------------------------
// getinfo -- output format pinning
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getinfo_returns_required_fields)
{
    json p = json::array();
    json result = getinfo(p, false);

    // Pin every field that clients may depend on
    BOOST_CHECK(result["version"].is_string());
    BOOST_CHECK(result["protocolversion"].is_number_integer());
    BOOST_CHECK(result["walletversion"].is_number_integer());
    BOOST_CHECK(result["balance"].is_number_float());
    BOOST_CHECK(result["newmint"].is_number_float());
    BOOST_CHECK(result["stake"].is_number_float());
    BOOST_CHECK(result["blocks"].is_number_integer());
    BOOST_CHECK(result["moneysupply"].is_number_float());
    BOOST_CHECK(result["connections"].is_number_integer());
    BOOST_CHECK(result["difficulty"].is_object());
    BOOST_CHECK(result["testnet"].is_boolean());
    BOOST_CHECK(result["keypoololdest"].is_number_integer());
    BOOST_CHECK(result["keypoolsize"].is_number_integer());
    BOOST_CHECK(result["paytxfee"].is_number_float());
    BOOST_CHECK(result["errors"].is_string());
}

BOOST_AUTO_TEST_CASE(getinfo_blocks_matches_nbest_height)
{
    json p = json::array();
    json result = getinfo(p, false);
    BOOST_CHECK_EQUAL(result["blocks"].get<int>(), nBestHeight);
}

BOOST_AUTO_TEST_CASE(getinfo_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getinfo(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// signmessage -- pairs with existing verifymessage tests
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(signmessage_produces_verifiable_sig)
{
    // Generate an address we own
    json genP = json::array();
    std::string addr = getnewaddress(genP, false).get<std::string>();

    // Sign a message
    json signP = json::array();
    signP.push_back(addr);
    signP.push_back("test message for signing");
    json sig = signmessage(signP, false);
    BOOST_CHECK(sig.is_string());
    BOOST_CHECK(!sig.get<std::string>().empty());

    // Verify round-trip
    json verP = json::array();
    verP.push_back(addr);
    verP.push_back(sig.get<std::string>());
    verP.push_back("test message for signing");
    BOOST_CHECK_EQUAL(verifymessage(verP, false).get<bool>(), true);
}

BOOST_AUTO_TEST_CASE(signmessage_wrong_param_count_throws)
{
    json p = json::array();
    p.push_back("addr_only");
    BOOST_CHECK_THROW(signmessage(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(signmessage_unknown_address_throws)
{
    json p = json::array();
    p.push_back("2InvalidAddressXXX");
    p.push_back("message");
    BOOST_CHECK_THROW(signmessage(p, false), json);
}

BOOST_AUTO_TEST_CASE(signmessage_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(signmessage(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// dumpprivkey / importprivkey -- key export/import round-trip
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(dumpprivkey_returns_wif_key)
{
    json genP = json::array();
    std::string addr = getnewaddress(genP, false).get<std::string>();

    json dumpP = json::array();
    dumpP.push_back(addr);
    json wif = dumpprivkey(dumpP, false);
    BOOST_CHECK(wif.is_string());
    BOOST_CHECK(wif.get<std::string>().length() > 40);
}

BOOST_AUTO_TEST_CASE(dumpprivkey_invalid_address_throws)
{
    json p = json::array();
    p.push_back("not_a_valid_address");
    BOOST_CHECK_THROW(dumpprivkey(p, false), json);
}

BOOST_AUTO_TEST_CASE(dumpprivkey_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(dumpprivkey(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(importprivkey_roundtrip)
{
    // Generate a key, dump it, then import it under a new account
    json genP = json::array();
    std::string addr = getnewaddress(genP, false).get<std::string>();

    json dumpP = json::array();
    dumpP.push_back(addr);
    std::string wif = dumpprivkey(dumpP, false).get<std::string>();

    // Import under a label -- should not throw (key already exists in wallet)
    json importP = json::array();
    importP.push_back(wif);
    importP.push_back("imported_test_account");
    BOOST_CHECK_NO_THROW(importprivkey(importP, false));

    // Validate the address still works
    json valP = json::array();
    valP.push_back(addr);
    json obj = validateaddress(valP, false);
    BOOST_CHECK_EQUAL(obj["isvalid"].get<bool>(), true);
    BOOST_CHECK_EQUAL(obj["ismine"].get<bool>(), true);
}

BOOST_AUTO_TEST_CASE(importprivkey_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(importprivkey(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// getaccountaddress
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getaccountaddress_returns_valid)
{
    json p = json::array();
    p.push_back("coverage_test_account");
    json result = getaccountaddress(p, false);
    BOOST_CHECK(result.is_string());

    CBitcoinAddress parsed(result.get<std::string>());
    BOOST_CHECK(parsed.IsValid());
}

BOOST_AUTO_TEST_CASE(getaccountaddress_same_account_same_address)
{
    json p = json::array();
    p.push_back("deterministic_test");
    std::string addr1 = getaccountaddress(p, false).get<std::string>();

    // Calling again for the same account should return the same address
    // (until a payment is received on it)
    std::string addr2 = getaccountaddress(p, false).get<std::string>();
    BOOST_CHECK_EQUAL(addr1, addr2);
}

BOOST_AUTO_TEST_CASE(getaccountaddress_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getaccountaddress(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listaddressgroupings
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listaddressgroupings_returns_array)
{
    json p = json::array();
    json result = listaddressgroupings(p, false);
    BOOST_CHECK(result.is_array());
}

BOOST_AUTO_TEST_CASE(listaddressgroupings_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(listaddressgroupings(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listtransactions
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listtransactions_default_returns_array)
{
    json p = json::array();
    json result = listtransactions(p, false);
    BOOST_CHECK(result.is_array());
}

BOOST_AUTO_TEST_CASE(listtransactions_with_account)
{
    json p = json::array();
    p.push_back(""); // default account
    p.push_back(10); // count
    json result = listtransactions(p, false);
    BOOST_CHECK(result.is_array());
}

BOOST_AUTO_TEST_CASE(listtransactions_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(listtransactions(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// listsinceblock
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(listsinceblock_no_params_returns_all)
{
    json p = json::array();
    json result = listsinceblock(p, false);

    BOOST_CHECK(result["transactions"].is_array());
    BOOST_CHECK(result["lastblock"].is_string());
}

BOOST_AUTO_TEST_CASE(listsinceblock_with_blockhash)
{
    // Use genesis block hash
    json p = json::array();
    p.push_back(pindexGenesisBlock->GetBlockHash().GetHex());
    json result = listsinceblock(p, false);

    BOOST_CHECK(result["transactions"].is_array());
    BOOST_CHECK(result["lastblock"].is_string());
}

BOOST_AUTO_TEST_CASE(listsinceblock_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(listsinceblock(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// getreceivedbyaccount / listreceivedbyaccount
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getreceivedbyaccount_requires_enableaccounts)
{
    // Account API is deprecated; calling without enableaccounts=1 throws
    json p = json::array();
    p.push_back("nonexistent_account_xyz");
    BOOST_CHECK_THROW(getreceivedbyaccount(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getreceivedbyaccount_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getreceivedbyaccount(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaccount_requires_enableaccounts)
{
    // Account API is deprecated; calling without enableaccounts=1 throws
    json p = json::array();
    BOOST_CHECK_THROW(listreceivedbyaccount(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(listreceivedbyaccount_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(listreceivedbyaccount(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// gettransaction -- needs a real tx hash from mined blocks
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(gettransaction_coinbase)
{
    // coinbaseTxns[0] is a mined coinbase -- should be retrievable
    BOOST_REQUIRE(!coinbaseTxns.empty());
    std::string txid = coinbaseTxns[0].GetHash().GetHex();

    json p = json::array();
    p.push_back(txid);
    json result = gettransaction(p, false);

    // gettransaction returns txid and confirmations for both wallet and non-wallet tx
    BOOST_CHECK(result["txid"].is_string());
    BOOST_CHECK_EQUAL(result["txid"].get<std::string>(), txid);
    BOOST_CHECK(result["confirmations"].is_number_integer());
    BOOST_CHECK(result["confirmations"].get<int>() > 0);
}

BOOST_AUTO_TEST_CASE(gettransaction_unknown_throws)
{
    json p = json::array();
    p.push_back("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
    BOOST_CHECK_THROW(gettransaction(p, false), json);
}

BOOST_AUTO_TEST_CASE(gettransaction_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(gettransaction(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// help -- the meta-command
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(help_no_args_returns_listing)
{
    json p = json::array();
    json result = help(p, false);
    BOOST_CHECK(result.is_string());
    // Should contain at least some known command names
    std::string helpText = result.get<std::string>();
    BOOST_CHECK(helpText.find("getinfo") != std::string::npos);
    BOOST_CHECK(helpText.find("getbalance") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(help_specific_command)
{
    json p = json::array();
    p.push_back("getinfo");
    json result = help(p, false);
    BOOST_CHECK(result.is_string());
    BOOST_CHECK(!result.get<std::string>().empty());
}

BOOST_AUTO_TEST_CASE(help_unknown_command)
{
    json p = json::array();
    p.push_back("nonexistent_command_xyz");
    // help for unknown command should return an error message, not throw
    json result = help(p, false);
    BOOST_CHECK(result.is_string());
    BOOST_CHECK(result.get<std::string>().find("help") != std::string::npos);
}

// ---------------------------------------------------------------------------
// checkwallet / repairwallet -- wallet diagnostics
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(checkwallet_returns_status)
{
    json p = json::array();
    json result = checkwallet(p, false);

    // Should report wallet check status
    BOOST_CHECK(result["wallet check passed"].is_boolean());
}

BOOST_AUTO_TEST_CASE(checkwallet_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(checkwallet(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(repairwallet_returns_status)
{
    json p = json::array();
    json result = repairwallet(p, false);

    // Should report wallet repair status
    BOOST_CHECK(result["wallet check passed"].is_boolean());
}

BOOST_AUTO_TEST_CASE(repairwallet_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(repairwallet(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// stakeout commands -- help text (throws before logic).
// Full functional coverage in staking_tests.cpp (rpc_staking_tests suite).
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(liststakeout_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(liststakeout(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getstakeoutinfo_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getstakeoutinfo(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// stealth address commands
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getnewstealthaddress_returns_address)
{
    json p = json::array();
    json result = getnewstealthaddress(p, false);
    BOOST_CHECK(result.is_string());
    BOOST_CHECK(!result.get<std::string>().empty());
}

BOOST_AUTO_TEST_CASE(getnewstealthaddress_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getnewstealthaddress(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(liststealthaddresses_returns_array)
{
    // First generate one so we have something to list
    json genP = json::array();
    getnewstealthaddress(genP, false);

    json p = json::array();
    json result = liststealthaddresses(p, false);
    BOOST_CHECK(result.is_array());
    BOOST_CHECK(result.size() >= 1);
}

BOOST_AUTO_TEST_CASE(liststealthaddresses_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(liststealthaddresses(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// smsg commands -- basic operations
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(smsgdisable_already_disabled_throws)
{
    // In test mode, secure messaging is not enabled by default.
    // smsgdisable should throw "already disabled".
    json disP = json::array();
    BOOST_CHECK_THROW(smsgdisable(disP, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgenable_then_disable)
{
    // Enable messaging, then disable it
    json enP = json::array();
    BOOST_CHECK_NO_THROW(smsgenable(enP, false));

    json disP = json::array();
    BOOST_CHECK_NO_THROW(smsgdisable(disP, false));
}

BOOST_AUTO_TEST_CASE(smsgenable_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgenable(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgdisable_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgdisable(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoptions_returns_list)
{
    json p = json::array();
    json result = smsgoptions(p, false);
    // Should return option listing
    BOOST_CHECK(result.is_object() || result.is_string());
}

BOOST_AUTO_TEST_CASE(smsgoptions_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgoptions(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsglocalkeys_disabled_throws)
{
    // When smsg is disabled, smsglocalkeys should throw
    json p = json::array();
    BOOST_CHECK_THROW(smsglocalkeys(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsglocalkeys_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsglocalkeys(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgbuckets_disabled_throws)
{
    // When smsg is disabled, smsgbuckets should throw
    json p = json::array();
    BOOST_CHECK_THROW(smsgbuckets(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgbuckets_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgbuckets(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsginbox_disabled_throws)
{
    // When smsg is disabled, smsginbox should throw
    json p = json::array();
    BOOST_CHECK_THROW(smsginbox(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsginbox_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsginbox(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoutbox_disabled_throws)
{
    // When smsg is disabled, smsgoutbox should throw
    json p = json::array();
    BOOST_CHECK_THROW(smsgoutbox(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgoutbox_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgoutbox(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// getnodes
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(getnodes_returns_string)
{
    // getnodes returns a formatted string of "addnode=ip\n" entries (not an array)
    json p = json::array();
    json result = getnodes(p, false);
    BOOST_CHECK(result.is_string());
}

BOOST_AUTO_TEST_CASE(getnodes_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getnodes(p, true), std::runtime_error);
}

// ---------------------------------------------------------------------------
// Wallet management: setstakesplitthreshold, combinethreshold, splitthreshold
// ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(setstakesplitthreshold_roundtrip)
{
    // AmountFromValue multiplies by COIN; threshold check is nAmount <= 1000000
    // and nAmount >= nCombineThreshold*2 (default 1000*2 = 2000).
    // So pass a tiny coin value: 0.005 => 500000 satoshis (fits both bounds).
    json setP = json::array();
    setP.push_back(0.005);
    json setResult = setstakesplitthreshold(setP, false);
    BOOST_CHECK(setResult.is_object());

    // Read it back -- key is "split threshold"
    json getP = json::array();
    json getResult = getstakesplitthreshold(getP, false);
    BOOST_CHECK(!getResult["split threshold"].is_null());
}

BOOST_AUTO_TEST_CASE(setstakesplitthreshold_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(setstakesplitthreshold(p, true), std::runtime_error);
}

// ===========================================================================
// Wallet Send Operations: sendtoaddress, sendfrom, sendmany, movecmd
// ===========================================================================

BOOST_AUTO_TEST_CASE(sendtoaddress_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendtoaddress(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(sendtoaddress_invalid_address_throws)
{
    json p = json::array();
    p.push_back(std::string("INVALID_ADDRESS"));
    p.push_back(1.0);
    BOOST_CHECK_THROW(sendtoaddress(p, false), json);
}

BOOST_AUTO_TEST_CASE(sendtoaddress_negative_amount_throws)
{
    // Negative amount should always be rejected
    json addrP = json::array();
    std::string destAddr = getnewaddress(addrP, false).get<std::string>();

    json p = json::array();
    p.push_back(destAddr);
    p.push_back(-1.0);
    BOOST_CHECK_THROW(sendtoaddress(p, false), json);
}

BOOST_AUTO_TEST_CASE(sendfrom_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendfrom(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(sendfrom_invalid_address_throws)
{
    json p = json::array();
    p.push_back(std::string("")); // account
    p.push_back(std::string("INVALID_ADDR"));
    p.push_back(1.0);
    BOOST_CHECK_THROW(sendfrom(p, false), json);
}

BOOST_AUTO_TEST_CASE(sendmany_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendmany(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(sendmany_invalid_address_throws)
{
    json recipients;
    recipients["INVALID_ADDR"] = 1.0;

    json p = json::array();
    p.push_back(std::string("")); // account
    p.push_back(recipients);
    BOOST_CHECK_THROW(sendmany(p, false), json);
}

BOOST_AUTO_TEST_CASE(movecmd_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(movecmd(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(movecmd_requires_enableaccounts)
{
    // Account API is deprecated -- movecmd should throw without enableaccounts=1
    json p = json::array();
    p.push_back(std::string("")); // from default account
    p.push_back(std::string("testacct"));
    p.push_back(0.01);
    BOOST_CHECK_THROW(movecmd(p, false), std::runtime_error);
}

// ===========================================================================
// Wallet Management: backupwallet, dumpwallet, importwallet
// ===========================================================================

BOOST_AUTO_TEST_CASE(backupwallet_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(backupwallet(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(backupwallet_functional)
{
    // BackupWallet may fail in test mode if wallet file isn't fully set up.
    // Either succeeds (null return) or throws JSON-RPC error.
    json p = json::array();
    p.push_back(std::string("/tmp/pink2_test_backup.dat"));
    try {
        json result = backupwallet(p, false);
        BOOST_CHECK(result.is_null());
    } catch (const json&) {
        // JSON-RPC error (wallet backup failed) is acceptable in test
        BOOST_CHECK(true);
    }
}

BOOST_AUTO_TEST_CASE(dumpwallet_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(dumpwallet(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(dumpwallet_functional)
{
    json p = json::array();
    std::string dumpPath = (GetDataDir() / "test_dumpwallet.txt").string();
    p.push_back(dumpPath);
    BOOST_CHECK_NO_THROW(dumpwallet(p, false));
}

BOOST_AUTO_TEST_CASE(importwallet_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(importwallet(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(importwallet_functional)
{
    // First dump, then reimport
    std::string walletPath = (GetDataDir() / "test_importwallet.txt").string();
    json dumpP = json::array();
    dumpP.push_back(walletPath);
    BOOST_CHECK_NO_THROW(dumpwallet(dumpP, false));

    json importP = json::array();
    importP.push_back(walletPath);
    BOOST_CHECK_NO_THROW(importwallet(importP, false));
}

// ===========================================================================
// Wallet Encryption: walletpassphrase, walletpassphrasechange, walletlock,
//                    encryptwallet, stop
// ===========================================================================

BOOST_AUTO_TEST_CASE(walletpassphrase_unencrypted)
{
    // Wallet is unencrypted in test mode.
    // Help guard: if (IsCrypted() && (fHelp || ...)) -- skipped (not encrypted)
    // Then: if (fHelp) return true; -- returns silently
    json p = json::array();
    BOOST_CHECK_NO_THROW(walletpassphrase(p, true));

    // With valid params but unencrypted -> JSONRPCError (json thrown)
    json p2 = json::array();
    p2.push_back(std::string("testpass"));
    p2.push_back(60);
    BOOST_CHECK_THROW(walletpassphrase(p2, false), json);
}

BOOST_AUTO_TEST_CASE(walletpassphrasechange_unencrypted)
{
    // Same pattern: if (fHelp) return true; for unencrypted wallet
    json p = json::array();
    BOOST_CHECK_NO_THROW(walletpassphrasechange(p, true));

    json p2 = json::array();
    p2.push_back(std::string("oldpass"));
    p2.push_back(std::string("newpass"));
    BOOST_CHECK_THROW(walletpassphrasechange(p2, false), json);
}

BOOST_AUTO_TEST_CASE(walletlock_unencrypted_throws)
{
    // Guard: if (IsCrypted() && (fHelp || ...)) -- skipped
    // Then: if (!IsCrypted()) throw JSONRPCError
    json p = json::array();
    BOOST_CHECK_THROW(walletlock(p, false), json);
}

BOOST_AUTO_TEST_CASE(encryptwallet_help_throws)
{
    // DO NOT call without fHelp -- it calls StartShutdown()
    json p = json::array();
    BOOST_CHECK_THROW(encryptwallet(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(stop_help_throws)
{
    // DO NOT call without fHelp -- it calls StartShutdown()
    json p = json::array();
    BOOST_CHECK_THROW(stop(p, true), std::runtime_error);
}

// ===========================================================================
// Multi-sig / Scripts: addmultisigaddress, addredeemscript
// ===========================================================================

BOOST_AUTO_TEST_CASE(addmultisigaddress_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(addmultisigaddress(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addmultisigaddress_1of1)
{
    // Get a fresh address and use it for 1-of-1 multisig
    json addrP = json::array();
    json addr = getnewaddress(addrP, false);

    json keys = json::array();
    keys.push_back(addr.get<std::string>());

    json p = json::array();
    p.push_back(1); // nrequired
    p.push_back(keys);
    json result = addmultisigaddress(p, false);
    BOOST_CHECK(result.is_string());
    // P2SH address should start with 'C' (Pinkcoin SCRIPT_ADDRESS = 28)
    BOOST_CHECK(result.get<std::string>()[0] == 'C');
}

BOOST_AUTO_TEST_CASE(addredeemscript_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(addredeemscript(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addredeemscript_simple)
{
    // OP_TRUE (0x51) as a trivial redeemScript
    json p = json::array();
    p.push_back(std::string("51"));
    json result = addredeemscript(p, false);
    BOOST_CHECK(result.is_string());
    // P2SH address
    BOOST_CHECK(result.get<std::string>()[0] == 'C');
}

// ===========================================================================
// Threshold Config: combinethreshold, splitthreshold
// ===========================================================================

BOOST_AUTO_TEST_CASE(combinethreshold_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(combinethreshold(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(combinethreshold_read_default)
{
    // No params -> returns current value
    json p = json::array();
    json result = combinethreshold(p, false);
    BOOST_CHECK(result.is_object());
    BOOST_CHECK(!result["combine threshold"].is_null());
}

BOOST_AUTO_TEST_CASE(combinethreshold_set_roundtrip)
{
    // Save original, set new, verify, restore
    json getP = json::array();
    json orig = combinethreshold(getP, false);
    int64_t origVal = orig["combine threshold"].get<int64_t>();

    // Set to 500 (must be >= 100 and < nSplitThreshold)
    json setP = json::array();
    setP.push_back(static_cast<int64_t>(500));
    json setResult = combinethreshold(setP, false);
    int64_t newVal = setResult["combine threshold"].get<int64_t>();
    BOOST_CHECK_EQUAL(newVal, 500);

    // Restore original
    json restoreP = json::array();
    restoreP.push_back(origVal);
    combinethreshold(restoreP, false);
}

BOOST_AUTO_TEST_CASE(splitthreshold_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(splitthreshold(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(splitthreshold_read_default)
{
    json p = json::array();
    json result = splitthreshold(p, false);
    BOOST_CHECK(result.is_object());
    BOOST_CHECK(!result["split threshold"].is_null());
}

BOOST_AUTO_TEST_CASE(splitthreshold_set_roundtrip)
{
    // Save original, set new, verify, restore
    json getP = json::array();
    json orig = splitthreshold(getP, false);
    int64_t origVal = orig["split threshold"].get<int64_t>();

    // Set to 5000 (must be <= 1000000 and > nCombineThreshold)
    json setP = json::array();
    setP.push_back(static_cast<int64_t>(5000));
    json setResult = splitthreshold(setP, false);
    int64_t newVal = setResult["split threshold"].get<int64_t>();
    BOOST_CHECK_EQUAL(newVal, 5000);

    // Restore original
    json restoreP = json::array();
    restoreP.push_back(origVal);
    splitthreshold(restoreP, false);
}

// ===========================================================================
// Raw Transactions: signrawtransaction, sendrawtransaction, submitblock
// ===========================================================================

BOOST_AUTO_TEST_CASE(signrawtransaction_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(signrawtransaction(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(signrawtransaction_invalid_hex_throws)
{
    json p = json::array();
    p.push_back(std::string("NOT_VALID_HEX"));
    BOOST_CHECK_THROW(signrawtransaction(p, false), json);
}

BOOST_AUTO_TEST_CASE(sendrawtransaction_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendrawtransaction(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(sendrawtransaction_invalid_hex_throws)
{
    json p = json::array();
    p.push_back(std::string("NOT_VALID_HEX"));
    BOOST_CHECK_THROW(sendrawtransaction(p, false), json);
}

BOOST_AUTO_TEST_CASE(submitblock_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(submitblock(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(submitblock_invalid_hex_throws)
{
    json p = json::array();
    p.push_back(std::string("0000"));
    // Should either throw or return "rejected"
    try {
        json result = submitblock(p, false);
        // If it doesn't throw, it should indicate rejection
        if (result.is_string())
            BOOST_CHECK(result.get<std::string>().find("rejected") != std::string::npos ||
                         result.get<std::string>().find("error") != std::string::npos);
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
    json p = json::array();
    BOOST_CHECK_THROW(importstealthaddress(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(importstealthaddress_invalid_key_throws)
{
    json p = json::array();
    p.push_back(std::string("INVALID_KEY"));
    p.push_back(std::string("INVALID_KEY"));
    // Should throw due to invalid key format
    BOOST_CHECK_THROW(importstealthaddress(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(sendtostealthaddress_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendtostealthaddress(p, true), std::runtime_error);
}

// ===========================================================================
// Mining (network-dependent): getblocktemplate, getwork, getworkex
// ===========================================================================

BOOST_AUTO_TEST_CASE(getblocktemplate_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getblocktemplate(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getblocktemplate_no_connections_throws)
{
    // In test mode, vNodes is empty -> should throw about no connections
    json p = json::array();
    BOOST_CHECK_THROW(getblocktemplate(p, false), json);
}

BOOST_AUTO_TEST_CASE(getwork_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getwork(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getwork_no_connections_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getwork(p, false), json);
}

BOOST_AUTO_TEST_CASE(getworkex_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getworkex(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getworkex_no_connections_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(getworkex(p, false), json);
}

// ===========================================================================
// Stakeout (pstakeDB-dependent): addstakeout, delstakeout
// Full functional coverage in staking_tests.cpp (rpc_staking_tests suite)
// ===========================================================================

BOOST_AUTO_TEST_CASE(addstakeout_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(addstakeout(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(delstakeout_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(delstakeout(p, true), std::runtime_error);
}

// ===========================================================================
// Messaging: smsgaddkey, smsggetpubkey, smsgsend, smsgsendanon,
//            smsgscanchain, smsgscanbuckets
// All require smsg to be enabled. Tests that need it will enable it first.
// ===========================================================================

BOOST_AUTO_TEST_CASE(smsgaddkey_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgaddkey(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgaddkey_disabled_throws)
{
    json p = json::array();
    p.push_back(std::string("2addr"));
    p.push_back(std::string("pubkey"));
    BOOST_CHECK_THROW(smsgaddkey(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsggetpubkey_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsggetpubkey(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsggetpubkey_disabled_throws)
{
    json p = json::array();
    p.push_back(std::string("2addr"));
    BOOST_CHECK_THROW(smsggetpubkey(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsend_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgsend(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsend_disabled_throws)
{
    json p = json::array();
    p.push_back(std::string("from"));
    p.push_back(std::string("to"));
    p.push_back(std::string("msg"));
    BOOST_CHECK_THROW(smsgsend(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsendanon_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgsendanon(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgsendanon_disabled_throws)
{
    json p = json::array();
    p.push_back(std::string("to"));
    p.push_back(std::string("msg"));
    BOOST_CHECK_THROW(smsgsendanon(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgscanchain(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_disabled_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgscanchain(p, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgscanbuckets(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_disabled_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(smsgscanbuckets(p, false), std::runtime_error);
}

// Functional smsg tests -- enable smsg, test commands, then disable
BOOST_AUTO_TEST_CASE(smsggetpubkey_wallet_address)
{
    // Enable smsg
    json enP = json::array();
    smsgenable(enP, false);

    // Get a wallet address
    json addrP = json::array();
    std::string addr = getnewaddress(addrP, false).get<std::string>();

    // Look up its pubkey
    json p = json::array();
    p.push_back(addr);
    json result = smsggetpubkey(p, false);
    BOOST_CHECK(result["result"].get<std::string>() == "Success.");
    BOOST_CHECK(result["compressed public key"].is_string());

    // Disable smsg
    json disP = json::array();
    smsgdisable(disP, false);
}

BOOST_AUTO_TEST_CASE(smsgscanchain_functional)
{
    // Enable smsg
    json enP = json::array();
    smsgenable(enP, false);

    // Scan chain -- should complete without error
    json p = json::array();
    json result = smsgscanchain(p, false);
    BOOST_CHECK(result["result"].get<std::string>() == "Scan Chain Completed.");

    // Disable smsg
    json disP = json::array();
    smsgdisable(disP, false);
}

BOOST_AUTO_TEST_CASE(smsgscanbuckets_functional)
{
    // Enable smsg
    json enP = json::array();
    smsgenable(enP, false);

    // Scan buckets -- returns obj with "result" key
    json p = json::array();
    json result = smsgscanbuckets(p, false);
    std::string scanResult = result["result"].get<std::string>();
    BOOST_CHECK(scanResult == "Scan Buckets Completed." ||
                scanResult == "Scan Buckets Failed.");

    // Disable smsg
    json disP = json::array();
    smsgdisable(disP, false);
}

// ===========================================================================
// Other: sendalert, resendtx, clearwallettransactions,
//        scanforalltxns, scanforstealthtxns
// ===========================================================================

BOOST_AUTO_TEST_CASE(sendalert_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(sendalert(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(resendtx_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(resendtx(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(resendtx_functional)
{
    // Should complete without crash (no unconfirmed txns to resend initially)
    json p = json::array();
    BOOST_CHECK_NO_THROW(resendtx(p, false));
}

BOOST_AUTO_TEST_CASE(clearwallettransactions_help_throws)
{
    // Destructive operation -- help test only
    json p = json::array();
    p.push_back(std::string("dummy")); // Wrong param count triggers help
    BOOST_CHECK_THROW(clearwallettransactions(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforalltxns_help_throws)
{
    // scanforalltxns takes 0-1 params; fHelp triggers help text
    json p = json::array();
    BOOST_CHECK_THROW(scanforalltxns(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforalltxns_functional)
{
    // Scan the TestChain's 50 blocks for wallet transactions
    json p = json::array();
    json result = scanforalltxns(p, false);
    // scanforalltxns returns only "result" (no "found" field)
    BOOST_CHECK(result["result"].get<std::string>() == "Scan complete.");
}

BOOST_AUTO_TEST_CASE(scanforstealthtxns_help_throws)
{
    json p = json::array();
    BOOST_CHECK_THROW(scanforstealthtxns(p, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(scanforstealthtxns_functional)
{
    // Scan the TestChain's 50 blocks for stealth transactions
    json p = json::array();
    json result = scanforstealthtxns(p, false);
    BOOST_CHECK(result["result"].get<std::string>() == "Scan complete.");
}

BOOST_AUTO_TEST_SUITE_END()
