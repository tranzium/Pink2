# Pre-Phase 6D Test Hardening Design ("Fortress")

**Date:** 2026-02-20
**Status:** Approved
**Goal:** Harden test coverage before Phase 6D dependency modernization to ensure backwards compatibility, RPC fidelity, wallet migration safety, and wire protocol correctness.

## Context

Phase 6D will perform large "all or nothing" refactors:
- json_spirit -> nlohmann/json (all RPC paths)
- boost::signals2 -> std::function-based Signal<>
- Result<T> error handling pattern
- Boost link dependency removal

This release will be the most radically different from any existing deployed wallet (including PinkPi nodes). Before touching dependencies, we need a comprehensive regression safety net.

## Current State

- 1,378 test cases across 64 files, 108 suites, zero failures
- All tiers P0-P9, C-K, Phase 6A-6C complete
- 100/100 RPC commands have at least one test
- Golden tests pin serialization formats across 9 categories

## Identified Gaps

1. **RPC response structure not pinned** -- rpc_coverage_tests verify commands don't throw but don't validate response field names, types, or consensus values
2. **No wallet.dat migration test** -- walletdb_tests test CRUD operations individually but nothing tests a full close/reopen cycle preserving all data
3. **Wire protocol gaps** -- golden_tests pin 9 categories but don't cover all network message types for mixed-version interop (version, addr, inv, getblocks, getheaders)
4. **JSON behavioral equivalence** -- no tests pin json_spirit edge-case behaviors (int64 max, precision, Unicode, null handling, duplicate keys)
5. **ConnectBlock/reorg untested** -- main.cpp ~70% untested (ConnectBlock internals, DisconnectBlock, chain reorganization)
6. **Signal notification paths** -- boost::signals2 -> Signal<> affects UI callbacks, no pre-migration behavioral tests

## Design

### Brittleness Management

Tests are designed as contract tests (field presence + type) rather than snapshot tests (exact string match):

| Test Type | Brittleness | Updates Needed |
|-----------|-------------|----------------|
| RPC response structure | Low | Never (unless field intentionally removed) |
| Wallet.dat round-trip | Zero | Never (format is consensus-level) |
| Wire protocol bytes | Zero | Never (unless protocol version bump) |
| JSON equivalence | Low | Once (during Phase 6D migration) |
| Integration workflows | Zero | Never |
| Signal semantics | Low | Once (during Phase 6D signal migration) |

~90% write-once/never-touch. ~10% update once during Phase 6D. Zero ongoing maintenance burden.

### File Structure

```
src/test/
  rpc_response_tests.cpp      [NEW]    ~100 tests
  migration_safety_tests.cpp   [NEW]    ~70 tests
  integration_tests.cpp        [EXPAND] +30 tests
```

Total: 2 new files + 1 expanded file, ~200 new tests (1,378 -> ~1,578).

---

## Deliverable 1: rpc_response_tests.cpp (~100 tests)

Pin response contracts (field names + types + consensus values) for all 100 RPC commands.

### Test Pattern

```cpp
BOOST_AUTO_TEST_CASE(getinfo_response_structure)
{
    json result = CallRPC("getinfo");
    BOOST_CHECK(result.is_object());

    // Required fields with expected types
    BOOST_CHECK(result["version"].is_number_integer());
    BOOST_CHECK(result["protocolversion"].is_number_integer());
    BOOST_CHECK_EQUAL(result["protocolversion"].get<int>(), 60019);  // consensus value pinned
    BOOST_CHECK(result["balance"].is_number_float());
    BOOST_CHECK(result["testnet"].is_boolean());
}
```

### Suites

| Suite | Tests | Coverage |
|-------|-------|----------|
| rpc_response_info | ~8 | getinfo, getmininginfo, getstakinginfo, getdifficulty, getnetworkinfo |
| rpc_response_wallet | ~25 | getbalance, listunspent, listtransactions, gettransaction, getaccount, etc. |
| rpc_response_blockchain | ~12 | getblock, getblockhash, getblockcount, getchaintips, getbestblockhash |
| rpc_response_network | ~8 | getpeerinfo, getconnectioncount, getnettotals, getaddednodeinfo |
| rpc_response_raw | ~10 | decoderawtransaction, decodescript, getrawtransaction, createrawtransaction |
| rpc_response_mining | ~8 | getblocktemplate, getwork, getmininginfo |
| rpc_response_smessage | ~12 | smsginbox, smsgoutbox, smsgbuckets, smsggetpubkey |
| rpc_response_errors | ~15 | Error structure for invalid/missing params, auth failures |

### Design Decisions

- Uses existing CallRPC() helper
- Checks field presence + type, not value (except consensus constants)
- Error tests verify JSON-RPC error envelope: {"code": int, "message": string}
- Commands requiring wallet state run after TestChain mines blocks
- Adding new fields to an RPC response will NOT break any test
- Only removing or renaming a field triggers failure

---

## Deliverable 2: migration_safety_tests.cpp (~70 tests)

Consolidated file with four subsections.

### 2A: Wallet.dat Migration Safety (~20 tests)

Programmatic fixture (not binary blob): test creates wallet with known content, writes to disk, reopens, verifies all data.

| Test | What it verifies |
|------|-----------------|
| wallet_keys_survive_reopen | 10 keys created, all present after reopen |
| wallet_accounts_survive_reopen | Named accounts preserved |
| wallet_transactions_survive_reopen | Tx history intact after reopen |
| wallet_encryption_survives_reopen | Can unlock with same passphrase after reopen |
| wallet_staking_data_survives_reopen | Stake DB entries intact |
| wallet_address_book_survives_reopen | Address book entries preserved |
| wallet_key_metadata_survives_reopen | Key creation timestamps preserved |
| wallet_default_key_survives_reopen | Default key unchanged |
| wallet_key_pool_survives_reopen | Key pool entries preserved |
| wallet_best_block_survives_reopen | Best block locator preserved |
| wallet_master_key_survives_reopen | Encryption master key preserved |
| wallet_cscript_survives_reopen | P2SH redeem scripts preserved |
| wallet_order_pos_survives_reopen | Transaction ordering preserved |
| wallet_version_survives_reopen | DB version preserved |
| wallet_multiple_close_reopen_cycles | 3 consecutive close/reopen cycles, all data intact |
| wallet_concurrent_data_types | All data types written in one session, all verified after reopen |
| wallet_empty_values_survive | Empty string account names, zero-value txs preserved |
| wallet_large_key_pool | 100-key pool preserved across reopen |
| wallet_stealth_addresses_survive | CStealthAddress entries preserved |
| wallet_smessage_keys_survive | Secure messaging keys preserved |

### 2B: Wire Protocol Byte Pinning (~15 tests)

Exact serialized bytes for network protocol messages.

| Test | What it pins |
|------|-------------|
| version_message_bytes | CVersion (protocol, services, timestamp, addrs) |
| addr_message_bytes | CAddress list (timestamp, services, IP, port) |
| inv_message_bytes | CInv vector (type + hash) |
| getblocks_message_bytes | CBlockLocator + hashStop |
| getheaders_message_bytes | Same structure, different command |
| block_header_bytes | 80-byte header (version, prevhash, merkle, time, bits, nonce) |
| tx_with_ntime_bytes | CTransaction including Pinkcoin's nTime field |
| alert_message_bytes | CAlert serialization with signature |
| ping_pong_bytes | Nonce serialization |
| caddr_disk_bytes | CAddress SER_DISK format (includes nVersion) |
| caddr_network_bytes | CAddress SER_NETWORK format |
| block_locator_bytes | CBlockLocator serialization |
| message_header_bytes | CMessageHeader (24 bytes: magic + command + size + checksum) |
| cinv_types_bytes | MSG_TX=1, MSG_BLOCK=2 serialized |
| getdata_message_bytes | Vector of CInv for block/tx requests |

Pattern: Build known object -> serialize -> compare bytes to hardcoded hex -> deserialize -> compare fields.

### 2C: JSON Behavioral Equivalence (~20 tests)

Pin json_spirit edge-case behaviors. Updated once during Phase 6D migration.

| Test | Behavior pinned |
|------|----------------|
| json_int64_max | INT64_MAX serializes without overflow or quotes |
| json_int64_min | INT64_MIN serializes correctly |
| json_negative_zero | -0.0 handling |
| json_precision_satoshi | 0.00000001 preserves 8 decimal places |
| json_unicode_passthrough | Unicode strings round-trip |
| json_null_in_object | null field values preserved |
| json_empty_string_key | Empty string as object key |
| json_duplicate_keys | json_spirit allows dupes; nlohmann doesn't |
| json_large_array | 1000-element array round-trip |
| json_nested_depth | 10-level deep nesting |
| json_number_string_distinction | "42" (string) vs 42 (int) type distinguishable |
| json_bool_not_int | true/false vs 1/0 type distinguishable |
| json_parse_trailing_content | Behavior on invalid trailing data |
| json_rpc_error_format | {"code":-1,"message":"text"} structure |
| json_object_field_ordering | Document current ordering behavior |
| json_empty_object_array | {} and [] serialize distinctly |
| json_scientific_notation | 1e10 handling |
| json_whitespace_in_parse | Leading/trailing whitespace tolerance |
| json_rpc_batch_format | Array of request objects |
| json_value_from_amount | ValueFromAmount(COIN) format (1.00000000) |

### 2D: Signal Behavioral Pinning (~15 tests)

Pin boost::signals2 semantics before Phase 6D replaces it.

| Test | Behavior pinned |
|------|----------------|
| signal_fire_single_slot | Connect -> fire -> callback invoked |
| signal_fire_multiple_slots | Multiple subscribers all receive |
| signal_disconnect | Disconnected slot stops receiving |
| signal_fire_order | Slots fire in connection order |
| signal_disconnect_during_fire | Re-entrant safety |
| signal_last_value_combiner | ThreadSafeAskFee pattern (last slot's return) |
| signal_connect_during_fire | New slot behavior during active fire |
| signal_thread_safety | Concurrent connect + fire |
| signal_disconnect_all | Bulk disconnect |
| signal_empty_fire | Fire with no slots (no crash) |
| ui_InitMessage | uiInterface.InitMessage fires to handler |
| ui_ThreadSafeMessageBox | Message box callback fires correctly |
| ui_NotifyBlocksChanged | Block change notification |
| ui_NotifyAlertChanged | Alert notification |
| signal_scoped_connection | Connection auto-disconnects on scope exit |

---

## Deliverable 3: integration_tests.cpp Expansion (+30 tests)

### 3A: ConnectBlock / Reorg Coverage (+15 tests)

| Test | Path covered |
|------|-------------|
| connectblock_utxo_creation | ConnectBlock creates UTXOs in txdb |
| connectblock_utxo_spending | Spent inputs marked, double-spend rejected |
| connectblock_coinbase_maturity | Immature coinbase not spendable |
| connectblock_fee_collection | Fees collected in coinbase |
| connectblock_money_supply | nMoneySupply tracked correctly |
| disconnectblock_utxo_restore | DisconnectBlock restores spent UTXOs |
| disconnectblock_removes_outputs | DisconnectBlock removes created outputs |
| reorg_shorter_chain_ignored | Less trust = no reorg |
| reorg_longer_chain_accepted | More trust = SetBestChain reorg |
| reorg_tx_returns_to_mempool | Disconnected txs return to mempool |
| reorg_double_spend_resolution | Conflicting txs across forks |
| orphan_block_stored | Unknown parent -> orphan storage |
| orphan_block_connected_after_parent | Parent arrival triggers orphan processing |
| acceptblock_version_check | Unsupported version rejected |
| processblock_duplicate_rejected | Known hash rejected immediately |

TestChain extension: `rewindTo()` + `mineBlocks()` for fork construction.

### 3B: End-to-End Wallet Workflows (+15 tests)

| Test | Workflow |
|------|----------|
| workflow_generate_address | GenerateNewKey -> GetAddress -> prefix "2" |
| workflow_receive_coins | Mine to wallet key -> GetBalance > 0 |
| workflow_send_coins | CreateTransaction -> CommitTransaction -> mine -> confirmations |
| workflow_send_change | Partial UTXO send -> change output to wallet |
| workflow_list_transactions | After send/receive, correct entries returned |
| workflow_get_transaction | Correct amount, confirmations, category |
| workflow_multiple_sends | 3 sends in sequence, all balances correct |
| workflow_insufficient_funds | CreateTransaction fails with error |
| workflow_immature_coinbase | Freshly mined coinbase not spendable |
| workflow_mature_coinbase | After maturity depth, coinbase spendable |
| workflow_unconfirmed_balance | Mempool tx reflected in unconfirmed balance |
| workflow_wallet_rescan | ScanForWalletTransactions finds historical txs |
| workflow_backup_restore | BackupWallet -> delete -> restore -> verify keys |
| workflow_dump_import_key | dumpprivkey -> importprivkey -> verify history |
| workflow_encrypt_send | Encrypt -> unlock -> send -> relock -> tx persists |

---

## Implementation Order

1. rpc_response_tests.cpp (largest, most mechanical, highest immediate value)
2. migration_safety_tests.cpp sections 2A-2B (wallet + wire protocol -- consensus-critical)
3. migration_safety_tests.cpp sections 2C-2D (JSON + signals -- migration-critical)
4. integration_tests.cpp expansion (ConnectBlock, reorg, workflows)
5. Build verification (both targets, full test suite, timestamp check)

## Success Criteria

- All ~1,578 tests pass with zero failures
- Both Linux and Windows MXE builds compile
- No test depends on field ordering in JSON responses
- Wallet data survives close/reopen with 100% fidelity
- Wire protocol bytes match hardcoded hex exactly
- Every RPC command has its response structure pinned
- Consensus constants (version, protocol, address prefixes) pinned to exact values
