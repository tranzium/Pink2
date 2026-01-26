# Test Data Migration: Bitcoin to Pinkcoin

## Overview

The unit tests contain Bitcoin-specific test data that needs to be updated to Pinkcoin values.

## Pinkcoin Version Bytes (from `src/base58.h`)

| Type | Pinkcoin | Bitcoin | Address Prefix |
|------|----------|---------|----------------|
| PUBKEY_ADDRESS | 3 | 0 | "2" vs "1" |
| SCRIPT_ADDRESS | 28 | 5 | "A" vs "3" |
| PUBKEY_ADDRESS_TEST | 55 | 111 | |
| SCRIPT_ADDRESS_TEST | 196 | 196 | |
| SECRET_KEY (WIF) | 131 | 128 | |

## Tasks

### EASY - Update hardcoded values

- [ ] **`src/test/Checkpoints_tests.cpp`**
  - Replace Bitcoin block hashes with Pinkcoin checkpoints
  - Use blocks from `src/checkpoints.cpp` (e.g., 50000, 150000)
  - Lines 17-18: update hash values
  - Lines 19-20, 24-25, 28-29, 31: update block numbers

- [ ] **`src/test/key_tests.cpp`**
  - Lines 13-20: Replace Bitcoin WIF keys and addresses
  - Need 2 keypairs (4 variants: compressed/uncompressed)
  - Generate using: `./pink2d getnewaddress` then `./pink2d dumpprivkey <addr>`

### MEDIUM - Regenerate JSON files

- [ ] **`src/test/data/base58_keys_valid.json`**
  - Format: `[address, hex_pubkeyhash, {addrType, isPrivkey, isTestnet}]`
  - Generate 10-20 Pinkcoin addresses/keys
  - Can use wallet RPC to generate valid addresses

- [ ] **`src/test/data/base58_keys_invalid.json`**
  - Review and update if needed
  - Most invalid cases should remain invalid for Pinkcoin

### COMPLEX - Can defer

- [ ] **`src/test/data/script_valid.json`**
  - Contains Bitcoin transaction scripts
  - Options: regenerate with Pinkcoin txs, or keep (tests parsing logic)

- [ ] **`src/test/data/script_invalid.json`**
  - Same as above

- [ ] **`src/test/data/tx_valid.json`**
  - Complete Bitcoin transactions in hex
  - Would need Pinkcoin equivalents

- [ ] **`src/test/data/tx_invalid.json`**
  - Same as above

## How to Generate Test Data

### Using the wallet RPC

```bash
# Start daemon
./pink2d -daemon

# Generate new address
./pink2d getnewaddress

# Get private key (WIF format)
./pink2d dumpprivkey <address>

# Get address info
./pink2d validateaddress <address>
```

### Key test format needed

```cpp
// key_tests.cpp format
static const string strSecret1     ("WIF_UNCOMPRESSED_KEY");
static const string strSecret1C    ("WIF_COMPRESSED_KEY");
static const CBitcoinAddress addr1 ("2xxxxxxxxxxxxxxxxxxxxxxxxxx");
static const CBitcoinAddress addr1C("2xxxxxxxxxxxxxxxxxxxxxxxxxx");
```

### JSON format for base58_keys_valid.json

```json
[
    "2PinkcoinAddress...",
    "hex_of_pubkey_hash_20_bytes",
    {
        "addrType": "pubkey",
        "isPrivkey": false,
        "isTestnet": false
    }
]
```

## Notes

- The `base58_encode_decode.json` file is generic and works for any coin
- Script/transaction tests exercise parsing logic that's identical between coins
- Consider removing tests that are truly Bitcoin-specific and not applicable
