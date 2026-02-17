// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for mining/staking RPC commands: getsubsidy, getmininginfo,
// getstakinginfo.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "main.h"
#include "test_framework.h"

using namespace std;
using namespace json_spirit;

extern CWallet* pwalletMain;

// ============================================================================
// Suite: Mining RPC commands (uses TestChain for real chain state)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_mining_tests, TestChain)

BOOST_AUTO_TEST_CASE(getsubsidy_default)
{
    // No params → subsidy for nBestHeight+1
    Array params;
    Value result = getsubsidy(params, false);
    uint64_t subsidy = result.get_uint64();
    // nBestHeight+1 is in the 2-16999 range (0 subsidy), or height 1 (premine)
    // Since we mined 50 blocks, next block is ~51 → 0 subsidy
    BOOST_CHECK_EQUAL(subsidy, 0u);
}

BOOST_AUTO_TEST_CASE(getsubsidy_height_one)
{
    // Height 1 is the premine: 364,800,000 COIN
    Array params;
    params.push_back(string("1"));
    Value result = getsubsidy(params, false);
    uint64_t subsidy = result.get_uint64();
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(364800000LL * COIN));
}

BOOST_AUTO_TEST_CASE(getsubsidy_zero_range)
{
    // Heights 2-16999 have 0 subsidy
    Array params;
    params.push_back(string("100"));
    Value result = getsubsidy(params, false);
    BOOST_CHECK_EQUAL(result.get_uint64(), 0u);
}

BOOST_AUTO_TEST_CASE(getsubsidy_first_halving)
{
    // Height 846800: nHalving = 846800 / 2 / 423400 = 1, subsidy = (50*COIN) >> 1 = 25 COIN
    Array params;
    params.push_back(string("846800"));
    Value result = getsubsidy(params, false);
    uint64_t subsidy = result.get_uint64();
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(25 * COIN));
}

BOOST_AUTO_TEST_CASE(getsubsidy_pre_halving)
{
    // Height 16240: 100 * COIN (nHalving=0)
    Array params;
    params.push_back(string("16240"));
    Value result = getsubsidy(params, false);
    uint64_t subsidy = result.get_uint64();
    // GetProofOfWorkReward at this height with 0 fees
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(GetProofOfWorkReward(16240, 0)));
}

BOOST_AUTO_TEST_CASE(getsubsidy_help_throws)
{
    Array params;
    BOOST_CHECK_THROW(getsubsidy(params, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getmininginfo_returns_object)
{
    Array params;
    Value result = getmininginfo(params, false);
    Object obj = result.get_obj();

    // Verify key fields are present
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK_EQUAL(find_value(obj, "blocks").get_int(), nBestHeight);
    BOOST_CHECK(find_value(obj, "pooledtx").type() == int_type);
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
}

BOOST_AUTO_TEST_CASE(getmininginfo_staking_object)
{
    Array params;
    Value result = getmininginfo(params, false);
    Object obj = result.get_obj();

    // "staking" is a nested object
    Object staking = find_value(obj, "staking").get_obj();
    BOOST_CHECK(find_value(staking, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(staking, "targeting-fpos").type() == bool_type);
}

BOOST_AUTO_TEST_CASE(getmininginfo_difficulty_object)
{
    Array params;
    Value result = getmininginfo(params, false);
    Object obj = result.get_obj();

    // "difficulty" is a nested object
    Object diff = find_value(obj, "difficulty").get_obj();
    BOOST_CHECK(find_value(diff, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake(flash)").type() == real_type);
}

BOOST_AUTO_TEST_CASE(getstakinginfo_returns_object)
{
    Array params;
    Value result = getstakinginfo(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(obj, "staking").type() == bool_type);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);
    BOOST_CHECK(find_value(obj, "difficulty").type() == real_type);
    BOOST_CHECK(find_value(obj, "weight").type() == int_type);
    BOOST_CHECK(find_value(obj, "netstakeweight").type() == int_type);
    BOOST_CHECK(find_value(obj, "expectedtime").type() == int_type);
}

BOOST_AUTO_TEST_CASE(getmininginfo_pooledtx_matches_mempool)
{
    // Ensure mempool is clear
    ClearMempool();

    Array params;
    Value result = getmininginfo(params, false);
    Object obj = result.get_obj();
    BOOST_CHECK_EQUAL(find_value(obj, "pooledtx").get_uint64(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
