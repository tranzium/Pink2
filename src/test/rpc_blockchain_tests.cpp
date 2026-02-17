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

using namespace std;
using namespace json_spirit;

extern int64_t nTransactionFee;

// ============================================================================
// Suite: Blockchain RPC commands (uses TestChain for real chain state)
// ============================================================================
BOOST_FIXTURE_TEST_SUITE(rpc_blockchain_tests, TestChain)

BOOST_AUTO_TEST_CASE(getblockcount_returns_height)
{
    Array params;
    Value result = getblockcount(params, false);
    BOOST_CHECK_EQUAL(result.get_int(), nBestHeight);
    BOOST_CHECK(result.get_int() >= 50);
}

BOOST_AUTO_TEST_CASE(getblockcount_help_throws)
{
    Array params;
    BOOST_CHECK_THROW(getblockcount(params, true), runtime_error);
}

BOOST_AUTO_TEST_CASE(getbestblockhash_matches_chain)
{
    Array params;
    Value result = getbestblockhash(params, false);
    BOOST_CHECK_EQUAL(result.get_str(), hashBestChain.GetHex());
}

BOOST_AUTO_TEST_CASE(getdifficulty_returns_object)
{
    Array params;
    Value result = getdifficulty(params, false);
    Object obj = result.get_obj();

    // Must contain all expected keys
    BOOST_CHECK(find_value(obj, "proof-of-work").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake (flash)").type() == real_type);
    BOOST_CHECK(find_value(obj, "search-interval").type() == int_type);
}

BOOST_AUTO_TEST_CASE(getdifficulty_pow_positive)
{
    Array params;
    Value result = getdifficulty(params, false);
    Object obj = result.get_obj();

    double powDiff = find_value(obj, "proof-of-work").get_real();
    BOOST_CHECK(powDiff > 0.0);
}

BOOST_AUTO_TEST_CASE(settxfee_valid)
{
    int64_t savedFee = nTransactionFee;

    Array params;
    params.push_back(0.01);
    Value result = settxfee(params, false);
    BOOST_CHECK_EQUAL(result.get_bool(), true);
    BOOST_CHECK(nTransactionFee >= MIN_TX_FEE);

    // Restore
    nTransactionFee = savedFee;
}

BOOST_AUTO_TEST_CASE(settxfee_below_min_throws)
{
    // Fee of 0 → AmountFromValue throws JSONRPCError (Object) because 0.0 <= 0.0
    Array params;
    params.push_back(0.0);
    BOOST_CHECK_THROW(settxfee(params, false), Object);
}

BOOST_AUTO_TEST_CASE(getrawmempool_empty)
{
    // Ensure mempool is empty
    ClearMempool();

    Array params;
    Value result = getrawmempool(params, false);
    Array arr = result.get_array();
    BOOST_CHECK(arr.empty());
}

BOOST_AUTO_TEST_CASE(getrawmempool_with_tx)
{
    BOOST_REQUIRE(IsCoinbaseMature(0));

    CTransaction tx = CreateSpendTx(0, CScript() << OP_TRUE, 1 * COIN);
    AddToMempool(tx);

    Array params;
    Value result = getrawmempool(params, false);
    Array arr = result.get_array();
    BOOST_CHECK(!arr.empty());

    // The tx hash should appear in the result
    bool found = false;
    string txhash = tx.GetHash().ToString();
    for (const Value& v : arr) {
        if (v.get_str() == txhash) {
            found = true;
            break;
        }
    }
    BOOST_CHECK(found);

    ClearMempool();
}

BOOST_AUTO_TEST_CASE(getblockhash_genesis)
{
    Array params;
    params.push_back(0);
    Value result = getblockhash(params, false);

    string hashHex = result.get_str();
    BOOST_CHECK(!hashHex.empty());
    BOOST_CHECK(IsHex(hashHex));
    BOOST_CHECK_EQUAL(hashHex, pindexGenesisBlock->GetBlockHash().GetHex());
}

BOOST_AUTO_TEST_CASE(getblockhash_out_of_range)
{
    Array params;
    params.push_back(nBestHeight + 100);
    BOOST_CHECK_THROW(getblockhash(params, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(getblock_by_hash)
{
    // Get genesis hash, then query getblock
    string genesisHash = pindexGenesisBlock->GetBlockHash().GetHex();

    Array params;
    params.push_back(genesisHash);
    Value result = getblock(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "hash").get_str(), genesisHash);
    BOOST_CHECK_EQUAL(find_value(obj, "height").get_int(), 0);
    BOOST_CHECK(find_value(obj, "tx").get_array().size() >= 1);
}

BOOST_AUTO_TEST_CASE(getblock_unknown_hash_throws)
{
    Array params;
    params.push_back(string("0000000000000000000000000000000000000000000000000000000000000bad"));
    BOOST_CHECK_THROW(getblock(params, false), Object);
}

BOOST_AUTO_TEST_CASE(getblockbynumber_height_one)
{
    Array params;
    params.push_back(1);
    Value result = getblockbynumber(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK_EQUAL(find_value(obj, "height").get_int(), 1);
    BOOST_CHECK(find_value(obj, "hash").type() == str_type);
    BOOST_CHECK(find_value(obj, "tx").get_array().size() >= 1);
}

BOOST_AUTO_TEST_CASE(getblockbynumber_out_of_range)
{
    Array params;
    params.push_back(nBestHeight + 100);
    BOOST_CHECK_THROW(getblockbynumber(params, false), runtime_error);
}

BOOST_AUTO_TEST_CASE(getcheckpoint_returns_object)
{
    Array params;
    Value result = getcheckpoint(params, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "synccheckpoint").type() == str_type);
    BOOST_CHECK(find_value(obj, "height").type() == int_type);
    BOOST_CHECK(find_value(obj, "timestamp").type() == str_type);
}

BOOST_AUTO_TEST_SUITE_END()
