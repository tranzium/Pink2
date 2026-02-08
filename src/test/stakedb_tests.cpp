// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "stakedb.h"
#include "wallet.h"
#include "db.h"

extern CWallet* pwalletMain;

BOOST_AUTO_TEST_SUITE(stakedb_tests)

// ============================================================================
// SDBErrors enum pinning — values must remain stable
// ============================================================================

BOOST_AUTO_TEST_CASE(sdb_error_enum_values)
{
    BOOST_CHECK_EQUAL(SDB_LOAD_OK, 0);
    BOOST_CHECK_EQUAL(SDB_CORRUPT, 1);
    BOOST_CHECK_EQUAL(SDB_NONCRITICAL_ERROR, 2);
    BOOST_CHECK_EQUAL(SDB_TOO_NEW, 3);
    BOOST_CHECK_EQUAL(SDB_LOAD_FAIL, 4);
    BOOST_CHECK_EQUAL(SDB_NEED_REWRITE, 5);
}

// ============================================================================
// WriteStake / ReadStake / EraseStake CRUD
// ============================================================================

BOOST_AUTO_TEST_CASE(stakedb_write_read_roundtrip)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteStake("2TestAddr1", "Alice", "50"));

        std::string name, percent;
        BOOST_CHECK(db.ReadStake("2TestAddr1", name, percent));
        BOOST_CHECK_EQUAL(name, "Alice");
        BOOST_CHECK_EQUAL(percent, "50");
    }
    bitdb.CloseDb("staketest.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_erase_stake)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteStake("2TestAddr2", "Bob", "25"));

        // Erase
        BOOST_CHECK(db.EraseStake("2TestAddr2"));

        // Read should now fail
        std::string name, percent;
        BOOST_CHECK(!db.ReadStake("2TestAddr2", name, percent));
    }
    bitdb.CloseDb("staketest.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_overwrite_stake)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteStake("2TestAddr3", "Carol", "10"));
        BOOST_CHECK(db.WriteStake("2TestAddr3", "Carol Updated", "20"));

        std::string name, percent;
        BOOST_CHECK(db.ReadStake("2TestAddr3", name, percent));
        BOOST_CHECK_EQUAL(name, "Carol Updated");
        BOOST_CHECK_EQUAL(percent, "20");
    }
    bitdb.CloseDb("staketest.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_multiple_stakes)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteStake("2Addr_A", "StakeA", "30"));
        BOOST_CHECK(db.WriteStake("2Addr_B", "StakeB", "40"));
        BOOST_CHECK(db.WriteStake("2Addr_C", "StakeC", "30"));

        std::string nameA, pctA, nameB, pctB, nameC, pctC;
        BOOST_CHECK(db.ReadStake("2Addr_A", nameA, pctA));
        BOOST_CHECK(db.ReadStake("2Addr_B", nameB, pctB));
        BOOST_CHECK(db.ReadStake("2Addr_C", nameC, pctC));

        BOOST_CHECK_EQUAL(nameA, "StakeA");
        BOOST_CHECK_EQUAL(pctB, "40");
        BOOST_CHECK_EQUAL(nameC, "StakeC");
    }
    bitdb.CloseDb("staketest.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_empty_read_fails)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        std::string name, percent;
        BOOST_CHECK(!db.ReadStake("2NonExistentAddr", name, percent));
    }
    bitdb.CloseDb("staketest.dat");
}

// ============================================================================
// nStakeDBUpdated counter
// ============================================================================

BOOST_AUTO_TEST_CASE(stakedb_update_counter)
{
    unsigned int before = nStakeDBUpdated;
    {
        CStakeDB db("staketest.dat", "cr+");
        db.WriteStake("2CounterTest", "Test", "10");
    }
    BOOST_CHECK_EQUAL(nStakeDBUpdated, before + 1);

    {
        CStakeDB db("staketest.dat", "cr+");
        db.EraseStake("2CounterTest");
    }
    BOOST_CHECK_EQUAL(nStakeDBUpdated, before + 2);
    bitdb.CloseDb("staketest.dat");
}

// ============================================================================
// WriteMinVersion
// ============================================================================

BOOST_AUTO_TEST_CASE(stakedb_write_min_version)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteMinVersion(1));
    }
    bitdb.CloseDb("staketest.dat");
}

// ============================================================================
// Erase nonexistent — should not crash
// ============================================================================

BOOST_AUTO_TEST_CASE(stakedb_erase_nonexistent)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        // Erasing something that doesn't exist should not crash
        // (may return false since keys don't exist)
        db.EraseStake("2NeverWritten");
    }
    bitdb.CloseDb("staketest.dat");
}

// ============================================================================
// Empty string values
// ============================================================================

BOOST_AUTO_TEST_CASE(stakedb_empty_values)
{
    {
        CStakeDB db("staketest.dat", "cr+");
        BOOST_CHECK(db.WriteStake("2EmptyTest", "", "0"));

        std::string name, percent;
        BOOST_CHECK(db.ReadStake("2EmptyTest", name, percent));
        BOOST_CHECK_EQUAL(name, "");
        BOOST_CHECK_EQUAL(percent, "0");
    }
    bitdb.CloseDb("staketest.dat");
}

BOOST_AUTO_TEST_SUITE_END()
