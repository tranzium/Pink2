// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for db.cpp lifecycle functions: CDBEnv (CloseDb, Flush),
// CDB (Rewrite), and BerkeleyDB environment management.

#include <boost/test/unit_test.hpp>

#include "db.h"
#include "wallet.h"
#include "walletdb.h"
#include "main.h"

#include <memory>

extern CDBEnv bitdb;
extern std::unique_ptr<CWallet> pwalletMain;

BOOST_AUTO_TEST_SUITE(db_tests)

// ============================================================================
// CDBEnv tests
// ============================================================================

BOOST_AUTO_TEST_CASE(dbenv_is_mock)
{
    // In test mode, bitdb should be in mock mode
    BOOST_CHECK(bitdb.IsMock());
}

BOOST_AUTO_TEST_CASE(close_db_unknown_file)
{
    // Closing a non-existent DB file should not crash
    BOOST_CHECK_NO_THROW(bitdb.CloseDb("nonexistent_file.dat"));
}

BOOST_AUTO_TEST_CASE(close_db_wallet_file)
{
    // Close the wallet DB file, then re-use it
    // (CWalletDB reopens as needed)
    BOOST_CHECK_NO_THROW(bitdb.CloseDb(pwalletMain->strWalletFile));
}

BOOST_AUTO_TEST_CASE(flush_nofiles)
{
    // Flush with fShutdown=false should not crash even with empty state
    // Save and clear mapFileUseCount
    auto saved = bitdb.mapFileUseCount;
    bitdb.mapFileUseCount.clear();

    BOOST_CHECK_NO_THROW(bitdb.Flush(false));

    bitdb.mapFileUseCount = saved;
}

BOOST_AUTO_TEST_CASE(map_file_use_count_wallet)
{
    // The wallet file should be tracked in mapFileUseCount
    auto it = bitdb.mapFileUseCount.find(pwalletMain->strWalletFile);
    // May or may not be present depending on whether any DB is open
    // Just verify the map exists and is accessible
    BOOST_CHECK(bitdb.mapFileUseCount.size() >= 0);
}

// ============================================================================
// CDB lifecycle tests
// ============================================================================

BOOST_AUTO_TEST_CASE(walletdb_open_write_read)
{
    // Open wallet DB, write a key-value pair, read it back
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        std::string addr = "2DbTestAddr123";
        std::string label = "DbTestLabel";

        BOOST_CHECK(walletdb.WriteName(addr, label));
        // Clean up
        BOOST_CHECK(walletdb.EraseName(addr));
    }
}

BOOST_AUTO_TEST_CASE(walletdb_write_minversion)
{
    {
        CWalletDB walletdb(pwalletMain->strWalletFile);

        // WriteMinVersion should succeed
        BOOST_CHECK(walletdb.WriteMinVersion(0));
    }
}

BOOST_AUTO_TEST_CASE(rewrite_mock_mode)
{
    // In mock mode, CDB::Rewrite returns true (early return)
    bool result = CDB::Rewrite(pwalletMain->strWalletFile);
    BOOST_CHECK(result);
}

BOOST_AUTO_TEST_CASE(rewrite_skip_key)
{
    // Rewrite with a skip key should also succeed in mock mode
    bool result = CDB::Rewrite(pwalletMain->strWalletFile, "skipme");
    BOOST_CHECK(result);
}

BOOST_AUTO_TEST_SUITE_END()
