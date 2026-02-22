// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for mining/staking RPC commands: getsubsidy, getmininginfo,
// getstakinginfo.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "main.h"
#include "test_framework.h"

extern CWallet* pwalletMain;

// ============================================================================
// Suite: Mining RPC commands (uses TestChain for real chain state)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_mining_tests, TestChain)

BOOST_AUTO_TEST_CASE(getsubsidy_default)
{
    // No params -> subsidy for nBestHeight+1
    json params = json::array();
    json result = getsubsidy(params, false);
    uint64_t subsidy = result.get<uint64_t>();
    // nBestHeight+1 is in the 2-16999 range (0 subsidy), or height 1 (premine)
    // Since we mined 50 blocks, next block is ~51 -> 0 subsidy
    BOOST_CHECK_EQUAL(subsidy, 0u);
}

BOOST_AUTO_TEST_CASE(getsubsidy_height_one)
{
    // Height 1 is the premine: 364,800,000 COIN
    json params = json::array();
    params.push_back(std::string("1"));
    json result = getsubsidy(params, false);
    uint64_t subsidy = result.get<uint64_t>();
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(364800000LL * COIN));
}

BOOST_AUTO_TEST_CASE(getsubsidy_zero_range)
{
    // Heights 2-16999 have 0 subsidy
    json params = json::array();
    params.push_back(std::string("100"));
    json result = getsubsidy(params, false);
    BOOST_CHECK_EQUAL(result.get<uint64_t>(), 0u);
}

BOOST_AUTO_TEST_CASE(getsubsidy_first_halving)
{
    // Height 846800: nHalving = 846800 / 2 / 423400 = 1, subsidy = (50*COIN) >> 1 = 25 COIN
    json params = json::array();
    params.push_back(std::string("846800"));
    json result = getsubsidy(params, false);
    uint64_t subsidy = result.get<uint64_t>();
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(25 * COIN));
}

BOOST_AUTO_TEST_CASE(getsubsidy_pre_halving)
{
    // Height 16240: 100 * COIN (nHalving=0)
    json params = json::array();
    params.push_back(std::string("16240"));
    json result = getsubsidy(params, false);
    uint64_t subsidy = result.get<uint64_t>();
    // GetProofOfWorkReward at this height with 0 fees
    BOOST_CHECK_EQUAL(subsidy, static_cast<uint64_t>(GetProofOfWorkReward(16240, 0)));
}

BOOST_AUTO_TEST_CASE(getsubsidy_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(getsubsidy(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getmininginfo_returns_object)
{
    json params = json::array();
    json result = getmininginfo(params, false);

    // Verify key fields are present
    BOOST_CHECK(result["blocks"].is_number_integer());
    BOOST_CHECK_EQUAL(result["blocks"].get<int>(), nBestHeight);
    BOOST_CHECK(result["pooledtx"].is_number_integer());
    BOOST_CHECK(result["testnet"].is_boolean());
}

BOOST_AUTO_TEST_CASE(getmininginfo_staking_object)
{
    json params = json::array();
    json result = getmininginfo(params, false);

    // "staking" is a nested object
    json staking = result["staking"];
    BOOST_CHECK(staking["enabled"].is_boolean());
    BOOST_CHECK(staking["targeting-fpos"].is_boolean());
}

BOOST_AUTO_TEST_CASE(getmininginfo_difficulty_object)
{
    json params = json::array();
    json result = getmininginfo(params, false);

    // "difficulty" is a nested object
    json diff = result["difficulty"];
    BOOST_CHECK(diff["proof-of-stake"].is_number_float());
    BOOST_CHECK(diff["proof-of-stake(flash)"].is_number_float());
}

BOOST_AUTO_TEST_CASE(getstakinginfo_returns_object)
{
    json params = json::array();
    json result = getstakinginfo(params, false);

    BOOST_CHECK(result["enabled"].is_boolean());
    BOOST_CHECK(result["staking"].is_boolean());
    BOOST_CHECK(result["errors"].is_string());
    BOOST_CHECK(result["difficulty"].is_number_float());
    BOOST_CHECK(result["weight"].is_number_integer());
    BOOST_CHECK(result["netstakeweight"].is_number_integer());
    BOOST_CHECK(result["expectedtime"].is_number_integer());
}

BOOST_AUTO_TEST_CASE(getmininginfo_pooledtx_matches_mempool)
{
    // Ensure mempool is clear
    ClearMempool();

    json params = json::array();
    json result = getmininginfo(params, false);
    BOOST_CHECK_EQUAL(result["pooledtx"].get<uint64_t>(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
