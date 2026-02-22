// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for blockchain RPC commands: getblockcount, getbestblockhash,
// getdifficulty, settxfee, getrawmempool, getblockhash, getblock,
// getblockbynumber, getcheckpoint.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "main.h"
#include "test_framework.h"

extern int64_t nTransactionFee;

// ============================================================================
// Suite: Blockchain RPC commands (uses TestChain for real chain state)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_blockchain_tests, TestChain)

BOOST_AUTO_TEST_CASE(getblockcount_returns_height)
{
    json params = json::array();
    json result = getblockcount(params, false);
    BOOST_CHECK_EQUAL(result.get<int>(), nBestHeight);
    BOOST_CHECK(result.get<int>() >= 50);
}

BOOST_AUTO_TEST_CASE(getblockcount_help_throws)
{
    json params = json::array();
    BOOST_CHECK_THROW(getblockcount(params, true), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getbestblockhash_matches_chain)
{
    json params = json::array();
    json result = getbestblockhash(params, false);
    BOOST_CHECK_EQUAL(result.get<std::string>(), hashBestChain.GetHex());
}

BOOST_AUTO_TEST_CASE(getdifficulty_returns_object)
{
    json params = json::array();
    json result = getdifficulty(params, false);
    json obj = result;

    // Must contain all expected keys
    BOOST_CHECK(obj["proof-of-work"].is_number_float());
    BOOST_CHECK(obj["proof-of-stake"].is_number_float());
    BOOST_CHECK(obj["proof-of-stake (flash)"].is_number_float());
    BOOST_CHECK(obj["search-interval"].is_number_integer());
}

BOOST_AUTO_TEST_CASE(getdifficulty_pow_positive)
{
    json params = json::array();
    json result = getdifficulty(params, false);

    double powDiff = result["proof-of-work"].get<double>();
    BOOST_CHECK(powDiff > 0.0);
}

BOOST_AUTO_TEST_CASE(settxfee_valid)
{
    int64_t savedFee = nTransactionFee;

    json params = json::array();
    params.push_back(0.01);
    json result = settxfee(params, false);
    BOOST_CHECK_EQUAL(result.get<bool>(), true);
    BOOST_CHECK(nTransactionFee >= MIN_TX_FEE);

    // Restore
    nTransactionFee = savedFee;
}

BOOST_AUTO_TEST_CASE(settxfee_below_min_throws)
{
    // Fee of 0 -> AmountFromValue throws JSONRPCError (json) because 0.0 <= 0.0
    json params = json::array();
    params.push_back(0.0);
    BOOST_CHECK_THROW(settxfee(params, false), json);
}

BOOST_AUTO_TEST_CASE(getrawmempool_empty)
{
    // Ensure mempool is empty
    ClearMempool();

    json params = json::array();
    json result = getrawmempool(params, false);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(getrawmempool_with_tx)
{
    BOOST_REQUIRE(IsCoinbaseMature(0));

    CTransaction tx = CreateSpendTx(0, CScript() << OP_TRUE, 1 * COIN);
    AddToMempool(tx);

    json params = json::array();
    json result = getrawmempool(params, false);
    BOOST_CHECK(!result.empty());

    // The tx hash should appear in the result
    bool found = false;
    std::string txhash = tx.GetHash().ToString();
    for (const json& v : result) {
        if (v.get<std::string>() == txhash) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);

    ClearMempool();
}

BOOST_AUTO_TEST_CASE(getblockhash_genesis)
{
    json params = json::array();
    params.push_back(0);
    json result = getblockhash(params, false);

    std::string hashHex = result.get<std::string>();
    BOOST_CHECK(!hashHex.empty());
    BOOST_CHECK(IsHex(hashHex));
    BOOST_CHECK_EQUAL(hashHex, pindexGenesisBlock->GetBlockHash().GetHex());
}

BOOST_AUTO_TEST_CASE(getblockhash_out_of_range)
{
    json params = json::array();
    params.push_back(nBestHeight + 100);
    BOOST_CHECK_THROW(getblockhash(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getblock_by_hash)
{
    // Get genesis hash, then query getblock
    std::string genesisHash = pindexGenesisBlock->GetBlockHash().GetHex();

    json params = json::array();
    params.push_back(genesisHash);
    json result = getblock(params, false);

    BOOST_CHECK_EQUAL(result["hash"].get<std::string>(), genesisHash);
    BOOST_CHECK_EQUAL(result["height"].get<int>(), 0);
    BOOST_CHECK(result["tx"].size() >= 1);
}

BOOST_AUTO_TEST_CASE(getblock_unknown_hash_throws)
{
    json params = json::array();
    params.push_back(std::string("0000000000000000000000000000000000000000000000000000000000000bad"));
    BOOST_CHECK_THROW(getblock(params, false), json);
}

BOOST_AUTO_TEST_CASE(getblockbynumber_height_one)
{
    json params = json::array();
    params.push_back(1);
    json result = getblockbynumber(params, false);

    BOOST_CHECK_EQUAL(result["height"].get<int>(), 1);
    BOOST_CHECK(result["hash"].is_string());
    BOOST_CHECK(result["tx"].size() >= 1);
}

BOOST_AUTO_TEST_CASE(getblockbynumber_out_of_range)
{
    json params = json::array();
    params.push_back(nBestHeight + 100);
    BOOST_CHECK_THROW(getblockbynumber(params, false), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(getcheckpoint_returns_object)
{
    json params = json::array();
    json result = getcheckpoint(params, false);

    BOOST_CHECK(result["synccheckpoint"].is_string());
    BOOST_CHECK(result["height"].is_number_integer());
    BOOST_CHECK(result["timestamp"].is_string());
}

BOOST_AUTO_TEST_SUITE_END()
