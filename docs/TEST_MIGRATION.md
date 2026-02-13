# Test Data Migration: Bitcoin to Pinkcoin

## Overview

The original unit tests contained Bitcoin-specific test data inherited from the Bitcoin Core ~0.8.x fork.
All critical migration tasks (Tier A and B) have been completed. Tests now use Pinkcoin-native
values or dynamically generated keys, eliminating dependence on hardcoded Bitcoin data.

## Pinkcoin Version Bytes (from `src/base58.h`)

| Type | Pinkcoin | Bitcoin | Address Prefix |
|------|----------|---------|----------------|
| PUBKEY_ADDRESS | 3 | 0 | "2" vs "1" |
| SCRIPT_ADDRESS | 28 | 5 | "C" vs "3" |
| PUBKEY_ADDRESS_TEST | 55 | 111 | |
| SCRIPT_ADDRESS_TEST | 196 | 196 | |
| SECRET_KEY (WIF) | 131 | 128 | |

## Migration Status

### Tier A — EASY (hardcoded values) — ALL DONE

- [x] **`src/test/Checkpoints_tests.cpp`**
  - All 15 hardened Pinkcoin checkpoints pinned (heights 0 through 728000)
  - Tests: `sanity`, `checkpoint_genesis`, `checkpoint_all_hardened`, `checkpoint_wrong_hash`,
    `checkpoint_non_checkpoint_height`, `total_blocks_estimate`
  - No Bitcoin data remains

- [x] **`src/test/key_tests.cpp`**
  - Replaced hardcoded Bitcoin WIF keys/addresses with deterministic key generation
    via `Hash("test secret N")` → `CKey::SetSecret()`
  - Added: EC_KEY_regenerate_key roundtrip, compact signature recovery (compressed/uncompressed),
    secp256k1 constant pinning, CheckSignatureElement bounds, key utilities
  - Tests use Pinkcoin address encoding natively (version byte 3 / prefix "2")
  - No Bitcoin data remains

### Tier B — MEDIUM (JSON test data) — ALL DONE

- [x] **`src/test/data/base58_keys_valid.json`** — **ORPHANED**
  - `base58_tests.cpp` no longer loads this file
  - `base58_keys_valid_parse` and `base58_keys_valid_gen` tests were rewritten to use
    dynamically generated Pinkcoin keys (deterministic seeds, CBitcoinSecret/CBitcoinAddress roundtrips)
  - The JSON file can be deleted

- [x] **`src/test/data/base58_keys_invalid.json`** — No changes needed
  - Still used by `base58_tests.cpp:base58_keys_invalid` test
  - Contains intentionally corrupted base58 strings — coin-agnostic negative test cases

- [x] **`src/test/data/base58_encode_decode.json`** — No changes needed
  - Used by `base58_tests.cpp:base58_EncodeBase58` and `base58_DecodeBase58`
  - Generic hex↔base58 encoding — not coin-specific

### Tier C — COMPLEX (script/transaction JSON) — RESOLVED

- [x] **`src/test/data/script_valid.json`** — No changes needed
  - Used by `script_tests.cpp:script_valid` — tests opcode/script parsing logic
  - Data is coin-agnostic (raw scriptSig/scriptPubKey pairs with `VerifyScript()`)
  - No addresses or transaction serialization involved

- [x] **`src/test/data/script_invalid.json`** — No changes needed
  - Used by `script_tests.cpp:script_invalid` — same coin-agnostic script parsing

- [x] **`src/test/data/tx_valid.json`** — **ORPHANED**
  - `transaction_tests.cpp` no longer loads this file
  - Tests were rewritten to use programmatic Pinkcoin transactions (with `nTime` field)
  - Bitcoin hex transaction data is incompatible (missing Pinkcoin's `nTime` field)
  - The JSON file can be deleted

- [x] **`src/test/data/tx_invalid.json`** — **ORPHANED**
  - Same as `tx_valid.json` — no longer referenced
  - The JSON file can be deleted

### Pinkcoin-native test data (added)

- [x] **`src/test/data/mainnet_blocks.json`**
  - Real Pinkcoin mainnet block data: headers, hashes, merkle roots
  - Used by `mainnet_block_tests.cpp` (9 tests): scrypt hash verification,
    PoW/PoS/FPoS block identification, checkpoint validation, entropy bits

## Orphaned JSON Files

The following files in `src/test/data/` are no longer referenced by any test and can be removed:

| File | Reason |
|------|--------|
| `base58_keys_valid.json` | Replaced by dynamic key generation in `base58_tests.cpp` |
| `tx_valid.json` | Replaced by programmatic Pinkcoin transactions in `transaction_tests.cpp` |
| `tx_invalid.json` | Same — Bitcoin tx hex incompatible with Pinkcoin's `nTime` field |

## JSON Files Still In Use

| File | Used By | Status |
|------|---------|--------|
| `base58_encode_decode.json` | `base58_tests.cpp` | Coin-agnostic, no changes needed |
| `base58_keys_invalid.json` | `base58_tests.cpp` | Coin-agnostic negative tests |
| `script_valid.json` | `script_tests.cpp` | Coin-agnostic opcode tests |
| `script_invalid.json` | `script_tests.cpp` | Coin-agnostic opcode tests |
| `mainnet_blocks.json` | `mainnet_block_tests.cpp` | Pinkcoin-native data |
