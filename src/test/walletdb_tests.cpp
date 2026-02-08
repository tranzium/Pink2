// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "db.h"
#include "wallet.h"
#include "walletdb.h"
#include "key.h"
#include "main.h"

extern CWallet* pwalletMain;

BOOST_AUTO_TEST_SUITE(walletdb_tests)

// ============================================================================
// DBErrors enum regression
// ============================================================================

BOOST_AUTO_TEST_CASE(dberrors_enum_values)
{
    // Pin enum values — any reordering would break wallet loading
    BOOST_CHECK_EQUAL(DB_LOAD_OK, 0);
    BOOST_CHECK_EQUAL(DB_CORRUPT, 1);
    BOOST_CHECK_EQUAL(DB_NONCRITICAL_ERROR, 2);
    BOOST_CHECK_EQUAL(DB_TOO_NEW, 3);
    BOOST_CHECK_EQUAL(DB_LOAD_FAIL, 4);
    BOOST_CHECK_EQUAL(DB_NEED_REWRITE, 5);
}

// ============================================================================
// WriteName / EraseName round-trip
// ============================================================================

BOOST_AUTO_TEST_CASE(write_erase_name)
{
    // Use pwalletMain which has a mock BerkeleyDB from TestingSetup
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        std::string addr = "2TestAddress123456";
        std::string name = "Test Label";

        BOOST_CHECK(walletdb.WriteName(addr, name));
        BOOST_CHECK(walletdb.EraseName(addr));
    }
}

BOOST_AUTO_TEST_CASE(write_name_overwrite)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        std::string addr = "2OverwriteAddr";
        BOOST_CHECK(walletdb.WriteName(addr, "First"));
        BOOST_CHECK(walletdb.WriteName(addr, "Second"));
        // Should succeed — overwrite is the default
        BOOST_CHECK(walletdb.EraseName(addr));
    }
}

// ============================================================================
// WriteTx / EraseTx
// ============================================================================

BOOST_AUTO_TEST_CASE(write_erase_tx)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CWalletTx wtx;
        wtx.nLockTime = 42;
        wtx.vout.resize(1);
        wtx.vout[0].nValue = 10 * COIN;
        uint256 hash = wtx.GetHash();

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteTx(hash, wtx));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);

        updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.EraseTx(hash));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// WriteMasterKey
// ============================================================================

BOOST_AUTO_TEST_CASE(write_master_key)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CMasterKey mk;
        mk.vchCryptedKey.assign(32, 0xAB);
        mk.vchSalt.assign(WALLET_CRYPTO_SALT_SIZE, 0xCD);
        mk.nDerivationMethod = 1;
        mk.nDeriveIterations = 25000;

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteMasterKey(1, mk));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// WriteKey — write a real keypair with metadata
// ============================================================================

BOOST_AUTO_TEST_CASE(write_key)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CKey key;
        key.MakeNewKey(true);
        CPubKey pubKey = key.GetPubKey();
        CPrivKey privKey = key.GetPrivKey();
        CKeyMetadata meta(GetTime());

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteKey(pubKey, privKey, meta));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// WritePool / ReadPool / ErasePool
// ============================================================================

BOOST_AUTO_TEST_CASE(pool_write_read_erase)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        // Create a key pool entry
        CKey key;
        key.MakeNewKey(true);
        CKeyPool pool(key.GetPubKey());

        int64_t nPoolIndex = 99999;  // use a high index to avoid conflicts

        // Write
        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WritePool(nPoolIndex, pool));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);

        // Read back
        CKeyPool poolRead;
        BOOST_CHECK(walletdb.ReadPool(nPoolIndex, poolRead));
        BOOST_CHECK(poolRead.vchPubKey == pool.vchPubKey);

        // Erase
        updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.ErasePool(nPoolIndex));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);

        // After erase, read should fail
        CKeyPool poolGone;
        BOOST_CHECK(!walletdb.ReadPool(nPoolIndex, poolGone));
    }
}

// ============================================================================
// WriteOrderPosNext
// ============================================================================

BOOST_AUTO_TEST_CASE(write_order_pos_next)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteOrderPosNext(42));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// WriteDefaultKey
// ============================================================================

BOOST_AUTO_TEST_CASE(write_default_key)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CKey key;
        key.MakeNewKey(true);

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteDefaultKey(key.GetPubKey()));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// WriteMinVersion
// ============================================================================

BOOST_AUTO_TEST_CASE(write_min_version)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);
        BOOST_CHECK(walletdb.WriteMinVersion(FEATURE_BASE));
    }
}

// ============================================================================
// WriteBestBlock / ReadBestBlock
// ============================================================================

BOOST_AUTO_TEST_CASE(write_read_bestblock)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CBlockLocator locator;
        // Write an empty locator
        BOOST_CHECK(walletdb.WriteBestBlock(locator));

        CBlockLocator locatorRead;
        BOOST_CHECK(walletdb.ReadBestBlock(locatorRead));
    }
}

// ============================================================================
// WriteAccount / ReadAccount
// ============================================================================

BOOST_AUTO_TEST_CASE(write_read_account)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        CAccount account;
        CKey key;
        key.MakeNewKey(true);
        account.vchPubKey = key.GetPubKey();

        std::string strAccount = "test_account";
        BOOST_CHECK(walletdb.WriteAccount(strAccount, account));

        CAccount accountRead;
        BOOST_CHECK(walletdb.ReadAccount(strAccount, accountRead));
        BOOST_CHECK(accountRead.vchPubKey == account.vchPubKey);
    }
}

// ============================================================================
// WriteCScript
// ============================================================================

BOOST_AUTO_TEST_CASE(write_cscript)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        // Create a simple P2SH script
        CKey key;
        key.MakeNewKey(true);
        CScript redeemScript;
        redeemScript << OP_DUP << OP_HASH160
                     << key.GetPubKey().GetID()
                     << OP_EQUALVERIFY << OP_CHECKSIG;

        uint160 scriptHash = Hash160(redeemScript);

        unsigned int updatesBefore = nWalletDBUpdated;
        BOOST_CHECK(walletdb.WriteCScript(scriptHash, redeemScript));
        BOOST_CHECK(nWalletDBUpdated > updatesBefore);
    }
}

// ============================================================================
// nWalletDBUpdated counter increments
// ============================================================================

BOOST_AUTO_TEST_CASE(db_updated_counter_increments)
{
    // Every write operation should increment nWalletDBUpdated
    unsigned int baseline = nWalletDBUpdated;

    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        walletdb.WriteName("addr1", "label1");
        BOOST_CHECK_EQUAL(nWalletDBUpdated, baseline + 1);

        CWalletTx wtx;
        wtx.vout.resize(1);
        wtx.vout[0].nValue = COIN;
        walletdb.WriteTx(wtx.GetHash(), wtx);
        BOOST_CHECK_EQUAL(nWalletDBUpdated, baseline + 2);

        walletdb.WriteOrderPosNext(1);
        BOOST_CHECK_EQUAL(nWalletDBUpdated, baseline + 3);
    }
}

// ============================================================================
// CDB version read/write
// ============================================================================

BOOST_AUTO_TEST_CASE(db_version_readwrite)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        int version = 12345;
        BOOST_CHECK(walletdb.WriteVersion(version));

        int versionRead = 0;
        BOOST_CHECK(walletdb.ReadVersion(versionRead));
        BOOST_CHECK_EQUAL(versionRead, version);
    }
}

BOOST_AUTO_TEST_SUITE_END()
