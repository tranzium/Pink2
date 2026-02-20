# Pre-Phase 6D Test Hardening Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add ~200 regression tests to harden coverage before Phase 6D dependency modernization (json_spirit removal, Boost reduction, Signal<> replacement).

**Architecture:** Two new test files (`rpc_response_tests.cpp`, `migration_safety_tests.cpp`) plus expansion of `integration_tests.cpp`. Tests verify existing behavior using contract-based assertions (field presence + type + consensus values), not brittle snapshot matching.

**Tech Stack:** Boost.Test, json_spirit (current), TestChain fixture, CDataStream serialization, boost::signals2

---

## Prerequisites

- Working directory: `/mnt/projects-windows/Pink2`
- Branch: `feature/twenty_six`
- Current tests pass: 1,378 cases, zero failures
- Both Linux and Windows MXE builds compile

## Build & Test Commands

```bash
# Build (no -j flag — single-threaded)
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release

# Run tests
cd build/linux-release && ctest --output-on-failure

# Direct test run (more output)
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite

# Windows cross-compile (verify it still compiles)
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe

# Timestamp verification
echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
```

---

## Key Codebase Patterns

### RPC function invocation (existing pattern from rpc_coverage_tests.cpp)

```cpp
#include "bitcoinrpc.h"           // All extern Value func(const Array&, bool) declarations
using namespace json_spirit;
using namespace std;

// Call RPC directly:
Array params;
params.push_back("arg1");
Value result = getinfo(params, false);   // false = not help mode
Object obj = result.get_obj();

// Check field presence + type:
BOOST_CHECK(find_value(obj, "version").type() == str_type);
BOOST_CHECK(find_value(obj, "blocks").type() == int_type);

// Pin consensus values:
BOOST_CHECK_EQUAL(find_value(obj, "protocolversion").get_int(), 60019);

// Error handling — RPC errors throw Object (not runtime_error for JSON-RPC errors):
BOOST_CHECK_THROW(signmessage(badParams, false), Object);

// Help mode — throws runtime_error with help text:
BOOST_CHECK_THROW(getinfo(params, true), runtime_error);
```

### Golden serialization pattern (from golden_tests.cpp)

```cpp
template<typename T>
static std::string SerializeToHex(const T& obj, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    CDataStream ss(nType, nVersion);
    ss << obj;
    return HexStr(ss.begin(), ss.end());
}

template<typename T>
static T DeserializeFromHex(const std::string& hex, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    std::vector<unsigned char> data = ParseHex(hex);
    CDataStream ss(data, nType, nVersion);
    T obj;
    ss >> obj;
    return obj;
}
```

### TestChain fixture (from test_framework.h)

```cpp
BOOST_FIXTURE_TEST_SUITE(my_tests, TestChain)
// TestChain mines 50 PoW blocks. Available:
//   chainHeight(), chainTip(), blockIndexAt(h), mintAt(h), moneySupply()
//   MineEmptyBlocks(n), MineOneBlock()
//   CreateSpendTx(coinbaseIdx, scriptPubKey, amount, fee)
//   AddToMempool(tx), SubmitToMempool(tx), ClearMempool()
//   IsCoinbaseMature(idx), coinbaseTxns[i]
BOOST_AUTO_TEST_SUITE_END()
```

### Important extern declarations

```cpp
// From bitcoinrpc.h — all RPC functions declared as:
extern json_spirit::Value getinfo(const json_spirit::Array& params, bool fHelp);
// ... 102 total commands

// From bitcoinrpc.cpp (not in header):
extern Value help(const Array& params, bool fHelp);
extern Value stop(const Array& params, bool fHelp);

// Globals:
extern CWallet* pwalletMain;
extern int nBestHeight;
extern CBlockIndex* pindexBest;
extern CBlockIndex* pindexGenesisBlock;
extern int nCoinbaseMaturity;
extern const CRPCTable tableRPC;
```

---

### Task 1: Create rpc_response_tests.cpp — Info & Status Commands

**Files:**
- Create: `src/test/rpc_response_tests.cpp`
- Modify: `src/test/CMakeLists.txt` (add to PINKCOIN_TEST_SOURCES)

**Step 1: Create the test file with includes, helpers, and first suite**

Create `src/test/rpc_response_tests.cpp` with the file header, includes, and the `rpc_response_info` suite covering ~8 tests for: `getinfo`, `getmininginfo`, `getstakinginfo`, `getdifficulty`, `getsubsidy`, `getwalletinfo`, `getloglevel`, `help`.

Include pattern:
```cpp
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
#include "smessage.h"
#include "test_framework.h"

using namespace json_spirit;
using namespace std;

extern CWallet* pwalletMain;
extern Value help(const Array& params, bool fHelp);
```

Each test follows this contract pattern:
```cpp
BOOST_AUTO_TEST_CASE(getinfo_response_contract)
{
    Array p;
    Value result = getinfo(p, false);
    Object obj = result.get_obj();

    // Required fields: presence + type
    BOOST_CHECK(find_value(obj, "version").type() == str_type);
    BOOST_CHECK(find_value(obj, "protocolversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "walletversion").type() == int_type);
    BOOST_CHECK(find_value(obj, "balance").type() == real_type);
    BOOST_CHECK(find_value(obj, "newmint").type() == real_type);
    BOOST_CHECK(find_value(obj, "stake").type() == real_type);
    BOOST_CHECK(find_value(obj, "blocks").type() == int_type);
    BOOST_CHECK(find_value(obj, "moneysupply").type() == real_type);
    BOOST_CHECK(find_value(obj, "connections").type() == int_type);
    BOOST_CHECK(find_value(obj, "difficulty").type() == obj_type);
    BOOST_CHECK(find_value(obj, "testnet").type() == bool_type);
    BOOST_CHECK(find_value(obj, "keypoololdest").type() == int_type);
    BOOST_CHECK(find_value(obj, "keypoolsize").type() == int_type);
    BOOST_CHECK(find_value(obj, "paytxfee").type() == real_type);
    BOOST_CHECK(find_value(obj, "errors").type() == str_type);

    // Consensus value pins
    BOOST_CHECK_EQUAL(find_value(obj, "protocolversion").get_int(), 60019);
    BOOST_CHECK_EQUAL(find_value(obj, "testnet").get_bool(), false);
}
```

Tests in this suite:
- `getinfo_response_contract` — all 15+ fields, type + consensus value pins
- `getmininginfo_response_contract` — blocks, currentblocksize, difficulty, generate, hashespersec, networkhashps, pooledtx, testnet
- `getstakinginfo_response_contract` — enabled, staking, difficulty, weight, netstakeweight, expectedtime
- `getdifficulty_response_contract` — returns object with "proof-of-work" and "proof-of-stake" fields (both real_type)
- `getsubsidy_response_contract` — returns int_type (subsidy in satoshis)
- `getwalletinfo_response_contract` — walletversion, balance, unconfirmed_balance, immature_balance, txcount, keypoololdest, keypoolsize
- `getloglevel_response_contract` — level (str_type), categories (array_type)
- `help_returns_string` — help() returns str_type with non-empty content

**Step 2: Register in CMakeLists.txt**

Add `rpc_response_tests.cpp` to `PINKCOIN_TEST_SOURCES` in `src/test/CMakeLists.txt`, after `logging_tests.cpp`.

**Step 3: Build and run**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

Expected: All existing tests + new ~8 info tests pass.

**Step 4: Commit**

```bash
git add src/test/rpc_response_tests.cpp src/test/CMakeLists.txt
git commit -m "Pre-6D hardening: RPC response tests — info & status commands"
```

---

### Task 2: RPC Response Tests — Wallet Commands

**Files:**
- Modify: `src/test/rpc_response_tests.cpp` (add `rpc_response_wallet` suite)

**Step 1: Add wallet command response contract tests**

Add a new `BOOST_FIXTURE_TEST_SUITE(rpc_response_wallet, TestChain)` suite after the info suite. These tests need mined blocks for wallet state.

Tests (~25):
- `getnewaddress_response_contract` — returns str_type, starts with "2" (Pinkcoin PUBKEY prefix)
- `getnewpubkey_response_contract` — returns str_type (hex pubkey)
- `getaccountaddress_response_contract` — returns str_type for default account
- `getaccount_response_contract` — returns str_type (account name)
- `getaddressesbyaccount_response_contract` — returns array_type of str_type addresses
- `getbalance_response_contract` — returns real_type >= 0
- `getreceivedbyaddress_response_contract` — returns real_type
- `getreceivedbyaccount_response_contract` — returns real_type
- `listreceivedbyaddress_response_contract` — returns array_type; each element has "address" (str), "account" (str), "amount" (real), "confirmations" (int)
- `listreceivedbyaccount_response_contract` — returns array_type; each element has "account" (str), "amount" (real), "confirmations" (int)
- `listtransactions_response_contract` — returns array_type; each tx has "account", "category" (str), "amount" (real), "confirmations" (int), "txid" (str), "time" (int)
- `listaccounts_response_contract` — returns obj_type with account names as keys, real_type values
- `listunspent_response_contract` — returns array_type; each UTXO has "txid" (str), "vout" (int), "address" (str), "scriptPubKey" (str), "amount" (real), "confirmations" (int)
- `listaddressgroupings_response_contract` — returns array_type of array_type
- `gettransaction_response_contract` — needs a mined tx; verify "amount" (real), "confirmations" (int), "txid" (str), "time" (int), "details" (array)
- `listsinceblock_response_contract` — returns obj_type with "transactions" (array) and "lastblock" (str)
- `validateaddress_response_contract` — returns obj_type with "isvalid" (bool), "address" (str), "ismine" (bool)
- `validatepubkey_response_contract` — returns obj_type with "isvalid" (bool), "pubkey" (str)
- `signmessage_response_contract` — returns str_type (base64-encoded signature)
- `verifymessage_response_contract` — returns bool_type
- `getrawmempool_response_contract` — returns array_type of str_type (tx hashes)
- `settxfee_response_contract` — returns bool_type (true on success)
- `reservebalance_response_contract` — returns obj_type with "reserve" (bool), "amount" (real)
- `checkwallet_response_contract` — returns obj_type with "wallet check passed" (bool)
- `repairwallet_response_contract` — returns obj_type with "wallet check passed" (bool)

For tests that need setup (e.g., gettransaction requires a txid), use the TestChain fixture:
```cpp
BOOST_AUTO_TEST_CASE(gettransaction_response_contract)
{
    // Mine until coinbase is mature, then send
    MineEmptyBlocks(nCoinbaseMaturity);
    BOOST_REQUIRE(IsCoinbaseMature(0));

    CScript dest;
    CPubKey newKey;
    BOOST_REQUIRE(pwalletMain->GetKeyFromPool(newKey, false));
    dest.SetDestination(newKey.GetID());

    CTransaction tx = CreateSpendTx(0, dest, 1 * COIN);
    BOOST_REQUIRE(AddToMempool(tx));
    BOOST_REQUIRE(MineOneBlock());

    // Now query gettransaction
    Array p;
    p.push_back(tx.GetHash().GetHex());
    Value result = gettransaction(p, false);
    Object obj = result.get_obj();

    BOOST_CHECK(find_value(obj, "amount").type() == real_type);
    BOOST_CHECK(find_value(obj, "confirmations").type() == int_type);
    BOOST_CHECK(find_value(obj, "txid").type() == str_type);
    BOOST_CHECK(find_value(obj, "time").type() == int_type);
    BOOST_CHECK(find_value(obj, "details").type() == array_type);
}
```

**Step 2: Build and run**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
```

**Step 3: Commit**

```bash
git add src/test/rpc_response_tests.cpp
git commit -m "Pre-6D hardening: RPC response tests — wallet commands"
```

---

### Task 3: RPC Response Tests — Blockchain & Network Commands

**Files:**
- Modify: `src/test/rpc_response_tests.cpp` (add `rpc_response_blockchain` and `rpc_response_network` suites)

**Step 1: Add blockchain response contract tests (~12)**

Suite: `BOOST_FIXTURE_TEST_SUITE(rpc_response_blockchain, TestChain)`

Tests:
- `getblockcount_response_contract` — returns int_type, equals nBestHeight
- `getbestblockhash_response_contract` — returns str_type, 64 hex chars
- `getblockhash_response_contract` — param: height 0, returns str_type, 64 hex chars
- `getblock_response_contract` — param: genesis hash; verify "hash" (str), "confirmations" (int), "size" (int), "height" (int), "version" (int), "merkleroot" (str), "time" (int), "nonce" (int), "bits" (str), "difficulty" (real), "tx" (array)
- `getblockbynumber_response_contract` — param: 0; same fields as getblock
- `getcheckpoint_response_contract` — returns obj_type with "synccheckpoint" (str), "height" (int)
- `getblock_genesis_hash_pinned` — pin genesis block hash exactly
- `getblock_height_zero_version` — version == 1 for genesis
- `getblock_tx_array_nonempty` — genesis has at least 1 tx
- `getblock_confirmations_positive` — genesis has confirmations > 0
- `getblockcount_matches_chain_height` — result equals chainHeight()
- `getbestblockhash_matches_tip` — result equals pindexBest->GetBlockHash().GetHex()

**Step 2: Add network response contract tests (~8)**

Suite: `BOOST_AUTO_TEST_SUITE(rpc_response_network)` (no TestChain needed)

Tests:
- `getconnectioncount_response_contract` — returns int_type, >= 0
- `getpeerinfo_response_contract` — returns array_type (may be empty in test)
- `getnodes_response_contract` — returns obj_type or array_type
- `getconnectioncount_is_zero_in_test` — no real connections during testing
- `getpeerinfo_empty_in_test` — no peers during testing
- `getpeerinfo_help_works` — help mode returns help text
- `getconnectioncount_help_works` — help mode returns help text
- `getnodes_help_works` — help mode returns help text

**Step 3: Build and run, then commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/rpc_response_tests.cpp
git commit -m "Pre-6D hardening: RPC response tests — blockchain & network commands"
```

---

### Task 4: RPC Response Tests — Raw Tx, Mining, and Secure Messaging

**Files:**
- Modify: `src/test/rpc_response_tests.cpp` (add `rpc_response_raw`, `rpc_response_mining`, `rpc_response_smessage` suites)

**Step 1: Add raw transaction response contract tests (~10)**

Suite: `BOOST_FIXTURE_TEST_SUITE(rpc_response_raw, TestChain)`

Tests:
- `createrawtransaction_response_contract` — returns str_type (hex-encoded tx)
- `decoderawtransaction_response_contract` — returns obj_type with "txid" (str), "version" (int), "time" (int), "locktime" (int), "vin" (array), "vout" (array)
- `decodescript_response_contract` — returns obj_type with "asm" (str), "type" (str), "reqSigs" (int), "addresses" (array)
- `decoderawtransaction_vin_fields` — each vin has "txid" (str), "vout" (int), "scriptSig" (obj with "asm" + "hex")
- `decoderawtransaction_vout_fields` — each vout has "value" (real), "n" (int), "scriptPubKey" (obj)
- `decoderawtransaction_has_ntime` — Pinkcoin-specific: "time" field present (int_type)
- `decodescript_p2pkh_type` — type == "pubkeyhash" for standard P2PKH
- `decodescript_p2sh_prefix` — P2SH address starts with "C" (Pinkcoin prefix 28)
- `makekeypair_response_contract` — returns obj_type with "PrivateKey" (str) and "PublicKey" (str)
- `makekeypair_unique_each_call` — two calls produce different keys

**Step 2: Add mining response contract tests (~8)**

Suite: `BOOST_FIXTURE_TEST_SUITE(rpc_response_mining, TestChain)`

Tests:
- `getmininginfo_response_contract` — (already in Task 1 but test here for the mining-specific fields: "hashespersec", "pooledtx", "difficulty", "generate", "genproclimit")
- `getsubsidy_no_params_contract` — returns int_type (current height subsidy)
- `getsubsidy_with_height_param` — pass nBestHeight; returns int_type
- `getstakinginfo_response_contract_extended` — "weight" (int), "netstakeweight" (int), "expectedtime" (int)
- `getstakesplitthreshold_response_contract` — returns obj_type with "threshold" field
- `combinethreshold_response_contract` — returns obj_type
- `splitthreshold_response_contract` — returns obj_type
- `getwork_help_works` — fHelp=true returns help text (don't call getwork directly as it requires mining)

**Step 3: Add secure messaging response contract tests (~12)**

Suite: `BOOST_AUTO_TEST_SUITE(rpc_response_smessage)`

Note: smsg commands require `fSecMsgenabled`. Save/restore this global.

```cpp
struct SmsgGuard {
    bool saved;
    SmsgGuard() : saved(fSecMsgEnabled) { fSecMsgEnabled = true; }
    ~SmsgGuard() { fSecMsgEnabled = saved; }
};
```

Tests:
- `smsgoptions_response_contract` — returns obj_type with smsg configuration fields
- `smsgbuckets_response_contract` — returns obj_type or array_type
- `smsginbox_response_contract` — returns obj_type with "result" (str)
- `smsgoutbox_response_contract` — returns obj_type with "result" (str)
- `smsggetpubkey_response_contract` — param: valid address; returns obj_type with "address" (str), "publickey" (str) or error
- `smsglocalkeys_response_contract` — returns obj_type with wallet keys info
- `smsgenable_response_contract` — returns obj_type with "result" (str)
- `smsgdisable_response_contract` — returns obj_type with "result" (str)
- `smsgoptions_help_works` — fHelp=true throws runtime_error
- `smsgaddkey_missing_params_throws` — missing param throws
- `smsgsend_missing_params_throws` — missing params throws
- `smsgsendanon_missing_params_throws` — missing params throws

**Step 4: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/rpc_response_tests.cpp
git commit -m "Pre-6D hardening: RPC response tests — raw tx, mining, smessage commands"
```

---

### Task 5: RPC Response Tests — Error Contracts & Remaining Commands

**Files:**
- Modify: `src/test/rpc_response_tests.cpp` (add `rpc_response_errors` and `rpc_response_remaining` suites)

**Step 1: Add error response contract tests (~15)**

Suite: `BOOST_AUTO_TEST_SUITE(rpc_response_errors)`

These verify the JSON-RPC error envelope structure when commands are called with invalid inputs.

Pattern:
```cpp
BOOST_AUTO_TEST_CASE(error_envelope_structure)
{
    Array p;
    p.push_back("invalid_address_xxx");
    try {
        validateaddress(p, false);  // might not throw for invalid, just returns isvalid=false
    } catch (const Object& err) {
        // Verify error envelope: {"code": int, "message": str}
        BOOST_CHECK(find_value(err, "code").type() == int_type);
        BOOST_CHECK(find_value(err, "message").type() == str_type);
    }
}
```

Tests:
- `error_invalid_address_code` — RPC_INVALID_ADDRESS_OR_KEY (-5) for bad address
- `error_invalid_params_code` — RPC_INVALID_PARAMS (-32602) for wrong param count
- `error_wallet_unlock_needed_code` — RPC_WALLET_UNLOCK_NEEDED (-13) for locked wallet ops
- `error_method_not_found_code` — tableRPC["nonexistent"] returns nullptr
- `error_envelope_has_code_and_message` — all thrown Object errors have "code" + "message"
- `error_sendtoaddress_no_funds` — RPC_WALLET_INSUFFICIENT_FUNDS (-6)
- `error_signmessage_wrong_params` — missing params throws
- `error_importprivkey_invalid_key` — invalid WIF throws
- `error_decoderawtransaction_invalid_hex` — non-hex throws
- `error_getblock_unknown_hash` — unknown hash throws
- `error_getblockhash_out_of_range` — height > nBestHeight throws
- `error_gettransaction_unknown_txid` — unknown txid throws
- `error_walletpassphrase_wrong_pass` — wrong passphrase on unencrypted wallet
- `error_encryptwallet_already_encrypted` — second encrypt throws
- `error_help_mode_throws_for_all` — verify fHelp=true throws runtime_error for at least 10 representative commands

**Step 2: Add remaining command contract tests**

Suite: `BOOST_FIXTURE_TEST_SUITE(rpc_response_remaining, TestChain)`

Cover stakeout, stealth, and misc commands not yet tested:
- `addstakeout_response_contract` — returns obj_type or str_type
- `liststakeout_response_contract` — returns array_type
- `getstakeoutinfo_response_contract` — returns obj_type
- `getnewstealthaddress_response_contract` — returns str_type (stealth address)
- `liststealthaddresses_response_contract` — returns array_type
- `backupwallet_response_contract` — param: temp path; succeeds without throw
- `keypoolrefill_response_contract` — returns null_type on success
- `resendtx_response_contract` — returns null_type on success
- `setloglevel_response_contract` — returns obj_type with "level" (str)
- `clearwallettransactions_response_contract` — returns obj_type
- `encryptwallet_response_contract` — test in isolated context (irreversible)

**Step 3: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/rpc_response_tests.cpp
git commit -m "Pre-6D hardening: RPC response tests — error contracts & remaining commands"
```

**Step 4: Checkpoint — verify test count**

```bash
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite 2>&1 | tail -5
```

Expected: ~1,478 tests (1,378 + ~100 new), zero failures.

---

### Task 6: Create migration_safety_tests.cpp — Wallet Migration

**Files:**
- Create: `src/test/migration_safety_tests.cpp`
- Modify: `src/test/CMakeLists.txt` (add to PINKCOIN_TEST_SOURCES)

**Step 1: Create the file with wallet migration safety tests**

These test the wallet close/reopen cycle — the same code path users exercise when upgrading.

Includes:
```cpp
#include <boost/test/unit_test.hpp>

#include "main.h"
#include "wallet.h"
#include "walletdb.h"
#include "key.h"
#include "base58.h"
#include "crypter.h"
#include "script.h"
#include "stealth.h"
#include "protocol.h"
#include "version.h"
#include "bignum.h"
#include "util.h"
#include "stakedb.h"
#include "smessage.h"
#include "db.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"
#include "json/json_spirit_utils.h"

#include "test_framework.h"

#include <boost/signals2/signal.hpp>

using namespace json_spirit;
using namespace std;

extern CWallet* pwalletMain;
```

**Wallet migration test pattern:**

The key pattern is: use a separate test wallet (not pwalletMain) to avoid contaminating other tests. Create with a unique filename, write data, flush, close, reopen, verify.

```cpp
BOOST_AUTO_TEST_CASE(wallet_keys_survive_reopen)
{
    const string walletFile = "test_migration_keys.dat";

    // Phase 1: Create wallet, add keys
    vector<CPubKey> savedKeys;
    {
        CWallet testWallet(walletFile);
        bool fFirstRun = true;
        BOOST_REQUIRE(testWallet.LoadWallet(fFirstRun) == DB_LOAD_OK);

        for (int i = 0; i < 10; ++i) {
            CPubKey key;
            testWallet.GenerateNewKey();
            // ... collect keys
        }
        testWallet.Flush(true);  // force flush
    }

    // Phase 2: Reopen and verify
    {
        CWallet testWallet(walletFile);
        bool fFirstRun = true;
        BOOST_REQUIRE(testWallet.LoadWallet(fFirstRun) == DB_LOAD_OK);

        for (const auto& key : savedKeys) {
            BOOST_CHECK(testWallet.HaveKey(key.GetID()));
        }
    }
}
```

Tests (~20):
- `wallet_keys_survive_reopen` — 10 generated keys all present after reopen
- `wallet_accounts_survive_reopen` — named accounts ("savings", "staking") preserved
- `wallet_address_book_survive_reopen` — address book entries (name → address) preserved
- `wallet_default_key_survives_reopen` — SetDefaultKey → reopen → GetDefaultKey matches
- `wallet_key_pool_survives_reopen` — TopUpKeyPool → reopen → key pool size preserved
- `wallet_version_survives_reopen` — WriteMinVersion → reopen → ReadMinVersion matches
- `wallet_best_block_survives_reopen` — WriteBestBlock → reopen → ReadBestBlock matches
- `wallet_cscript_survives_reopen` — WriteCScript → reopen → HaveCScript returns true
- `wallet_order_pos_survives_reopen` — WriteOrderPosNext → reopen → value preserved
- `wallet_master_key_survives_reopen` — WriteMasterKey → reopen → read back matches
- `wallet_key_metadata_survives_reopen` — key creation time preserved across reopen
- `wallet_tx_survives_reopen` — WriteTx → reopen → tx hash present in wallet
- `wallet_name_survives_reopen` — WriteName → reopen → address→name mapping preserved
- `wallet_multiple_reopen_cycles` — 3 consecutive close/reopen cycles, all data intact
- `wallet_concurrent_data_types` — all data types written in one session, verified after reopen
- `wallet_empty_account_name_survives` — empty string account name preserved
- `wallet_large_key_pool_survives` — 100-key pool preserved
- `stakedb_entries_survive_reopen` — WriteStake → reopen → ReadStake matches
- `stakedb_version_survives_reopen` — WriteMinVersion → reopen → version preserved
- `stakedb_multiple_entries_survive` — 10 stake entries, all preserved

**Step 2: Register in CMakeLists.txt**

Add `migration_safety_tests.cpp` after `rpc_response_tests.cpp`.

**Step 3: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/migration_safety_tests.cpp src/test/CMakeLists.txt
git commit -m "Pre-6D hardening: wallet migration safety tests"
```

---

### Task 7: Migration Safety — Wire Protocol Byte Pinning

**Files:**
- Modify: `src/test/migration_safety_tests.cpp` (add `wire_protocol_pinning` suite)

**Step 1: Add wire protocol serialization tests**

Add serialization helpers (same pattern as golden_tests.cpp) and a new suite:

```cpp
// Reuse the golden_tests pattern
template<typename T>
static std::string SerializeToHex(const T& obj, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    CDataStream ss(nType, nVersion);
    ss << obj;
    return HexStr(ss.begin(), ss.end());
}

template<typename T>
static T DeserializeFromHex(const std::string& hex, int nType = SER_DISK, int nVersion = CLIENT_VERSION)
{
    std::vector<unsigned char> data = ParseHex(hex);
    CDataStream ss(data, nType, nVersion);
    T obj;
    ss >> obj;
    return obj;
}
```

For each test: construct a deterministic object with known field values, serialize to hex, compare against hardcoded expected hex, then deserialize back and verify fields match.

**IMPORTANT:** The golden hex values must be captured from a RUNNING build first. The implementation step is:
1. Write the test with `BOOST_CHECK_EQUAL(hex, "TODO");`
2. Build and run — test fails and prints actual hex
3. Verify the hex is correct (manual inspection of field layout)
4. Replace "TODO" with the actual hex
5. Rebuild and verify pass

Tests (~15):
- `wire_cinv_tx_bytes` — CInv(MSG_TX, known hash) SER_NETWORK serialization
- `wire_cinv_block_bytes` — CInv(MSG_BLOCK, known hash) SER_NETWORK serialization
- `wire_caddr_network_bytes` — CAddress with known IP/port, SER_NETWORK format
- `wire_caddr_disk_bytes` — CAddress with known IP/port, SER_DISK format (includes nVersion prefix)
- `wire_block_header_80_bytes` — CBlock header (no vtx), must be exactly 80 bytes
- `wire_tx_with_ntime_bytes` — CTransaction with known fields, verify nTime field is serialized (Pinkcoin-specific)
- `wire_block_locator_bytes` — CBlockLocator with known hashes
- `wire_message_header_24_bytes` — CMessageHeader is exactly 24 bytes (4 magic + 12 command + 4 size + 4 checksum)
- `wire_cinv_type_constants` — MSG_TX == 1, MSG_BLOCK == 2 in serialized form
- `wire_ping_nonce_bytes` — uint64_t nonce serialization (8 bytes, little-endian)
- `wire_caddr_services_field` — NODE_NETWORK flag in services field
- `wire_genesis_header_bytes` — genesis block header serialization pinned
- `wire_tx_empty_vin_bytes` — empty transaction serialization
- `wire_outpoint_bytes` — COutPoint(hash, n) serialization
- `wire_script_bytes` — CScript with OP_DUP OP_HASH160 ... serialization

**Step 2: Build, capture golden hex values, finalize tests**

First build with "TODO" placeholders, run to capture actual hex, then update.

**Step 3: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/migration_safety_tests.cpp
git commit -m "Pre-6D hardening: wire protocol byte pinning tests"
```

---

### Task 8: Migration Safety — JSON Behavioral Equivalence

**Files:**
- Modify: `src/test/migration_safety_tests.cpp` (add `json_behavioral_equivalence` suite)

**Step 1: Add JSON behavioral tests**

These pin json_spirit's exact edge-case behaviors. During Phase 6D migration, they'll be updated to pin nlohmann's behaviors instead.

Suite: `BOOST_AUTO_TEST_SUITE(json_behavioral_equivalence)`

```cpp
BOOST_AUTO_TEST_CASE(json_int64_max_serializes)
{
    Value v(static_cast<int64_t>(INT64_MAX));
    string s = write_string(v, false);
    BOOST_CHECK(!s.empty());
    // Pin: json_spirit writes INT64_MAX as decimal without quotes
    Value reparsed;
    BOOST_CHECK(read_string(s, reparsed));
    BOOST_CHECK_EQUAL(reparsed.get_int64(), INT64_MAX);
}

BOOST_AUTO_TEST_CASE(json_precision_satoshi)
{
    // 1 satoshi = 0.00000001 PINK
    Value v = ValueFromAmount(1);  // 1 satoshi
    string s = write_string(v, false);
    // Must preserve 8 decimal places
    BOOST_CHECK(s.find("0.00000001") != string::npos);
}
```

Tests (~20):
- `json_int64_max_serializes` — INT64_MAX round-trips through write/read
- `json_int64_min_serializes` — INT64_MIN round-trips
- `json_precision_satoshi` — 0.00000001 preserves 8 decimal places
- `json_precision_one_coin` — ValueFromAmount(COIN) == "1.00000000"
- `json_precision_max_money` — ValueFromAmount(MAX_MONEY) doesn't overflow
- `json_null_value_roundtrip` — Value() (null) writes and reads back as null_type
- `json_null_in_object` — Object with Pair("key", Value()) preserves null
- `json_empty_string_roundtrip` — "" round-trips correctly
- `json_unicode_basic` — Unicode characters in strings round-trip
- `json_bool_true_distinct_from_int` — true serializes as "true", not "1"
- `json_bool_false_distinct_from_int` — false serializes as "false", not "0"
- `json_number_string_distinction` — Value(42) vs Value("42") produce different output
- `json_empty_object_serializes` — Object() → "{}"
- `json_empty_array_serializes` — Array() → "[]"
- `json_nested_objects` — 3-level nesting round-trips
- `json_array_of_mixed_types` — [int, string, bool, null] round-trips
- `json_object_field_ordering` — json_spirit Object preserves insertion order (document this)
- `json_duplicate_keys_behavior` — json_spirit allows duplicate keys; document which one wins
- `json_rpc_error_object_format` — JSONRPCError(code, msg) produces {"code":int,"message":str}
- `json_value_from_amount_format` — ValueFromAmount for representative values: 0, 1, COIN, MAX_MONEY

**Step 2: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/migration_safety_tests.cpp
git commit -m "Pre-6D hardening: JSON behavioral equivalence tests"
```

---

### Task 9: Migration Safety — Signal Behavioral Pinning

**Files:**
- Modify: `src/test/migration_safety_tests.cpp` (add `signal_behavioral_pinning` suite)

**Step 1: Add signal tests**

These pin boost::signals2 semantics that Phase 6D will replicate in Signal<>.

```cpp
#include <boost/signals2/signal.hpp>
#include "ui_interface.h"

BOOST_AUTO_TEST_SUITE(signal_behavioral_pinning)

BOOST_AUTO_TEST_CASE(signal_fire_single_slot)
{
    boost::signals2::signal<void(int)> sig;
    int received = -1;
    sig.connect([&](int v) { received = v; });
    sig(42);
    BOOST_CHECK_EQUAL(received, 42);
}
```

Tests (~15):
- `signal_fire_single_slot` — connect one slot, fire, verify callback received value
- `signal_fire_multiple_slots` — 3 slots, all receive the signal
- `signal_disconnect` — disconnect slot, fire, disconnected slot doesn't receive
- `signal_fire_order` — slots fire in connection order
- `signal_empty_fire_no_crash` — fire signal with zero connected slots
- `signal_disconnect_all_slots` — disconnect_all_slots(), fire, nothing received
- `signal_reconnect_after_disconnect` — disconnect then reconnect, slot receives again
- `signal_last_value_combiner` — `signal<bool(...), last_value<bool>>`: last slot's return value returned
- `signal_last_value_multiple` — 3 slots returning different bools, last one wins
- `signal_scoped_connection` — boost::signals2::scoped_connection auto-disconnects on scope exit
- `signal_void_return` — void-returning signal fires without crash
- `ui_InitMessage_fires` — connect to uiInterface.InitMessage, fire, verify received
- `ui_ThreadSafeMessageBox_fires` — connect to uiInterface.ThreadSafeMessageBox, verify args
- `ui_NotifyNumConnectionsChanged_fires` — connect, fire with int, verify received
- `ui_NotifyAlertChanged_fires` — connect, fire with (hash, CT_NEW), verify received

**Note on uiInterface tests:** Save and restore the signal state. The `noui_connect()` call in TestingSetup connects default handlers. These tests add additional handlers alongside, then disconnect them.

```cpp
BOOST_AUTO_TEST_CASE(ui_InitMessage_fires)
{
    string received;
    auto conn = uiInterface.InitMessage.connect([&](const string& msg) {
        received = msg;
    });
    uiInterface.InitMessage("test init message");
    BOOST_CHECK_EQUAL(received, "test init message");
    conn.disconnect();
}
```

**Step 2: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/migration_safety_tests.cpp
git commit -m "Pre-6D hardening: signal behavioral pinning tests"
```

**Step 3: Checkpoint — verify test count**

Expected: ~1,548 tests (1,378 + ~100 RPC + ~70 migration safety), zero failures.

---

### Task 10: Integration Tests — ConnectBlock & Reorg

**Files:**
- Modify: `src/test/test_framework.h` (add `DisconnectTip()` and `MineFork()` helpers if needed)
- Modify: `src/test/test_framework.cpp` (implement new helpers)
- Modify: `src/test/integration_tests.cpp` (add `integration_connectblock` and `integration_reorg` suites)

**Step 1: Extend TestChain with reorg helpers (if feasible)**

Check whether the existing TestChain can support chain reorganization by mining a competing fork. The key challenge is that ProcessBlock needs a block whose prevhash points to an earlier block, not the current tip.

If reorg is too complex to implement reliably in the test framework, focus on the ConnectBlock tests and skip the reorg tests (marking them as future work). Do not force fragile reorg infrastructure.

**ConnectBlock tests to add (~10-15):**

Suite: `BOOST_FIXTURE_TEST_SUITE(integration_connectblock_extended, TestChain)`

Tests:
- `connectblock_utxo_in_txdb` — after mining with a spend tx, ReadTxIndex finds the tx
- `connectblock_spent_utxo_marked` — spent UTXO's CTxIndex shows spent position
- `connectblock_double_spend_rejected` — second spend of same UTXO rejected by ConnectInputs
- `connectblock_fee_in_coinbase` — fee from non-coinbase tx ends up in block's coinbase value
- `connectblock_money_supply_increments` — nMoneySupply increases by reward for each block
- `connectblock_chain_trust_increases` — nChainTrust strictly increases
- `connectblock_block_file_written` — blk*.dat file exists and contains block data
- `connectblock_multiple_txs_in_block` — mine block with 2+ txs in mempool, all connected

**Reorg tests (attempt, may be complex):**
- `orphan_block_detection` — ProcessBlock stores block with unknown parent as orphan
- `processblock_duplicate_hash_rejected` — already-known block rejected
- `processblock_invalid_pow_rejected` — block with bad nonce rejected

**Step 2: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/integration_tests.cpp src/test/test_framework.h src/test/test_framework.cpp
git commit -m "Pre-6D hardening: ConnectBlock & chain processing integration tests"
```

---

### Task 11: Integration Tests — End-to-End Wallet Workflows

**Files:**
- Modify: `src/test/integration_tests.cpp` (add `integration_wallet_workflows` suite)

**Step 1: Add wallet workflow tests**

Suite: `BOOST_FIXTURE_TEST_SUITE(integration_wallet_workflows, TestChain)`

These exercise the full lifecycle: generate key → receive coins → send → confirm → query.

```cpp
BOOST_AUTO_TEST_CASE(workflow_generate_and_receive)
{
    // Generate a key owned by the wallet
    CPubKey newKey;
    BOOST_REQUIRE(pwalletMain->GetKeyFromPool(newKey, false));
    CScript scriptPubKey;
    scriptPubKey.SetDestination(newKey.GetID());

    // Mine blocks until coinbase is mature
    MineEmptyBlocks(nCoinbaseMaturity);
    BOOST_REQUIRE(IsCoinbaseMature(0));

    // Send from mature coinbase to our new key
    CTransaction tx = CreateSpendTx(0, scriptPubKey, 10 * COIN);
    BOOST_REQUIRE(AddToMempool(tx));
    BOOST_REQUIRE(MineOneBlock());

    // Verify wallet sees the received funds
    // (The wallet should recognize the output as ours via scriptPubKey matching)
    BOOST_CHECK(pwalletMain->IsMine(tx.vout[0]));
}
```

Tests (~15):
- `workflow_generate_and_receive` — generate key, send to it, verify wallet sees it
- `workflow_address_prefix` — CBitcoinAddress from wallet key starts with "2"
- `workflow_receive_updates_balance` — GetBalance increases after receiving
- `workflow_send_coins_end_to_end` — full CreateTransaction → CommitTransaction → mine → confirm
- `workflow_send_creates_change` — partial UTXO spend creates change output to wallet
- `workflow_list_transactions_after_send` — listtransactions returns the sent tx
- `workflow_get_transaction_details` — gettransaction returns correct amount and confirmations
- `workflow_multiple_sends` — 3 sequential sends, all balances correct
- `workflow_insufficient_funds_error` — CreateTransaction fails when balance insufficient
- `workflow_immature_coinbase_not_spendable` — fresh coinbase output rejected by AcceptToMemoryPool
- `workflow_mature_coinbase_spendable` — coinbase after nCoinbaseMaturity depth is spendable
- `workflow_unconfirmed_in_mempool` — tx in mempool but not mined: GetDepthInMainChain == 0
- `workflow_backup_and_verify` — BackupWallet → verify backup file exists and has content
- `workflow_dump_import_key_roundtrip` — dumpprivkey → importprivkey on a second wallet → key present
- `workflow_sign_verify_message` — signmessage → verifymessage round-trip with wallet key

**Step 2: Build, run, commit**

```bash
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
cd build/linux-release && ctest --output-on-failure
git add src/test/integration_tests.cpp
git commit -m "Pre-6D hardening: end-to-end wallet workflow integration tests"
```

---

### Task 12: Final Build Verification

**Files:** None (verification only)

**Step 1: Clean rebuild both targets**

```bash
cd /mnt/projects-windows/Pink2
rm -rf build/linux-release && cmake --preset linux-release && cmake --build build/linux-release
rm -rf build/windows-mxe && cmake --preset windows-mxe && cmake --build build/windows-mxe
```

**Step 2: Timestamp verification**

```bash
echo "=== LINUX ===" && stat -c '%y' build/linux-release/src/test/test_pinkcoin
echo "=== WINDOWS ===" && stat -c '%y' build/windows-mxe/src/pink2d.exe && stat -c '%y' build/windows-mxe/src/qt/Pinkcoin-Qt.exe
echo "=== NOW ===" && date '+%Y-%m-%d %H:%M:%S %z'
```

All timestamps must be within the current session.

**Step 3: Full test suite**

```bash
cd build/linux-release && ctest --output-on-failure
```

**Step 4: Test count verification**

```bash
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite 2>&1 | tail -5
```

Expected: ~1,578 tests (1,378 + ~200 new), zero failures.

**Step 5: Verify no regressions in existing tests**

```bash
./build/linux-release/src/test/test_pinkcoin --log_level=test_suite 2>&1 | grep -c "FAIL"
```

Expected: 0

**Step 6: Final commit (if any cleanup needed)**

```bash
git status
# If clean, no commit needed
# If files modified, commit with descriptive message
```

**Step 7: Update memory**

Update `/home/lisa/.claude/projects/-mnt-projects-windows-Pink2/memory/MEMORY.md` with:
- New test count
- New test file names and their tier assignment
- Pre-Phase 6D readiness status

---

## Summary

| Task | File | Tests | Focus |
|------|------|-------|-------|
| 1 | rpc_response_tests.cpp | ~8 | Info & status commands |
| 2 | rpc_response_tests.cpp | ~25 | Wallet commands |
| 3 | rpc_response_tests.cpp | ~20 | Blockchain & network |
| 4 | rpc_response_tests.cpp | ~30 | Raw tx, mining, smessage |
| 5 | rpc_response_tests.cpp | ~17 | Errors & remaining |
| 6 | migration_safety_tests.cpp | ~20 | Wallet migration |
| 7 | migration_safety_tests.cpp | ~15 | Wire protocol bytes |
| 8 | migration_safety_tests.cpp | ~20 | JSON equivalence |
| 9 | migration_safety_tests.cpp | ~15 | Signal behavioral |
| 10 | integration_tests.cpp | ~12 | ConnectBlock & chain |
| 11 | integration_tests.cpp | ~15 | Wallet workflows |
| 12 | — | — | Final build verification |
| **Total** | **2 new + 1 expanded** | **~200** | **1,378 → ~1,578** |

## Build Checkpoints

| After Task | What to verify |
|-----------|---------------|
| 5 | Linux build + tests pass (~1,478 tests) |
| 9 | Linux build + tests pass (~1,548 tests) |
| 11 | Linux build + tests pass (~1,578 tests) |
| 12 | Linux + Windows builds pass, all tests pass, timestamps fresh |
