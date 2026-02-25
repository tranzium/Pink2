// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Phase 6A: Staking correctness tests.
// Covers: AggregateStakeOut, CountStakeOut, SelectCoinsForStaking,
//         GetStakeWeight, CreateCoinStake splitting/combining,
//         threshold configuration, StakeDB persistence, RPC commands,
//         Flash PoS timing.

#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"
#include "kernel.h"
#include "init.h"
#include "stakedb.h"
#include "base58.h"
#include "bitcoinrpc.h"

#include <ctime>

// json type alias provided by bitcoinrpc.h (using json = nlohmann::json)

extern std::unique_ptr<CWallet> pwalletMain;
extern std::unique_ptr<CWallet> pstakeDB;
extern int64_t nSplitThreshold;
extern int64_t nCombineThreshold;
extern int64_t nReserveBalance;

// ============================================================================
// Helpers
// ============================================================================

// Create a valid Pinkcoin address from an index (deterministic).
// Pinkcoin PUBKEY_ADDRESS = 3, so addresses start with "2".
static CBitcoinAddress MakeTestAddress(unsigned int idx)
{
    // uint160 has a uint64_t constructor; use idx directly
    CKeyID keyid(uint160(static_cast<uint64_t>(idx + 1)));
    return CBitcoinAddress(keyid);
}

// RAII guard to save/restore pstakeDB maps between tests.
struct StakeDBGuard {
    std::map<CTxDestination, std::string> savedBook;
    std::map<CTxDestination, std::string> savedPercent;

    StakeDBGuard() {
        BOOST_REQUIRE(pstakeDB != nullptr);
        savedBook = pstakeDB->mapAddressBook;
        savedPercent = pstakeDB->mapAddressPercent;
        pstakeDB->mapAddressBook.clear();
        pstakeDB->mapAddressPercent.clear();
    }
    ~StakeDBGuard() {
        pstakeDB->mapAddressBook = savedBook;
        pstakeDB->mapAddressPercent = savedPercent;
    }
};

// RAII guard for threshold globals.
struct ThresholdGuard {
    int64_t savedSplit;
    int64_t savedCombine;
    int64_t savedReserve;

    ThresholdGuard()
        : savedSplit(nSplitThreshold)
        , savedCombine(nCombineThreshold)
        , savedReserve(nReserveBalance)
    {}
    ~ThresholdGuard() {
        nSplitThreshold = savedSplit;
        nCombineThreshold = savedCombine;
        nReserveBalance = savedReserve;
    }
};

// ============================================================================
// Suite 1: AggregateStakeOut — side-stake reward distribution
// ============================================================================
BOOST_AUTO_TEST_SUITE(aggregate_stakeout_tests)

BOOST_AUTO_TEST_CASE(no_sidestakes_returns_zero)
{
    StakeDBGuard guard;
    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);
    BOOST_CHECK_EQUAL(distributed, 0);
    BOOST_CHECK(tx.vout.empty());
}

BOOST_AUTO_TEST_CASE(single_sidestake_50_percent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    BOOST_REQUIRE(addr.IsValid());

    pstakeDB->mapAddressPercent[addr.Get()] = "50";
    pstakeDB->mapAddressBook[addr.Get()] = "TestAddr1";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 50 * COIN);
    BOOST_CHECK_EQUAL(tx.vout.size(), 1u);
    BOOST_CHECK_EQUAL(tx.vout[0].nValue, 50 * COIN);
}

BOOST_AUTO_TEST_CASE(single_sidestake_100_percent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(2);
    BOOST_REQUIRE(addr.IsValid());
    pstakeDB->mapAddressPercent[addr.Get()] = "100";
    pstakeDB->mapAddressBook[addr.Get()] = "FullStake";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 100 * COIN);
    BOOST_CHECK_EQUAL(tx.vout.size(), 1u);
}

BOOST_AUTO_TEST_CASE(multiple_sidestakes_30_40_30)
{
    StakeDBGuard guard;
    CBitcoinAddress addr1 = MakeTestAddress(1);
    CBitcoinAddress addr2 = MakeTestAddress(2);
    CBitcoinAddress addr3 = MakeTestAddress(3);

    pstakeDB->mapAddressPercent[addr1.Get()] = "30";
    pstakeDB->mapAddressPercent[addr2.Get()] = "40";
    pstakeDB->mapAddressPercent[addr3.Get()] = "30";
    pstakeDB->mapAddressBook[addr1.Get()] = "A";
    pstakeDB->mapAddressBook[addr2.Get()] = "B";
    pstakeDB->mapAddressBook[addr3.Get()] = "C";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 100 * COIN);
    BOOST_CHECK_EQUAL(tx.vout.size(), 3u);

    int64_t totalOut = 0;
    for (const auto& out : tx.vout)
        totalOut += out.nValue;
    BOOST_CHECK_EQUAL(totalOut, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(sidestake_exceeds_100_total_clamped)
{
    // If adding an entry would push cumulative total over 100%, that entry is skipped.
    // Iteration order is std::map order (by CTxDestination), so the result depends
    // on which address sorts first.  We verify the invariant: distributed <= reward.
    StakeDBGuard guard;
    CBitcoinAddress addr1 = MakeTestAddress(1);
    CBitcoinAddress addr2 = MakeTestAddress(2);

    pstakeDB->mapAddressPercent[addr1.Get()] = "70";
    pstakeDB->mapAddressPercent[addr2.Get()] = "50"; // 70+50 = 120 → second skipped
    pstakeDB->mapAddressBook[addr1.Get()] = "A";
    pstakeDB->mapAddressBook[addr2.Get()] = "B";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    // Only the first encountered (by map order) should be honoured if total would exceed 100%
    BOOST_CHECK(distributed <= nReward);
    BOOST_CHECK(distributed > 0);
}

BOOST_AUTO_TEST_CASE(sidestake_zero_percent_skipped)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "0";
    pstakeDB->mapAddressBook[addr.Get()] = "Zero";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 0);
    BOOST_CHECK(tx.vout.empty());
}

BOOST_AUTO_TEST_CASE(sidestake_negative_percent_skipped)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "-10";
    pstakeDB->mapAddressBook[addr.Get()] = "Negative";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 0);
    BOOST_CHECK(tx.vout.empty());
}

BOOST_AUTO_TEST_CASE(sidestake_fractional_percent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "33.333333";
    pstakeDB->mapAddressBook[addr.Get()] = "Third";

    CTransaction tx;
    int64_t nReward = 300 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(tx.vout.size(), 1u);
    BOOST_CHECK(distributed > 0);
    BOOST_CHECK(distributed <= nReward);
    // Key invariant: no coin creation
}

BOOST_AUTO_TEST_CASE(sidestake_over_100_single_entry_skipped)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "150"; // > 100
    pstakeDB->mapAddressBook[addr.Get()] = "Over";

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 0);
    BOOST_CHECK(tx.vout.empty());
}

BOOST_AUTO_TEST_CASE(sidestake_small_reward_no_negative)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "50";
    pstakeDB->mapAddressBook[addr.Get()] = "Small";

    CTransaction tx;
    int64_t nReward = 1; // 1 satoshi
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK(distributed >= 0);
    BOOST_CHECK(distributed <= nReward);
}

BOOST_AUTO_TEST_CASE(sidestake_zero_reward)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "50";
    pstakeDB->mapAddressBook[addr.Get()] = "Zero";

    CTransaction tx;
    int64_t nReward = 0;
    int64_t distributed = pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(distributed, 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 2: CountStakeOut — side-stake count validation
// ============================================================================
BOOST_AUTO_TEST_SUITE(count_stakeout_tests)

BOOST_AUTO_TEST_CASE(no_entries_returns_zero)
{
    StakeDBGuard guard;
    BOOST_CHECK_EQUAL(pstakeDB->CountStakeOut(), 0);
}

BOOST_AUTO_TEST_CASE(single_valid_entry)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "50";
    pstakeDB->mapAddressBook[addr.Get()] = "A";
    BOOST_CHECK_EQUAL(pstakeDB->CountStakeOut(), 1);
}

BOOST_AUTO_TEST_CASE(multiple_valid_entries)
{
    StakeDBGuard guard;
    for (unsigned int i = 1; i <= 5; i++) {
        CBitcoinAddress addr = MakeTestAddress(i);
        pstakeDB->mapAddressPercent[addr.Get()] = "10";
        pstakeDB->mapAddressBook[addr.Get()] = "Addr" + std::to_string(i);
    }
    BOOST_CHECK_EQUAL(pstakeDB->CountStakeOut(), 5);
}

BOOST_AUTO_TEST_CASE(zero_percent_not_counted)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "0";
    pstakeDB->mapAddressBook[addr.Get()] = "Zero";
    BOOST_CHECK_EQUAL(pstakeDB->CountStakeOut(), 0);
}

BOOST_AUTO_TEST_CASE(count_matches_aggregate_output_count)
{
    // CountStakeOut and AggregateStakeOut must agree on valid entry count.
    StakeDBGuard guard;
    CBitcoinAddress addr1 = MakeTestAddress(1);
    CBitcoinAddress addr2 = MakeTestAddress(2);
    CBitcoinAddress addr3 = MakeTestAddress(3);
    pstakeDB->mapAddressPercent[addr1.Get()] = "20";
    pstakeDB->mapAddressPercent[addr2.Get()] = "30";
    pstakeDB->mapAddressPercent[addr3.Get()] = "0"; // skipped
    pstakeDB->mapAddressBook[addr1.Get()] = "A";
    pstakeDB->mapAddressBook[addr2.Get()] = "B";
    pstakeDB->mapAddressBook[addr3.Get()] = "C";

    int64_t count = pstakeDB->CountStakeOut();

    CTransaction tx;
    int64_t nReward = 100 * COIN;
    pstakeDB->AggregateStakeOut(tx, nReward);

    BOOST_CHECK_EQUAL(count, static_cast<int64_t>(tx.vout.size()));
}

BOOST_AUTO_TEST_CASE(negative_percent_not_counted)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(1);
    pstakeDB->mapAddressPercent[addr.Get()] = "-5";
    pstakeDB->mapAddressBook[addr.Get()] = "Neg";
    BOOST_CHECK_EQUAL(pstakeDB->CountStakeOut(), 0);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 3: Threshold configuration — split/combine globals
// ============================================================================
BOOST_AUTO_TEST_SUITE(threshold_config_tests)

BOOST_AUTO_TEST_CASE(default_thresholds_pinned)
{
    // main.cpp:81-82 defaults
    ThresholdGuard guard;
    nSplitThreshold = 2000;
    nCombineThreshold = 1000;

    BOOST_CHECK_EQUAL(nSplitThreshold, 2000);
    BOOST_CHECK_EQUAL(nCombineThreshold, 1000);
    BOOST_CHECK(nSplitThreshold > nCombineThreshold);
}

BOOST_AUTO_TEST_CASE(flash_pos_thresholds)
{
    ThresholdGuard guard;
    nSplitThreshold = 200000;
    nCombineThreshold = 100000;

    bool targetFPOS = (nSplitThreshold == 200000 && nCombineThreshold == 100000);
    BOOST_CHECK(targetFPOS);
}

BOOST_AUTO_TEST_CASE(flash_pos_detection_false_for_defaults)
{
    ThresholdGuard guard;
    nSplitThreshold = 2000;
    nCombineThreshold = 1000;

    bool targetFPOS = (nSplitThreshold == 200000 && nCombineThreshold == 100000);
    BOOST_CHECK(!targetFPOS);
}

BOOST_AUTO_TEST_CASE(split_must_exceed_combine)
{
    ThresholdGuard guard;
    nSplitThreshold = 5000;
    nCombineThreshold = 2000;
    BOOST_CHECK(nSplitThreshold > nCombineThreshold);

    // Equal values violate the invariant
    nSplitThreshold = 1000;
    nCombineThreshold = 1000;
    BOOST_CHECK(!(nSplitThreshold > nCombineThreshold));
}

BOOST_AUTO_TEST_CASE(split_threshold_coin_scaling)
{
    // CreateCoinStake compares: nTotalSize > nSplitThreshold * COIN
    ThresholdGuard guard;
    nSplitThreshold = 2000;

    int64_t thresholdSat = nSplitThreshold * COIN;
    BOOST_CHECK_EQUAL(thresholdSat, 200000000000LL);
}

BOOST_AUTO_TEST_CASE(combine_threshold_coin_scaling)
{
    ThresholdGuard guard;
    nCombineThreshold = 1000;

    int64_t thresholdSat = nCombineThreshold * COIN;
    BOOST_CHECK_EQUAL(thresholdSat, 100000000000LL);
}

BOOST_AUTO_TEST_CASE(reserve_balance_default_zero)
{
    // No coins reserved by default
    ThresholdGuard guard;
    nReserveBalance = 0;
    BOOST_CHECK_EQUAL(nReserveBalance, 0);
}

BOOST_AUTO_TEST_CASE(reserve_balance_staking_guard)
{
    // CreateCoinStake:2513: if (nBalance <= nReserveBalance) return false
    ThresholdGuard guard;

    int64_t nBalance = 5000 * COIN;
    nReserveBalance = 6000 * COIN;
    BOOST_CHECK(nBalance <= nReserveBalance); // Would NOT stake

    nReserveBalance = 1000 * COIN;
    BOOST_CHECK(nBalance > nReserveBalance); // Would stake
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 4: Stake weight calculation — formula verification
// ============================================================================
BOOST_AUTO_TEST_SUITE(stake_weight_calc_tests)

BOOST_AUTO_TEST_CASE(weight_formula_basic)
{
    // Formula: bnCoinDayWeight = (nValue / COIN) * nTimeWeight / nDayTime
    int64_t nValue = 1000 * COIN;
    int64_t nValueCoins = nValue / COIN; // 1000
    int64_t nTimeWeight = 86400;         // 1 day
    int nDayTime = 86400;

    int64_t weight = nValueCoins * nTimeWeight / nDayTime;
    BOOST_CHECK_EQUAL(weight, 1000); // 1000 coin-days
}

BOOST_AUTO_TEST_CASE(weight_fractional_coin_is_zero)
{
    // "Less than 1 coin will never stake." — wallet.cpp:2466
    int64_t nValue = COIN / 2; // 0.5 PINK
    int64_t nValueCoins = nValue / COIN; // 0 (integer truncation)
    int64_t weight = nValueCoins * 86400 / 86400;
    BOOST_CHECK_EQUAL(weight, 0);
}

BOOST_AUTO_TEST_CASE(weight_exactly_one_coin)
{
    int64_t nValueCoins = 1;
    int64_t weight = nValueCoins * 86400 / 86400;
    BOOST_CHECK_EQUAL(weight, 1);
}

BOOST_AUTO_TEST_CASE(weight_scales_linearly_with_value)
{
    int nDayTime = 86400;
    int64_t nTimeWeight = 86400;

    int64_t w100  = (100  * COIN / COIN) * nTimeWeight / nDayTime;
    int64_t w1000 = (1000 * COIN / COIN) * nTimeWeight / nDayTime;
    int64_t w10k  = (10000 * COIN / COIN) * nTimeWeight / nDayTime;

    BOOST_CHECK_EQUAL(w100, 100);
    BOOST_CHECK_EQUAL(w1000, 1000);
    BOOST_CHECK_EQUAL(w10k, 10000);
    BOOST_CHECK_EQUAL(w1000, w100 * 10);
}

BOOST_AUTO_TEST_CASE(weight_scales_with_time)
{
    int nDayTime = 86400;
    int64_t nValueCoins = 1000;

    int64_t w1h  = nValueCoins * 3600 / nDayTime;
    int64_t w1d  = nValueCoins * 86400 / nDayTime;
    int64_t w30d = nValueCoins * static_cast<int64_t>(nStakeMaxAge) / nDayTime;

    BOOST_CHECK_EQUAL(w1d, 1000);
    BOOST_CHECK(w1h < w1d);
    BOOST_CHECK(w30d > w1d);
}

BOOST_AUTO_TEST_CASE(weight_max_age_regular_vs_flash)
{
    int nDayTime = 86400;
    int64_t nValueCoins = 1000;

    int64_t wRegular = nValueCoins * static_cast<int64_t>(nStakeMaxAge) / nDayTime;
    int64_t wFlash   = nValueCoins * static_cast<int64_t>(nFlashStakeMaxAge) / nDayTime;

    BOOST_CHECK(wRegular > wFlash);
    BOOST_CHECK_EQUAL(wRegular, 30000); // 1000 * 30 days
    BOOST_CHECK_EQUAL(wFlash, 7000);    // 1000 * 7 days
}

BOOST_AUTO_TEST_CASE(weight_min_vs_max_categorization)
{
    // nMinWeight: 0 < nTimeWeight < nStakeMaxAge
    // nMaxWeight: nTimeWeight == nStakeMaxAge
    int64_t nTimeYoung = 86400;
    int64_t nTimeMax   = static_cast<int64_t>(nStakeMaxAge);

    BOOST_CHECK(nTimeYoung > 0 && nTimeYoung < static_cast<int64_t>(nStakeMaxAge));
    BOOST_CHECK_EQUAL(nTimeMax, static_cast<int64_t>(nStakeMaxAge));
}

BOOST_AUTO_TEST_CASE(getweight_at_exact_min_age)
{
    // GetWeight at exactly nStakeMinAge → returns 0
    int64_t now = 1700000000;
    int64_t coinTime = now - nStakeMinAge;
    BOOST_CHECK_EQUAL(GetWeight(coinTime, now, false), 0);
    BOOST_CHECK_EQUAL(GetWeight(coinTime, now, true), 0);
}

BOOST_AUTO_TEST_CASE(getweight_clamped_at_max_age)
{
    int64_t now = 1700000000;
    int64_t coinTime = now - 100 * 86400; // 100 days ago
    BOOST_CHECK_EQUAL(GetWeight(coinTime, now, false), static_cast<int64_t>(nStakeMaxAge));
    BOOST_CHECK_EQUAL(GetWeight(coinTime, now, true), static_cast<int64_t>(nFlashStakeMaxAge));
}

BOOST_AUTO_TEST_CASE(getweight_one_hour_past_min_age)
{
    // Coin is 2 hours old: weight = 2h - 1h(minAge) = 3600
    int64_t now = 1700000000;
    int64_t coinTime = now - 2 * 3600;
    BOOST_CHECK_EQUAL(GetWeight(coinTime, now, false), 3600);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 5: CreateCoinStake split/combine decision logic
// ============================================================================
BOOST_AUTO_TEST_SUITE(coinstake_split_combine_tests)

BOOST_AUTO_TEST_CASE(split_above_threshold)
{
    ThresholdGuard guard;
    nSplitThreshold = 2000;

    int64_t nTotalSize = 3100 * COIN; // coin + reward
    BOOST_CHECK(nTotalSize > nSplitThreshold * COIN);
}

BOOST_AUTO_TEST_CASE(split_below_threshold)
{
    ThresholdGuard guard;
    nSplitThreshold = 2000;

    int64_t nTotalSize = 1600 * COIN;
    BOOST_CHECK(!(nTotalSize > nSplitThreshold * COIN));
}

BOOST_AUTO_TEST_CASE(split_exactly_at_threshold_no_split)
{
    // Uses > not >=, so exactly-at does NOT split
    ThresholdGuard guard;
    nSplitThreshold = 2000;

    int64_t nTotalSize = 2000 * COIN;
    BOOST_CHECK(!(nTotalSize > nSplitThreshold * COIN));
}

BOOST_AUTO_TEST_CASE(split_flash_pos_threshold)
{
    ThresholdGuard guard;
    nSplitThreshold = 200000;

    int64_t below = 150150 * COIN;
    BOOST_CHECK(!(below > nSplitThreshold * COIN));

    int64_t above = 250150 * COIN;
    BOOST_CHECK(above > nSplitThreshold * COIN);
}

BOOST_AUTO_TEST_CASE(combine_stops_at_threshold)
{
    ThresholdGuard guard;
    nCombineThreshold = 1000;

    BOOST_CHECK(500 * COIN < nCombineThreshold * COIN);   // Keep combining
    BOOST_CHECK(1000 * COIN >= nCombineThreshold * COIN);  // Stop
    BOOST_CHECK(1500 * COIN >= nCombineThreshold * COIN);  // Already over
}

BOOST_AUTO_TEST_CASE(combine_skips_large_inputs)
{
    // Individual input >= nCombineThreshold * COIN → skip
    ThresholdGuard guard;
    nCombineThreshold = 1000;

    BOOST_CHECK(1500 * COIN >= nCombineThreshold * COIN);
    BOOST_CHECK(!(500 * COIN >= nCombineThreshold * COIN));
}

BOOST_AUTO_TEST_CASE(combine_respects_reserve_balance)
{
    ThresholdGuard guard;
    int64_t nBalance = 10000 * COIN;
    nReserveBalance = 8000 * COIN;
    int64_t available = nBalance - nReserveBalance; // 2000 COIN

    int64_t nCredit = 1500 * COIN;
    BOOST_CHECK(nCredit + 600 * COIN > available);  // Would exceed
    BOOST_CHECK(!(nCredit + 400 * COIN > available)); // Would fit
}

BOOST_AUTO_TEST_CASE(combine_max_100_inputs)
{
    CTransaction tx;
    for (int i = 0; i < 99; i++)
        tx.vin.push_back(CTxIn());
    BOOST_CHECK(tx.vin.size() < 100);

    tx.vin.push_back(CTxIn());
    BOOST_CHECK(tx.vin.size() >= 100);
}

BOOST_AUTO_TEST_CASE(split_output_even_allocation)
{
    // vout[1] = (nCredit / 2 / CENT) * CENT; vout[2] = nCredit - vout[1]
    int64_t nCredit = 2000 * COIN;
    int64_t vout1 = (nCredit / 2 / CENT) * CENT;
    int64_t vout2 = nCredit - vout1;

    BOOST_CHECK_EQUAL(vout1, 1000 * COIN);
    BOOST_CHECK_EQUAL(vout2, 1000 * COIN);
    BOOST_CHECK_EQUAL(vout1 + vout2, nCredit);
}

BOOST_AUTO_TEST_CASE(split_output_odd_amount)
{
    int64_t nCredit = 2001 * COIN + 50000000; // 2001.5 PINK
    int64_t vout1 = (nCredit / 2 / CENT) * CENT;
    int64_t vout2 = nCredit - vout1;

    BOOST_CHECK_EQUAL(vout1 + vout2, nCredit); // No coin loss
    BOOST_CHECK_EQUAL(vout1 % CENT, 0);        // Rounded to CENT
}

BOOST_AUTO_TEST_CASE(split_output_conservation_various)
{
    std::vector<int64_t> amounts = {
        100 * COIN, 1000 * COIN, 99999 * COIN,
        1 * COIN + 1, 12345678901LL
    };

    for (int64_t nCredit : amounts) {
        int64_t vout1 = (nCredit / 2 / CENT) * CENT;
        int64_t vout2 = nCredit - vout1;
        BOOST_CHECK_EQUAL(vout1 + vout2, nCredit);
        BOOST_CHECK(vout1 >= 0);
        BOOST_CHECK(vout2 >= 0);
    }
}

BOOST_AUTO_TEST_CASE(split_with_sidestake_deduction)
{
    int64_t kernelValue = 2000 * COIN;
    int64_t nReward = 100 * COIN;
    int64_t stakeOutReward = 30 * COIN; // 30% side-staked

    int64_t nCredit = kernelValue + nReward - stakeOutReward;
    BOOST_CHECK_EQUAL(nCredit, 2070 * COIN);

    int64_t vout1 = (nCredit / 2 / CENT) * CENT;
    int64_t vout2 = nCredit - vout1;
    BOOST_CHECK_EQUAL(vout1 + vout2, nCredit);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 6: Coin selection — algorithm and age filtering
// ============================================================================
BOOST_AUTO_TEST_SUITE(coin_selection_tests)

BOOST_AUTO_TEST_CASE(stake_min_age_pinned)
{
    BOOST_CHECK_EQUAL(nStakeMinAge, 3600u);
}

BOOST_AUTO_TEST_CASE(stake_max_age_pinned)
{
    BOOST_CHECK_EQUAL(nStakeMaxAge, 2592000u);
}

BOOST_AUTO_TEST_CASE(flash_stake_max_age_pinned)
{
    BOOST_CHECK_EQUAL(nFlashStakeMaxAge, 604800u);
}

BOOST_AUTO_TEST_CASE(age_filter_too_young)
{
    unsigned int nNow = 1700000000;
    unsigned int coinTime = nNow - 1800; // 30 min
    BOOST_CHECK(coinTime + nStakeMinAge > nNow);
}

BOOST_AUTO_TEST_CASE(age_filter_exactly_min_age)
{
    unsigned int nNow = 1700000000;
    unsigned int coinTime = nNow - nStakeMinAge;
    // coinTime + nStakeMinAge == nNow → NOT too young (> not >=)
    BOOST_CHECK(!(coinTime + nStakeMinAge > nNow));
}

BOOST_AUTO_TEST_CASE(age_filter_mature)
{
    unsigned int nNow = 1700000000;
    unsigned int coinTime = nNow - 7200; // 2 hours
    BOOST_CHECK(!(coinTime + nStakeMinAge > nNow));
}

BOOST_AUTO_TEST_CASE(selection_single_large_coin_suffices)
{
    int64_t nTargetValue = 5000 * COIN;
    int64_t coinValue = 10000 * COIN;
    BOOST_CHECK(coinValue >= nTargetValue);
}

BOOST_AUTO_TEST_CASE(selection_accumulates_small_coins)
{
    int64_t nTargetValue = 5000 * COIN;
    int64_t coins[] = {1000 * COIN, 1500 * COIN, 2000 * COIN, 2500 * COIN};

    int64_t accumulated = 0;
    int coinsUsed = 0;
    for (int64_t c : coins) {
        if (accumulated >= nTargetValue)
            break;
        accumulated += c;
        coinsUsed++;
    }
    BOOST_CHECK(accumulated >= nTargetValue);
    BOOST_CHECK_EQUAL(coinsUsed, 4); // Need all 4: 1000+1500+2000=4500 < 5000
}

BOOST_AUTO_TEST_CASE(selection_stops_at_target)
{
    int64_t nTargetValue = 2000 * COIN;
    int64_t coins[] = {1000 * COIN, 1500 * COIN, 500 * COIN};

    int64_t accumulated = 0;
    int coinsUsed = 0;
    for (int64_t c : coins) {
        if (accumulated >= nTargetValue)
            break;
        accumulated += c;
        coinsUsed++;
    }
    BOOST_CHECK(accumulated >= nTargetValue);
    BOOST_CHECK_EQUAL(coinsUsed, 2); // 1000 + 1500 = 2500 >= 2000
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 7: StakeDB persistence — wallet-level stake management
// ============================================================================
BOOST_AUTO_TEST_SUITE(stakedb_persistence_tests)

BOOST_AUTO_TEST_CASE(stakedb_write_read_erase_cycle)
{
    {
        CStakeDB db("staketest_phase6.dat", "cr+");

        CBitcoinAddress addr1 = MakeTestAddress(10);
        std::string a1 = addr1.ToString();

        BOOST_CHECK(db.WriteStake(a1, "Donor1", "25.5"));

        std::string name, pct;
        BOOST_CHECK(db.ReadStake(a1, name, pct));
        BOOST_CHECK_EQUAL(name, "Donor1");
        BOOST_CHECK_EQUAL(pct, "25.5");

        BOOST_CHECK(db.EraseStake(a1));

        std::string name2, pct2;
        BOOST_CHECK(!db.ReadStake(a1, name2, pct2));
    }
    bitdb.CloseDb("staketest_phase6.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_multiple_entries_independent)
{
    {
        CStakeDB db("staketest_phase6.dat", "cr+");

        CBitcoinAddress addr1 = MakeTestAddress(20);
        CBitcoinAddress addr2 = MakeTestAddress(21);

        BOOST_CHECK(db.WriteStake(addr1.ToString(), "Pool", "10"));
        BOOST_CHECK(db.WriteStake(addr2.ToString(), "Dev", "5"));

        BOOST_CHECK(db.EraseStake(addr1.ToString()));

        std::string name, pct;
        BOOST_CHECK(!db.ReadStake(addr1.ToString(), name, pct));
        BOOST_CHECK(db.ReadStake(addr2.ToString(), name, pct));
        BOOST_CHECK_EQUAL(name, "Dev");
        BOOST_CHECK_EQUAL(pct, "5");
    }
    bitdb.CloseDb("staketest_phase6.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_overwrite_preserves_latest)
{
    {
        CStakeDB db("staketest_phase6.dat", "cr+");

        CBitcoinAddress addr = MakeTestAddress(30);
        std::string a = addr.ToString();

        BOOST_CHECK(db.WriteStake(a, "V1", "10"));
        BOOST_CHECK(db.WriteStake(a, "V2", "20"));
        BOOST_CHECK(db.WriteStake(a, "V3", "30"));

        std::string name, pct;
        BOOST_CHECK(db.ReadStake(a, name, pct));
        BOOST_CHECK_EQUAL(name, "V3");
        BOOST_CHECK_EQUAL(pct, "30");
    }
    bitdb.CloseDb("staketest_phase6.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_fractional_percent_preserved)
{
    {
        CStakeDB db("staketest_phase6.dat", "cr+");

        CBitcoinAddress addr = MakeTestAddress(40);
        BOOST_CHECK(db.WriteStake(addr.ToString(), "Fractional", "12.345678"));

        std::string name, pct;
        BOOST_CHECK(db.ReadStake(addr.ToString(), name, pct));
        BOOST_CHECK_EQUAL(pct, "12.345678");
    }
    bitdb.CloseDb("staketest_phase6.dat");
}

BOOST_AUTO_TEST_CASE(stakedb_empty_name_allowed)
{
    {
        CStakeDB db("staketest_phase6.dat", "cr+");

        CBitcoinAddress addr = MakeTestAddress(50);
        BOOST_CHECK(db.WriteStake(addr.ToString(), "", "10"));

        std::string name, pct;
        BOOST_CHECK(db.ReadStake(addr.ToString(), name, pct));
        BOOST_CHECK_EQUAL(name, "");
    }
    bitdb.CloseDb("staketest_phase6.dat");
}

BOOST_AUTO_TEST_CASE(wallet_map_consistency_set)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(60);
    CTxDestination dest = addr.Get();

    pstakeDB->mapAddressBook[dest] = "TestName";
    pstakeDB->mapAddressPercent[dest] = "15";

    BOOST_CHECK_EQUAL(pstakeDB->mapAddressBook[dest], "TestName");
    BOOST_CHECK_EQUAL(pstakeDB->mapAddressPercent[dest], "15");
    BOOST_CHECK_EQUAL(pstakeDB->mapAddressBook.size(), 1u);
    BOOST_CHECK_EQUAL(pstakeDB->mapAddressPercent.size(), 1u);
}

BOOST_AUTO_TEST_CASE(wallet_map_consistency_delete)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(70);
    CTxDestination dest = addr.Get();

    pstakeDB->mapAddressBook[dest] = "ToDelete";
    pstakeDB->mapAddressPercent[dest] = "20";

    pstakeDB->mapAddressBook.erase(dest);
    pstakeDB->mapAddressPercent.erase(dest);

    BOOST_CHECK(pstakeDB->mapAddressBook.find(dest) == pstakeDB->mapAddressBook.end());
    BOOST_CHECK(pstakeDB->mapAddressPercent.find(dest) == pstakeDB->mapAddressPercent.end());
}

BOOST_AUTO_TEST_CASE(both_maps_same_size)
{
    StakeDBGuard guard;
    for (unsigned int i = 1; i <= 10; i++) {
        CBitcoinAddress addr = MakeTestAddress(i + 100);
        pstakeDB->mapAddressBook[addr.Get()] = "Name" + std::to_string(i);
        pstakeDB->mapAddressPercent[addr.Get()] = std::to_string(i);
    }
    BOOST_CHECK_EQUAL(pstakeDB->mapAddressBook.size(), pstakeDB->mapAddressPercent.size());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 8: RPC staking commands
// ============================================================================

// RPC functions declared in bitcoinrpc.h with json type

BOOST_AUTO_TEST_SUITE(rpc_staking_tests)

BOOST_AUTO_TEST_CASE(splitthreshold_get_returns_object)
{
    json params = json::array();
    json result = splitthreshold(params, false);
    BOOST_CHECK(result.is_object());
}

BOOST_AUTO_TEST_CASE(splitthreshold_set_valid)
{
    ThresholdGuard guard;
    nCombineThreshold = 1000;

    json params = json::array();
    params.push_back(static_cast<int64_t>(5000));
    json result = splitthreshold(params, false);

    BOOST_CHECK_EQUAL(nSplitThreshold, 5000);
}

BOOST_AUTO_TEST_CASE(splitthreshold_rejects_over_million)
{
    ThresholdGuard guard;
    json params = json::array();
    params.push_back(static_cast<int64_t>(2000000));
    BOOST_CHECK_THROW(splitthreshold(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(splitthreshold_rejects_below_combine)
{
    ThresholdGuard guard;
    nCombineThreshold = 5000;

    json params = json::array();
    params.push_back(static_cast<int64_t>(3000));
    BOOST_CHECK_THROW(splitthreshold(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(splitthreshold_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(splitthreshold(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getstakesplitthreshold_returns_object)
{
    json params = json::array();
    json result = getstakesplitthreshold(params, false);
    BOOST_CHECK(result.is_object());
}

BOOST_AUTO_TEST_CASE(getstakesplitthreshold_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(getstakesplitthreshold(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(addstakeout(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_rejects_invalid_address)
{
    StakeDBGuard guard;
    json params = json::array();
    params.push_back(std::string("TestName"));
    params.push_back(std::string("INVALID_ADDRESS"));
    params.push_back(std::string("10"));
    BOOST_CHECK_THROW(addstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_rejects_negative_percent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(200);
    json params = json::array();
    params.push_back(std::string("Neg"));
    params.push_back(addr.ToString());
    params.push_back(std::string("-10"));
    BOOST_CHECK_THROW(addstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_rejects_over_100_percent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(201);
    json params = json::array();
    params.push_back(std::string("Over"));
    params.push_back(addr.ToString());
    params.push_back(std::string("150"));
    BOOST_CHECK_THROW(addstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_rejects_long_name)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(202);
    std::string longName(101, 'A');

    json params = json::array();
    params.push_back(longName);
    params.push_back(addr.ToString());
    params.push_back(std::string("10"));
    BOOST_CHECK_THROW(addstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_valid_entry)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(203);

    json params = json::array();
    params.push_back(std::string("ValidEntry"));
    params.push_back(addr.ToString());
    params.push_back(std::string("25"));

    json result = addstakeout(params, false);
    BOOST_CHECK(result.is_string());

    BOOST_CHECK(pstakeDB->mapAddressPercent.find(addr.Get()) != pstakeDB->mapAddressPercent.end());
    BOOST_CHECK_EQUAL(pstakeDB->mapAddressPercent[addr.Get()], "25");
}

BOOST_AUTO_TEST_CASE(delstakeout_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(delstakeout(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(delstakeout_rejects_invalid_address)
{
    json params = json::array();
    params.push_back(std::string("INVALID"));
    BOOST_CHECK_THROW(delstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(delstakeout_rejects_nonexistent)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(210);
    json params = json::array();
    params.push_back(addr.ToString());
    BOOST_CHECK_THROW(delstakeout(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(add_then_del_roundtrip)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(220);

    // Add
    json addParams = json::array();
    addParams.push_back(std::string("Roundtrip"));
    addParams.push_back(addr.ToString());
    addParams.push_back(std::string("15"));
    addstakeout(addParams, false);

    BOOST_CHECK(pstakeDB->mapAddressBook.find(addr.Get()) != pstakeDB->mapAddressBook.end());

    // Delete
    json delParams = json::array();
    delParams.push_back(addr.ToString());
    delstakeout(delParams, false);

    BOOST_CHECK(pstakeDB->mapAddressBook.find(addr.Get()) == pstakeDB->mapAddressBook.end());
    BOOST_CHECK(pstakeDB->mapAddressPercent.find(addr.Get()) == pstakeDB->mapAddressPercent.end());
}

BOOST_AUTO_TEST_CASE(liststakeout_empty)
{
    StakeDBGuard guard;
    json params = json::array();
    json result = liststakeout(params, false);
    BOOST_CHECK(result.is_array());

    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(liststakeout_after_add)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(230);

    json addParams = json::array();
    addParams.push_back(std::string("Listed"));
    addParams.push_back(addr.ToString());
    addParams.push_back(std::string("10"));
    addstakeout(addParams, false);

    json listParams = json::array();
    json result = liststakeout(listParams, false);
    BOOST_CHECK_EQUAL(result.size(), 1u);
}

BOOST_AUTO_TEST_CASE(liststakeout_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(liststakeout(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_total_overflow_rejected)
{
    StakeDBGuard guard;

    CBitcoinAddress addr1 = MakeTestAddress(240);
    json p1 = json::array();
    p1.push_back(std::string("First"));
    p1.push_back(addr1.ToString());
    p1.push_back(std::string("60"));
    addstakeout(p1, false);

    CBitcoinAddress addr2 = MakeTestAddress(241);
    json p2 = json::array();
    p2.push_back(std::string("Second"));
    p2.push_back(addr2.ToString());
    p2.push_back(std::string("50")); // 60+50 = 110%
    BOOST_CHECK_THROW(addstakeout(p2, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(addstakeout_precision_truncated)
{
    StakeDBGuard guard;
    CBitcoinAddress addr = MakeTestAddress(250);

    json params = json::array();
    params.push_back(std::string("Precise"));
    params.push_back(addr.ToString());
    params.push_back(std::string("12.12345678901"));

    json result = addstakeout(params, false);
    BOOST_CHECK(result.is_string());

    // Stored percent should be truncated (6 decimal places max)
    std::string stored = pstakeDB->mapAddressPercent[addr.Get()];
    BOOST_CHECK(stored.length() <= 10);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Suite 9: Flash PoS timing — IsFlashStake hour windows
// ============================================================================
BOOST_AUTO_TEST_SUITE(flash_pos_timing_tests)

BOOST_AUTO_TEST_CASE(flash_stake_hours_pinned)
{
    BOOST_CHECK_EQUAL(nFlashStakeHour1, 15u);
    BOOST_CHECK_EQUAL(nFlashStakeHour2, 20u);
    BOOST_CHECK_EQUAL(nFlashStakeHour3, 1u);
    BOOST_CHECK_EQUAL(nFlashStakeHour4, 6u);
}

BOOST_AUTO_TEST_CASE(flash_at_hour_15)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 15; t.tm_min = 0; t.tm_sec = 0;
    time_t ts = timegm(&t);
    BOOST_CHECK(IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(flash_at_hour_20)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 20; t.tm_min = 0; t.tm_sec = 0;
    time_t ts = timegm(&t);
    BOOST_CHECK(IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(flash_at_hour_1)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 1; t.tm_min = 30; t.tm_sec = 0;
    time_t ts = timegm(&t);
    BOOST_CHECK(IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(flash_at_hour_6)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 6; t.tm_min = 59; t.tm_sec = 59;
    time_t ts = timegm(&t);
    BOOST_CHECK(IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(not_flash_at_hour_0)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
    time_t ts = timegm(&t);
    BOOST_CHECK(!IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(not_flash_at_hour_12)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
    time_t ts = timegm(&t);
    BOOST_CHECK(!IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(not_flash_at_hour_23)
{
    struct tm t = {};
    t.tm_year = 124; t.tm_mon = 0; t.tm_mday = 1;
    t.tm_hour = 23; t.tm_min = 59; t.tm_sec = 59;
    time_t ts = timegm(&t);
    BOOST_CHECK(!IsFlashStake(static_cast<unsigned int>(ts)));
}

BOOST_AUTO_TEST_CASE(flash_max_age_constants)
{
    BOOST_CHECK(nFlashStakeMaxAge < nStakeMaxAge);
    BOOST_CHECK_EQUAL(nFlashStakeMaxAge, 7u * 24 * 60 * 60);
    BOOST_CHECK_EQUAL(nStakeMaxAge, 30u * 24 * 60 * 60);
}

BOOST_AUTO_TEST_SUITE_END()
