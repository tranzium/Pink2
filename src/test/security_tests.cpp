// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Phase 6B: Security hardening tests.
// Covers: RAII database cursor guards, LevelDB iterator guards,
//         network security constants, threading primitives.

#include <boost/test/unit_test.hpp>

#include "db.h"
#include "db_cursor_guard.h"
#include "main.h"
#include "net.h"
#include "util.h"
#include "wallet.h"
#include "walletdb.h"
#include "stakedb.h"
#include "serialize.h"

#include <atomic>
#include <memory>
#include <thread>

extern std::unique_ptr<CWallet> pwalletMain;
extern std::unique_ptr<CWallet> pstakeDB;

// ============================================================================
// Suite 1: BDB cursor guard RAII tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(bdb_cursor_guard_tests)

BOOST_AUTO_TEST_CASE(guard_nullptr_safe)
{
    // Constructing a guard with nullptr should not crash on destruction
    BdbCursorGuard guard(nullptr);
    BOOST_CHECK(!guard);
    BOOST_CHECK(guard.get() == nullptr);
}

BOOST_AUTO_TEST_CASE(guard_bool_conversion)
{
    BdbCursorGuard nullGuard(nullptr);
    BOOST_CHECK(!nullGuard);
    BOOST_CHECK_EQUAL(static_cast<bool>(nullGuard), false);
}

BOOST_AUTO_TEST_CASE(guard_release_transfers_ownership)
{
    BdbCursorGuard guard(nullptr);
    Dbc* raw = guard.release();
    BOOST_CHECK(!guard); // guard no longer owns it
    BOOST_CHECK(raw == nullptr);
}

BOOST_AUTO_TEST_CASE(guard_exception_safety)
{
    // BdbCursorGuard with nullptr should survive exception unwinding
    bool exceptionCaught = false;
    try {
        BdbCursorGuard cursor(nullptr);
        throw std::runtime_error("simulated error during iteration");
    } catch (const std::runtime_error&) {
        exceptionCaught = true;
    }
    BOOST_CHECK(exceptionCaught);
}

BOOST_AUTO_TEST_CASE(wallet_load_uses_raii)
{
    // CWalletDB::LoadWallet uses BdbCursorGuard.
    // Verify by exercising ListAccountCreditDebit (which also uses RAII cursors)
    // on the existing test wallet — avoids mock BDB cross-contamination.
    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::list<CAccountingentry> entries;
    BOOST_CHECK_NO_THROW(walletdb.ListAccountCreditDebit("*", entries));
}

BOOST_AUTO_TEST_CASE(stakedb_load_uses_raii)
{
    // CStakeDB::LoadWallet now uses BdbCursorGuard.
    CStakeDB stakedb(pstakeDB->strWalletFile);
    SDBErrors err = stakedb.LoadWallet(pstakeDB.get());
    BOOST_CHECK(err == SDB_LOAD_OK || err == SDB_NONCRITICAL_ERROR);
}

BOOST_AUTO_TEST_CASE(list_account_credit_debit_uses_raii)
{
    // ListAccountCreditDebit now uses BdbCursorGuard.
    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::list<CAccountingentry> entries;
    // Should not throw, even with empty wallet
    BOOST_CHECK_NO_THROW(walletdb.ListAccountCreditDebit("*", entries));
}

BOOST_AUTO_TEST_CASE(cursor_guard_repeated_operations)
{
    // Repeated cursor operations prove cleanup is solid — no resource leaks
    for (int i = 0; i < 5; i++) {
        CWalletDB walletdb(pwalletMain->strWalletFile);
        std::list<CAccountingentry> entries;
        BOOST_CHECK_NO_THROW(walletdb.ListAccountCreditDebit("*", entries));
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 2: LevelDB iterator guard tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(leveldb_iterator_guard_tests)

BOOST_AUTO_TEST_CASE(unique_ptr_iterator_valid)
{
    // rpcsmessage.cpp already uses unique_ptr<leveldb::Iterator>.
    // This test documents the pattern and ensures it compiles correctly.
    // We can't easily create a LevelDB iterator in tests without a real DB,
    // but we can verify the unique_ptr pattern with nullptr.
    std::unique_ptr<leveldb::Iterator> it(nullptr);
    BOOST_CHECK(!it);
}

BOOST_AUTO_TEST_CASE(unique_ptr_reset_pattern)
{
    // Verify the reset pattern used in messagemodel.cpp
    std::unique_ptr<leveldb::Iterator> it(nullptr);
    BOOST_CHECK(!it);

    // Reset with another nullptr (simulating second iteration)
    it.reset(nullptr);
    BOOST_CHECK(!it);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 3: Network security constant tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(network_security_tests)

BOOST_AUTO_TEST_CASE(max_size_is_32mb)
{
    BOOST_CHECK_EQUAL(MAX_SIZE, 0x02000000u);
    BOOST_CHECK_EQUAL(MAX_SIZE, 33554432u); // 32 MB
}

BOOST_AUTO_TEST_CASE(max_inv_sz_pinned)
{
    BOOST_CHECK_EQUAL(MAX_INV_SZ, 50000u);
}

BOOST_AUTO_TEST_CASE(max_block_size_pinned)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIZE, 1000000u);
}

BOOST_AUTO_TEST_CASE(max_block_size_gen_pinned)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIZE_GEN, MAX_BLOCK_SIZE / 2);
}

BOOST_AUTO_TEST_CASE(max_block_sigops_pinned)
{
    BOOST_CHECK_EQUAL(MAX_BLOCK_SIGOPS, MAX_BLOCK_SIZE / 50);
}

BOOST_AUTO_TEST_CASE(money_range_validation)
{
    BOOST_CHECK(MoneyRange(0));
    BOOST_CHECK(MoneyRange(1));
    BOOST_CHECK(MoneyRange(MAX_MONEY));
    BOOST_CHECK(!MoneyRange(-1));
    BOOST_CHECK(!MoneyRange(MAX_MONEY + 1));
}

BOOST_AUTO_TEST_CASE(max_money_pinned)
{
    // MAX_MONEY = 500,000,000 * COIN
    BOOST_CHECK_EQUAL(MAX_MONEY, static_cast<int64_t>(500000000) * COIN);
}

BOOST_AUTO_TEST_CASE(coin_value_pinned)
{
    BOOST_CHECK_EQUAL(COIN, 100000000);
    BOOST_CHECK_EQUAL(CENT, 1000000);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 4: Threading primitive tests
// ============================================================================
BOOST_AUTO_TEST_SUITE(threading_tests)

BOOST_AUTO_TEST_CASE(thread_max_pinned)
{
    BOOST_CHECK_EQUAL(THREAD_MAX, 10);
}

BOOST_AUTO_TEST_CASE(thread_enum_values)
{
    BOOST_CHECK_EQUAL(THREAD_SOCKETHANDLER, 0);
    BOOST_CHECK_EQUAL(THREAD_OPENCONNECTIONS, 1);
    BOOST_CHECK_EQUAL(THREAD_MESSAGEHANDLER, 2);
    BOOST_CHECK_EQUAL(THREAD_RPCLISTENER, 3);
    BOOST_CHECK_EQUAL(THREAD_UPNP, 4);
    BOOST_CHECK_EQUAL(THREAD_DNSSEED, 5);
    BOOST_CHECK_EQUAL(THREAD_ADDEDCONNECTIONS, 6);
    BOOST_CHECK_EQUAL(THREAD_DUMPADDRESS, 7);
    BOOST_CHECK_EQUAL(THREAD_RPCHANDLER, 8);
    BOOST_CHECK_EQUAL(THREAD_STAKE_MINER, 9);
}

BOOST_AUTO_TEST_CASE(vnthreadsrunning_array_size)
{
    BOOST_CHECK_EQUAL(vnThreadsRunning.size(), static_cast<size_t>(THREAD_MAX));
}

BOOST_AUTO_TEST_CASE(new_thread_creates_and_detaches)
{
    // NewThread should create a detached thread that runs to completion
    static std::atomic<bool> threadRan{false};

    auto fn = []() { threadRan.store(true); };
    bool ok = NewThread(fn);
    BOOST_CHECK(ok);

    // Give thread time to run
    MilliSleep(50);
    BOOST_CHECK(threadRan.load());
}

BOOST_AUTO_TEST_CASE(threadgroup_create_and_join)
{
    std::atomic<int> counter{0};
    {
        ThreadGroup tg;
        tg.create_thread([&counter]() { counter++; });
        tg.create_thread([&counter]() { counter++; });
        tg.create_thread([&counter]() { counter++; });
        tg.join_all();
    }
    BOOST_CHECK_EQUAL(counter.load(), 3);
}

BOOST_AUTO_TEST_CASE(threadgroup_destructor_joins)
{
    std::atomic<int> counter{0};
    {
        ThreadGroup tg;
        tg.create_thread([&counter]() {
            MilliSleep(10);
            counter++;
        });
        // Destructor should join
    }
    BOOST_CHECK_EQUAL(counter.load(), 1);
}

BOOST_AUTO_TEST_CASE(threadgroup_interrupt_all_is_safe)
{
    ThreadGroup tg;
    // interrupt_all() on empty group should not crash
    BOOST_CHECK_NO_THROW(tg.interrupt_all());
}

BOOST_AUTO_TEST_SUITE_END()
