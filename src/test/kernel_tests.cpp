// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include "kernel.h"
#include "main.h"

BOOST_AUTO_TEST_SUITE(kernel_tests)

// ============================================================================
// GetWeight() tests — core PoS weight calculation
// ============================================================================

BOOST_AUTO_TEST_CASE(getweight_basic)
{
    // GetWeight returns: min(interval - nStakeMinAge, nStakeMaxAge)
    // nStakeMinAge = 1 hour (3600s)
    // nStakeMaxAge = 30 days (2592000s)

    int64_t nNow = 1700000000;
    int64_t nOneHourAgo = nNow - 3600;       // exactly min age
    int64_t nTwoHoursAgo = nNow - 7200;      // 1 hour past min age

    // At exactly min age, weight should be 0 (interval - minAge = 0)
    BOOST_CHECK_EQUAL(GetWeight(nOneHourAgo, nNow, false), 0);

    // 2 hours total interval: weight = 7200 - 3600 = 3600
    BOOST_CHECK_EQUAL(GetWeight(nTwoHoursAgo, nNow, false), 3600);
}

BOOST_AUTO_TEST_CASE(getweight_max_age_cap)
{
    // Weight should be capped at nStakeMaxAge (30 days = 2592000s)
    int64_t nNow = 1700000000;
    int64_t n60DaysAgo = nNow - (60 * 24 * 60 * 60);

    int64_t weight = GetWeight(n60DaysAgo, nNow, false);
    BOOST_CHECK_EQUAL(weight, (int64_t)nStakeMaxAge);
}

BOOST_AUTO_TEST_CASE(getweight_flash_stake)
{
    // Flash stake uses nFlashStakeMaxAge (7 days = 604800s) as cap
    int64_t nNow = 1700000000;
    int64_t n60DaysAgo = nNow - (60 * 24 * 60 * 60);

    int64_t weightFlash = GetWeight(n60DaysAgo, nNow, true);
    BOOST_CHECK_EQUAL(weightFlash, (int64_t)nFlashStakeMaxAge);

    // Flash weight cap should be less than regular weight cap
    int64_t weightRegular = GetWeight(n60DaysAgo, nNow, false);
    BOOST_CHECK(weightFlash < weightRegular);
}

BOOST_AUTO_TEST_CASE(getweight_flash_vs_regular_within_flash_range)
{
    // Within flash max age range, flash and regular should be equal
    int64_t nNow = 1700000000;
    int64_t n2DaysAgo = nNow - (2 * 24 * 60 * 60);

    int64_t weightFlash = GetWeight(n2DaysAgo, nNow, true);
    int64_t weightRegular = GetWeight(n2DaysAgo, nNow, false);

    // Both should equal: 2 days - 1 hour = 169200s
    int64_t expected = (2 * 24 * 60 * 60) - nStakeMinAge;
    BOOST_CHECK_EQUAL(weightFlash, expected);
    BOOST_CHECK_EQUAL(weightRegular, expected);
}

BOOST_AUTO_TEST_CASE(getweight_exactly_at_flash_max)
{
    // At exactly flash max age + min age, flash weight should be at cap
    int64_t nNow = 1700000000;
    int64_t nInterval = nFlashStakeMaxAge + nStakeMinAge;
    int64_t nStart = nNow - nInterval;

    int64_t weightFlash = GetWeight(nStart, nNow, true);
    BOOST_CHECK_EQUAL(weightFlash, (int64_t)nFlashStakeMaxAge);
}

BOOST_AUTO_TEST_CASE(getweight_progressive_increase)
{
    // Weight should increase linearly between min age and max age
    int64_t nNow = 1700000000;

    int64_t w1 = GetWeight(nNow - 7200, nNow, false);   // 2h interval
    int64_t w2 = GetWeight(nNow - 14400, nNow, false);  // 4h interval
    int64_t w3 = GetWeight(nNow - 86400, nNow, false);  // 24h interval

    BOOST_CHECK(w1 < w2);
    BOOST_CHECK(w2 < w3);

    // Weight = interval - nStakeMinAge, so:
    // w1 = 7200 - 3600 = 3600
    // w2 = 14400 - 3600 = 10800
    // w3 = 86400 - 3600 = 82800
    BOOST_CHECK_EQUAL(w1, 3600);
    BOOST_CHECK_EQUAL(w2, 10800);
    BOOST_CHECK_EQUAL(w3, 82800);
}

// ============================================================================
// CheckCoinStakeTimestamp() tests
// ============================================================================

BOOST_AUTO_TEST_CASE(coinstake_timestamp_must_match_block)
{
    // v0.3 protocol: block time must equal coinstake tx time
    int64_t nBlockTime = 1700000000;
    BOOST_CHECK(CheckCoinStakeTimestamp(nBlockTime, nBlockTime));
}

BOOST_AUTO_TEST_CASE(coinstake_timestamp_mismatch_rejected)
{
    int64_t nBlockTime = 1700000000;
    // Even 1 second difference should fail
    BOOST_CHECK(!CheckCoinStakeTimestamp(nBlockTime, nBlockTime + 1));
    BOOST_CHECK(!CheckCoinStakeTimestamp(nBlockTime, nBlockTime - 1));
    BOOST_CHECK(!CheckCoinStakeTimestamp(nBlockTime, nBlockTime + 60));
}

// ============================================================================
// Stake modifier interval section calculation tests
// ============================================================================

BOOST_AUTO_TEST_CASE(modifier_interval_ratio)
{
    // MODIFIER_INTERVAL_RATIO should be 5
    BOOST_CHECK_EQUAL(MODIFIER_INTERVAL_RATIO, 5);
}

BOOST_AUTO_TEST_CASE(modifier_interval_value)
{
    // nModifierInterval should be 5 minutes
    BOOST_CHECK_EQUAL(nModifierInterval, 5u * 60u);
}

// ============================================================================
// CBlock PoW/PoS identification tests
// ============================================================================

BOOST_AUTO_TEST_CASE(block_is_proof_of_work)
{
    // A block with only a coinbase tx is PoW
    CBlock block;
    CTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 0 << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    block.vtx.push_back(coinbase);

    BOOST_CHECK(block.IsProofOfWork());
    BOOST_CHECK(!block.IsProofOfStake());
}

BOOST_AUTO_TEST_CASE(block_is_proof_of_stake)
{
    // A PoS block has coinbase + coinstake as second tx
    CBlock block;

    // Coinbase (empty output for PoS)
    CTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 0 << 0;
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();
    block.vtx.push_back(coinbase);

    // Coinstake: non-null prevout, first vout empty
    CTransaction coinstake;
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout.hash = uint256("0x1234");
    coinstake.vin[0].prevout.n = 0;
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();  // marker: first output empty
    coinstake.vout[1].nValue = 100 * COIN;
    block.vtx.push_back(coinstake);

    BOOST_CHECK(block.IsProofOfStake());
    BOOST_CHECK(!block.IsProofOfWork());
}

// ============================================================================
// CTransaction IsCoinBase / IsCoinStake identification
// ============================================================================

BOOST_AUTO_TEST_CASE(tx_is_coinbase)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 50 * COIN;

    BOOST_CHECK(tx.IsCoinBase());
    BOOST_CHECK(!tx.IsCoinStake());
}

BOOST_AUTO_TEST_CASE(tx_is_coinstake)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(2);
    tx.vout[0].SetEmpty();  // first output empty = coinstake marker
    tx.vout[1].nValue = 100 * COIN;

    BOOST_CHECK(tx.IsCoinStake());
    BOOST_CHECK(!tx.IsCoinBase());
}

BOOST_AUTO_TEST_CASE(tx_regular_is_neither)
{
    CTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.hash = uint256("0x1234");
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 10 * COIN;

    BOOST_CHECK(!tx.IsCoinBase());
    BOOST_CHECK(!tx.IsCoinStake());
}

BOOST_AUTO_TEST_SUITE_END()
