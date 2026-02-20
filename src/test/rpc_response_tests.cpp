// Copyright (c) 2024-2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Pre-Phase 6D test hardening: RPC response structure contracts.
// Pins field names, types, and consensus values for all RPC commands.
// Contract-based: checks field presence + type, not exact JSON strings.
// Adding new fields to responses will NOT break these tests.
// Only removing or renaming a field triggers failure.

#include <boost/test/unit_test.hpp>

#include "bitcoinrpc.h"
#include "main.h"
#include "wallet.h"
#include "base58.h"
#include "init.h"
#include "test_framework.h"

using namespace json_spirit;
using namespace std;

extern CWallet* pwalletMain;
extern Value help(const Array& params, bool fHelp);

// ============================================================================
// Suite: rpc_response_info — info & status RPC response contracts
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(rpc_response_info, TestChain)

// ---------------------------------------------------------------------------
// getinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getinfo_response_contract)
{
    Array p;
    Value result = getinfo(p, false);
    Object obj = result.get_obj();

    // String fields
    BOOST_CHECK(find_value(obj, "version").type() == str_type);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);

    // Integer fields
    BOOST_CHECK(find_value(obj, "protocolversion").type() == int_type);
    BOOST_CHECK_EQUAL(find_value(obj, "protocolversion").get_int(), 60019);
    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "connections").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);

    // Real (double) fields — ValueFromAmount returns double
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "newmint").type() == real_type);
    BOOST_CHECK(find_value(obj, "stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "moneysupply").type() == real_type);
    BOOST_CHECK(find_value(obj, "paytxfee").type() == real_type);

    // Boolean fields
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "testnet").get_bool(), false);

    // Nested object: difficulty
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    Object diff = find_value(obj, "difficulty").get_obj();
    BOOST_CHECK(find_value(diff, "proof-of-work").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake").type() == real_type);
}

// ---------------------------------------------------------------------------
// getmininginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getmininginfo_response_contract)
{
    Array p;
    Value result = getmininginfo(p, false);
    Object obj = result.get_obj();

    // Core fields always present
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "next-block-value-pos").type() == real_type);
    BOOST_CHECK(find_value(obj, "last-block-size").type() == int_type);
    BOOST_CHECK(find_value(obj, "last-block-tx").type() == int_type);
    BOOST_CHECK(find_value(obj, "pooledtx").type() == int_type);
    BOOST_CHECK(find_value(obj, "tx-fee").type() == real_type);

    // Staking sub-object
    BOOST_CHECK(find_value(obj, "staking").type() == obj_type);
    Object staking = find_value(obj, "staking").get_obj();
    BOOST_CHECK(find_value(staking, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(staking, "targeting-fpos").type() == bool_type);
    BOOST_CHECK(find_value(staking, "estimated-time").type() == int_type);
    BOOST_CHECK(find_value(staking, "search-interval").type() == int_type);
    BOOST_CHECK(find_value(staking, "utxo-combine-threshold").type() == int_type);
    BOOST_CHECK(find_value(staking, "utxo-split-threshold").type() == int_type);

    // Stake weight sub-object
    BOOST_CHECK(find_value(obj, "stakeweight").type() == obj_type);
    Object sw = find_value(obj, "stakeweight").get_obj();
    BOOST_CHECK(find_value(sw, "minimum").type() == int_type);
    BOOST_CHECK(find_value(sw, "maximum").type() == int_type);
    BOOST_CHECK(find_value(sw, "combined").type() == int_type);
    BOOST_CHECK(find_value(sw, "network").type() == int_type);

    // Difficulty sub-object
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    Object diff = find_value(obj, "difficulty").get_obj();
    BOOST_CHECK(find_value(diff, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(diff, "proof-of-stake(flash)").type() == real_type);

    // Top-level scalars
    BOOST_CHECK(find_value(obj, "netstakeweight").type() == int_type);
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK_EQUAL(find_value(obj, "testnet").get_bool(), false);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);
}

// ---------------------------------------------------------------------------
// getstakinginfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getstakinginfo_response_contract)
{
    Array p;
    Value result = getstakinginfo(p, false);
    Object obj = result.get_obj();

    // Boolean fields
    BOOST_CHECK(find_value(obj, "enabled").type() == bool_type);
    BOOST_CHECK(find_value(obj, "staking").type() == bool_type);

    // String fields
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);

    // Integer fields (uint64_t → int_type in json_spirit)
    BOOST_CHECK(find_value(obj, "currentblocksize").type() == int_type);
    BOOST_CHECK(find_value(obj, "currentblocktx").type() == int_type);
    BOOST_CHECK(find_value(obj, "pooledtx").type() == int_type);
    BOOST_CHECK(find_value(obj, "search-interval").type() == int_type);
    BOOST_CHECK(find_value(obj, "weight").type() == int_type);
    BOOST_CHECK(find_value(obj, "netstakeweight").type() == int_type);
    BOOST_CHECK(find_value(obj, "expectedtime").type() == int_type);

    // Real (double) fields
    BOOST_CHECK(find_value(obj, "difficulty").type() == real_type);
    BOOST_CHECK(find_value(obj, "difficulty (flash)").type() == real_type);
}

// ---------------------------------------------------------------------------
// getdifficulty — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getdifficulty_response_contract)
{
    Array p;
    Value result = getdifficulty(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "proof-of-work").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "proof-of-stake (flash)").type() == real_type);
    BOOST_CHECK(find_value(obj, "search-interval").type() == int_type);
}

// ---------------------------------------------------------------------------
// getsubsidy — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getsubsidy_response_contract)
{
    Array p;
    Value result = getsubsidy(p, false);

    // getsubsidy returns a bare uint64_t (block subsidy in satoshis)
    BOOST_CHECK(result.type() == int_type);
    // At test chain height (~51), PoW subsidy is 0 — only block 1 and
    // heights >= 17000 have non-zero PoW rewards. Verify non-negative.
    BOOST_CHECK(result.get_int64() >= 0);
}

// ---------------------------------------------------------------------------
// getwalletinfo — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getwalletinfo_response_contract)
{
    Array p;
    Value result = getwalletinfo(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "txcount").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);

    // "unlocked_until" only present when wallet is encrypted — not guaranteed
    // in test environment, so we do not assert its presence.
}

// ---------------------------------------------------------------------------
// getloglevel — response structure contract
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(getloglevel_response_contract)
{
    Array p;
    Value result = getloglevel(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "level").type() == str_type);
    BOOST_CHECK(find_value(obj, "categories").type() == array_type);
}

// ---------------------------------------------------------------------------
// help — returns non-empty string
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(help_returns_string)
{
    Array p;
    Value result = help(p, false);

    BOOST_CHECK(result.type() == str_type);
    BOOST_CHECK(!result.get_str().empty());
}

BOOST_AUTO_TEST_SUITE_END()
